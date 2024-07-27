// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

using ::quic::Perspective;

MoqtSession::Stream* MoqtSession::GetControlStream() {
  if (!control_stream_.has_value()) {
    return nullptr;
  }
  webtransport::Stream* raw_stream = session_->GetStreamById(*control_stream_);
  if (raw_stream == nullptr) {
    return nullptr;
  }
  return static_cast<Stream*>(raw_stream->visitor());
}

void MoqtSession::SendControlMessage(quiche::QuicheBuffer message) {
  Stream* control_stream = GetControlStream();
  if (control_stream == nullptr) {
    QUICHE_LOG(DFATAL) << "Trying to send a message on the control stream "
                          "while it does not exist";
    return;
  }
  control_stream->SendOrBufferMessage(std::move(message));
}

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error(MoqtError::kInternalError, "Unable to open a control stream");
    return;
  }
  control_stream->SetVisitor(std::make_unique<Stream>(
      this, control_stream, /*is_control_stream=*/true));
  control_stream_ = control_stream->GetStreamId();
  MoqtClientSetup setup = MoqtClientSetup{
      .supported_versions = std::vector<MoqtVersion>{parameters_.version},
      .role = MoqtRole::kPubSub,
  };
  if (!parameters_.using_webtrans) {
    setup.path = parameters_.path;
  }
  SendControlMessage(framer_.SerializeClientSetup(setup));
  QUIC_DLOG(INFO) << ENDPOINT << "Send the SETUP message";
}

void MoqtSession::OnSessionClosed(webtransport::SessionErrorCode,
                                  const std::string& error_message) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session closed with message: "
                    << error_message;
  error_ = error_message;
  std::move(callbacks_.session_terminated_callback)(error_message);
}

void MoqtSession::OnIncomingBidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingBidirectionalStream()) {
    if (control_stream_.has_value()) {
      Error(MoqtError::kProtocolViolation, "Bidirectional stream already open");
      return;
    }
    stream->SetVisitor(std::make_unique<Stream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}
void MoqtSession::OnIncomingUnidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingUnidirectionalStream()) {
    stream->SetVisitor(std::make_unique<Stream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}

void MoqtSession::OnDatagramReceived(absl::string_view datagram) {
  MoqtObject message;
  absl::string_view payload = MoqtParser::ProcessDatagram(datagram, message);
  if (payload.empty()) {
    Error(MoqtError::kProtocolViolation, "Malformed datagram");
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "Received OBJECT message in datagram for subscribe_id "
                    << message.subscribe_id << " for track alias "
                    << message.track_alias << " with sequence "
                    << message.group_id << ":" << message.object_id
                    << " send_order " << message.object_send_order << " length "
                    << payload.size();
  auto [full_track_name, visitor] = TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(full_track_name, message.group_id,
                              message.object_id, message.object_send_order,
                              message.forwarding_preference, payload, true);
  }
}

void MoqtSession::Error(MoqtError code, absl::string_view error) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "MOQT session closed with code: "
                    << static_cast<int>(code) << " and message: " << error;
  error_ = std::string(error);
  session_->CloseSession(static_cast<uint64_t>(code), error);
  std::move(callbacks_.session_terminated_callback)(error);
}

void MoqtSession::AddLocalTrack(const FullTrackName& full_track_name,
                                MoqtForwardingPreference forwarding_preference,
                                LocalTrack::Visitor* visitor) {
  local_tracks_.try_emplace(full_track_name, full_track_name,
                            forwarding_preference, visitor);
}

// TODO: Create state that allows ANNOUNCE_OK/ERROR on spurious namespaces to
// trigger session errors.
void MoqtSession::Announce(absl::string_view track_namespace,
                           MoqtOutgoingAnnounceCallback announce_callback) {
  if (peer_role_ == MoqtRole::kPublisher) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{MoqtAnnounceErrorCode::kInternalError,
                                "ANNOUNCE cannot be sent to Publisher"});
    return;
  }
  if (pending_outgoing_announces_.contains(track_namespace)) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{
            MoqtAnnounceErrorCode::kInternalError,
            "ANNOUNCE message already outstanding for namespace"});
    return;
  }
  MoqtAnnounce message;
  message.track_namespace = track_namespace;
  SendControlMessage(framer_.SerializeAnnounce(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE message for "
                  << message.track_namespace;
  pending_outgoing_announces_[track_namespace] = std::move(announce_callback);
}

bool MoqtSession::HasSubscribers(const FullTrackName& full_track_name) const {
  auto it = local_tracks_.find(full_track_name);
  return (it != local_tracks_.end() && it->second.HasSubscriber());
}

bool MoqtSession::SubscribeAbsolute(absl::string_view track_namespace,
                                    absl::string_view name,
                                    uint64_t start_group, uint64_t start_object,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = MoqtSubscribeLocation(true, start_group);
  message.start_object = MoqtSubscribeLocation(true, start_object);
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(absl::string_view track_namespace,
                                    absl::string_view name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group, uint64_t end_object,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  if (end_group == start_group && end_object < start_object) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = MoqtSubscribeLocation(true, start_group);
  message.start_object = MoqtSubscribeLocation(true, start_object);
  message.end_group = MoqtSubscribeLocation(true, end_group);
  message.end_object = MoqtSubscribeLocation(true, end_object);
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeRelative(absl::string_view track_namespace,
                                    absl::string_view name, int64_t start_group,
                                    int64_t start_object,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = MoqtSubscribeLocation(false, start_group);
  message.start_object = MoqtSubscribeLocation(false, start_object);
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentGroup(absl::string_view track_namespace,
                                        absl::string_view name,
                                        RemoteTrack::Visitor* visitor,
                                        absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  // First object of current group.
  message.start_group = MoqtSubscribeLocation(false, (uint64_t)0);
  message.start_object = MoqtSubscribeLocation(true, (int64_t)0);
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::Subscribe(MoqtSubscribe& message,
                            RemoteTrack::Visitor* visitor) {
  if (peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE to subscriber peer";
    return false;
  }
  // TODO(martinduke): support authorization info
  message.subscribe_id = next_subscribe_id_++;
  FullTrackName ftn(std::string(message.track_namespace),
                    std::string(message.track_name));
  auto it = remote_track_aliases_.find(ftn);
  if (it != remote_track_aliases_.end()) {
    message.track_alias = it->second;
    if (message.track_alias >= next_remote_track_alias_) {
      next_remote_track_alias_ = message.track_alias + 1;
    }
  } else {
    message.track_alias = next_remote_track_alias_++;
  }
  SendControlMessage(framer_.SerializeSubscribe(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE message for "
                  << message.track_namespace << ":" << message.track_name;
  active_subscribes_.try_emplace(message.subscribe_id, message, visitor);
  return true;
}

std::optional<webtransport::StreamId> MoqtSession::OpenUnidirectionalStream() {
  if (!session_->CanOpenNextOutgoingUnidirectionalStream()) {
    return std::nullopt;
  }
  webtransport::Stream* new_stream =
      session_->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    return std::nullopt;
  }
  new_stream->SetVisitor(std::make_unique<Stream>(this, new_stream, false));
  return new_stream->GetStreamId();
}

std::pair<FullTrackName, RemoteTrack::Visitor*>
MoqtSession::TrackPropertiesFromAlias(const MoqtObject& message) {
  auto it = remote_tracks_.find(message.track_alias);
  RemoteTrack::Visitor* visitor = nullptr;
  if (it == remote_tracks_.end()) {
    // SUBSCRIBE_OK has not arrived yet, but deliver it.
    auto subscribe_it = active_subscribes_.find(message.subscribe_id);
    if (subscribe_it == active_subscribes_.end()) {
      return std::pair<FullTrackName, RemoteTrack::Visitor*>(
          {{"", ""}, nullptr});
    }
    ActiveSubscribe& subscribe = subscribe_it->second;
    visitor = subscribe.visitor;
    subscribe.received_object = true;
    if (subscribe.forwarding_preference.has_value()) {
      if (message.forwarding_preference != *subscribe.forwarding_preference) {
        Error(MoqtError::kProtocolViolation,
              "Forwarding preference changes mid-track");
        return std::pair<FullTrackName, RemoteTrack::Visitor*>(
            {{"", ""}, nullptr});
      }
    } else {
      subscribe.forwarding_preference = message.forwarding_preference;
    }
    return std::pair<FullTrackName, RemoteTrack::Visitor*>(
        {{subscribe.message.track_namespace, subscribe.message.track_name},
         subscribe.visitor});
  }
  RemoteTrack& track = it->second;
  if (!track.CheckForwardingPreference(message.forwarding_preference)) {
    // Incorrect forwarding preference.
    Error(MoqtError::kProtocolViolation,
          "Forwarding preference changes mid-track");
    return std::pair<FullTrackName, RemoteTrack::Visitor*>({{"", ""}, nullptr});
  }
  return std::pair<FullTrackName, RemoteTrack::Visitor*>(
      {{track.full_track_name().track_namespace,
        track.full_track_name().track_name},
       track.visitor()});
}

bool MoqtSession::PublishObject(const FullTrackName& full_track_name,
                                uint64_t group_id, uint64_t object_id,
                                uint64_t object_send_order,
                                absl::string_view payload, bool end_of_stream) {
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return false;
  }
  LocalTrack& track = track_it->second;
  MoqtForwardingPreference forwarding_preference =
      track.forwarding_preference();
  if ((forwarding_preference == MoqtForwardingPreference::kObject ||
       forwarding_preference == MoqtForwardingPreference::kDatagram) &&
      !end_of_stream) {
    QUIC_BUG(MoqtSession_PublishObject_end_of_stream_required)
        << "Forwarding preferences of Object or Datagram require stream to be "
           "immediately closed";
    return false;
  }
  track.SentSequence(FullSequence(group_id, object_id));
  std::vector<SubscribeWindow*> subscriptions =
      track.ShouldSend({group_id, object_id});
  if (subscriptions.empty()) {
    return true;
  }
  MoqtObject object;
  QUICHE_DCHECK(track.track_alias().has_value());
  object.track_alias = *track.track_alias();
  object.group_id = group_id;
  object.object_id = object_id;
  object.object_send_order = object_send_order;
  object.forwarding_preference = forwarding_preference;
  object.payload_length = payload.size();
  int failures = 0;
  quiche::StreamWriteOptions write_options;
  write_options.set_send_fin(end_of_stream);
  for (auto subscription : subscriptions) {
    if (forwarding_preference == MoqtForwardingPreference::kDatagram) {
      object.subscribe_id = subscription->subscribe_id();
      quiche::QuicheBuffer datagram =
          framer_.SerializeObjectDatagram(object, payload);
      // TODO(martinduke): It's OK to just silently fail, but better to notify
      // the app on errors.
      session_->SendOrQueueDatagram(datagram.AsStringView());
      continue;
    }
    bool new_stream = false;
    std::optional<webtransport::StreamId> stream_id =
        subscription->GetStreamForSequence(FullSequence(group_id, object_id));
    if (!stream_id.has_value()) {
      new_stream = true;
      stream_id = OpenUnidirectionalStream();
      if (!stream_id.has_value()) {
        QUICHE_DLOG(ERROR) << ENDPOINT
                           << "Sending OBJECT to nonexistent stream";
        ++failures;
        continue;
      }
      if (!end_of_stream) {
        subscription->AddStream(group_id, object_id, *stream_id);
      }
    }
    webtransport::Stream* stream = session_->GetStreamById(*stream_id);
    if (stream == nullptr) {
      QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT to nonexistent stream "
                         << *stream_id;
      ++failures;
      continue;
    }
    object.subscribe_id = subscription->subscribe_id();
    quiche::QuicheBuffer header =
        framer_.SerializeObjectHeader(object, new_stream);
    std::array<absl::string_view, 2> views = {header.AsStringView(), payload};
    if (!stream->Writev(views, write_options).ok()) {
      QUICHE_DLOG(ERROR) << ENDPOINT << "Failed to write OBJECT message";
      ++failures;
      continue;
    }
    QUICHE_LOG(INFO) << ENDPOINT << "Sending object length " << payload.length()
                     << " for " << full_track_name.track_namespace << ":"
                     << full_track_name.track_name << " with sequence "
                     << object.group_id << ":" << object.object_id
                     << " on stream " << *stream_id;
    if (end_of_stream && !new_stream) {
      subscription->RemoveStream(group_id, object_id);
    }
  }
  return (failures == 0);
}

void MoqtSession::CloseObjectStream(const FullTrackName& full_track_name,
                                    uint64_t group_id) {
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return;
  }
  LocalTrack& track = track_it->second;

  MoqtForwardingPreference forwarding_preference =
      track.forwarding_preference();
  if (forwarding_preference == MoqtForwardingPreference::kObject ||
      forwarding_preference == MoqtForwardingPreference::kDatagram) {
    QUIC_BUG(MoqtSession_CloseStreamObject_wrong_type)
        << "Forwarding preferences of Object or Datagram require stream to be "
           "immediately closed, and thus are not valid CloseObjectStream() "
           "targets";
    return;
  }

  std::vector<SubscribeWindow*> subscriptions =
      track.ShouldSend({group_id, /*object=*/0});
  for (SubscribeWindow* subscription : subscriptions) {
    std::optional<webtransport::StreamId> stream_id =
        subscription->GetStreamForSequence(
            FullSequence(group_id, /*object=*/0));
    if (!stream_id.has_value()) {
      continue;
    }
    webtransport::Stream* stream = session_->GetStreamById(*stream_id);
    if (stream == nullptr) {
      continue;
    }
    bool success = stream->SendFin();
    QUICHE_BUG_IF(MoqtSession_CloseObjectStream_fin_failed, !success);
  }
}

void MoqtSession::Stream::OnCanRead() {
  bool fin =
      quiche::ProcessAllReadableRegions(*stream_, [&](absl::string_view chunk) {
        parser_.ProcessData(chunk, /*end_of_stream=*/false);
      });
  if (fin) {
    parser_.ProcessData("", /*end_of_stream=*/true);
  }
}
void MoqtSession::Stream::OnCanWrite() {}
void MoqtSession::Stream::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  if (is_control_stream_.has_value() && *is_control_stream_) {
    session_->Error(
        MoqtError::kProtocolViolation,
        absl::StrCat("Control stream reset with error code ", error));
  }
}
void MoqtSession::Stream::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  if (is_control_stream_.has_value() && *is_control_stream_) {
    session_->Error(
        MoqtError::kProtocolViolation,
        absl::StrCat("Control stream reset with error code ", error));
  }
}

void MoqtSession::Stream::OnObjectMessage(const MoqtObject& message,
                                          absl::string_view payload,
                                          bool end_of_message) {
  if (is_control_stream_ == true) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received OBJECT message on control stream");
    return;
  }
  QUICHE_DLOG(INFO)
      << ENDPOINT << "Received OBJECT message on stream "
      << stream_->GetStreamId() << " for subscribe_id " << message.subscribe_id
      << " for track alias " << message.track_alias << " with sequence "
      << message.group_id << ":" << message.object_id << " send_order "
      << message.object_send_order << " forwarding_preference "
      << MoqtForwardingPreferenceToString(message.forwarding_preference)
      << " length " << payload.size() << " explicit length "
      << (message.payload_length.has_value() ? (int)*message.payload_length
                                             : -1)
      << (end_of_message ? "F" : "");
  if (!session_->parameters_.deliver_partial_objects) {
    if (!end_of_message) {  // Buffer partial object.
      absl::StrAppend(&partial_object_, payload);
      return;
    }
    if (!partial_object_.empty()) {  // Completes the object
      absl::StrAppend(&partial_object_, payload);
      payload = absl::string_view(partial_object_);
    }
  }
  auto [full_track_name, visitor] = session_->TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(full_track_name, message.group_id,
                              message.object_id, message.object_send_order,
                              message.forwarding_preference, payload,
                              end_of_message);
  }
  partial_object_.clear();
}

void MoqtSession::Stream::OnClientSetupMessage(const MoqtClientSetup& message) {
  if (is_control_stream_.has_value()) {
    if (!*is_control_stream_) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Received SETUP on non-control stream");
      return;
    }
  } else {
    is_control_stream_ = true;
  }
  session_->control_stream_ = stream_->GetStreamId();
  if (perspective() == Perspective::IS_CLIENT) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received CLIENT_SETUP from server");
    return;
  }
  if (absl::c_find(message.supported_versions, session_->parameters_.version) ==
      message.supported_versions.end()) {
    // TODO(martinduke): Is this the right error code? See issue #346.
    session_->Error(MoqtError::kProtocolViolation,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  if (session_->parameters_.perspective == Perspective::IS_SERVER) {
    MoqtServerSetup response;
    response.selected_version = session_->parameters_.version;
    response.role = MoqtRole::kPubSub;
    SendOrBufferMessage(session_->framer_.SerializeServerSetup(response));
    QUIC_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle role and path.
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
}

void MoqtSession::Stream::OnServerSetupMessage(const MoqtServerSetup& message) {
  if (is_control_stream_.has_value()) {
    if (!*is_control_stream_) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Received SETUP on non-control stream");
      return;
    }
  } else {
    is_control_stream_ = true;
  }
  if (perspective() == Perspective::IS_SERVER) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SERVER_SETUP from client");
    return;
  }
  if (message.selected_version != session_->parameters_.version) {
    // TODO(martinduke): Is this the right error code? See issue #346.
    session_->Error(MoqtError::kProtocolViolation,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle role and path.
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
}

void MoqtSession::Stream::SendSubscribeError(const MoqtSubscribe& message,
                                             SubscribeErrorCode error_code,
                                             absl::string_view reason_phrase,
                                             uint64_t track_alias) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.subscribe_id = message.subscribe_id;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  subscribe_error.track_alias = track_alias;
  SendOrBufferMessage(
      session_->framer_.SerializeSubscribeError(subscribe_error));
}

void MoqtSession::Stream::OnSubscribeMessage(const MoqtSubscribe& message) {
  std::string reason_phrase = "";
  if (!CheckIfIsControlStream()) {
    return;
  }
  if (session_->peer_role_ == MoqtRole::kPublisher) {
    QUIC_DLOG(INFO) << ENDPOINT << "Publisher peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE from publisher");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE for "
                  << message.track_namespace << ":" << message.track_name;
  auto it = session_->local_tracks_.find(FullTrackName(
      std::string(message.track_namespace), std::string(message.track_name)));
  if (it == session_->local_tracks_.end()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Rejected because "
                    << message.track_namespace << ":" << message.track_name
                    << " does not exist";
    SendSubscribeError(message, SubscribeErrorCode::kInternalError,
                       "Track does not exist", message.track_alias);
    return;
  }
  LocalTrack& track = it->second;
  if ((track.track_alias().has_value() &&
       message.track_alias != *track.track_alias()) ||
      session_->used_track_aliases_.contains(message.track_alias)) {
    // Propose a different track_alias.
    SendSubscribeError(message, SubscribeErrorCode::kRetryTrackAlias,
                       "Track alias already exists",
                       session_->next_local_track_alias_++);
    return;
  } else {  // Use client-provided alias.
    track.set_track_alias(message.track_alias);
    if (message.track_alias >= session_->next_local_track_alias_) {
      session_->next_local_track_alias_ = message.track_alias + 1;
    }
    session_->used_track_aliases_.insert(message.track_alias);
  }
  std::optional<FullSequence> start = session_->LocationToAbsoluteNumber(
      track, message.start_group, message.start_object);
  QUICHE_DCHECK(start.has_value());  // Parser enforces this.
  std::optional<FullSequence> end = session_->LocationToAbsoluteNumber(
      track, message.end_group, message.end_object);
  LocalTrack::Visitor::PublishPastObjectsCallback publish_past_objects;
  SubscribeWindow window =
      end.has_value()
          ? SubscribeWindow(message.subscribe_id, track.forwarding_preference(),
                            start->group, start->object, end->group,
                            end->object)
          : SubscribeWindow(message.subscribe_id, track.forwarding_preference(),
                            start->group, start->object);
  if (start < track.next_sequence() && track.visitor() != nullptr) {
    absl::StatusOr<LocalTrack::Visitor::PublishPastObjectsCallback>
        past_objects_available = track.visitor()->OnSubscribeForPast(window);
    if (!past_objects_available.ok()) {
      SendSubscribeError(message, SubscribeErrorCode::kInternalError,
                         past_objects_available.status().message(),
                         message.track_alias);
      return;
    }
    publish_past_objects = *std::move(past_objects_available);
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.subscribe_id = message.subscribe_id;
  SendOrBufferMessage(session_->framer_.SerializeSubscribeOk(subscribe_ok));
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << message.track_namespace << ":" << message.track_name;
  if (end.has_value()) {
    track.AddWindow(message.subscribe_id, start->group, start->object,
                    end->group, end->object);
  } else {
    track.AddWindow(message.subscribe_id, start->group, start->object);
  }
  if (publish_past_objects) {
    std::move(publish_past_objects)();
  }
}

void MoqtSession::Stream::OnSubscribeOkMessage(const MoqtSubscribeOk& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->active_subscribes_.find(message.subscribe_id);
  if (it == session_->active_subscribes_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_OK for nonexistent subscribe");
    return;
  }
  MoqtSubscribe& subscribe = it->second.message;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                  << "subscribe_id = " << message.subscribe_id << " "
                  << subscribe.track_namespace << ":" << subscribe.track_name;
  // Copy the Remote Track from session_->active_subscribes_ to
  // session_->remote_tracks_.
  FullTrackName ftn(subscribe.track_namespace, subscribe.track_name);
  RemoteTrack::Visitor* visitor = it->second.visitor;
  auto [track_iter, new_entry] = session_->remote_tracks_.try_emplace(
      subscribe.track_alias, ftn, subscribe.track_alias, visitor);
  if (it->second.forwarding_preference.has_value()) {
    if (!track_iter->second.CheckForwardingPreference(
            *it->second.forwarding_preference)) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Forwarding preference different in early objects");
      return;
    }
  }
  // TODO: handle expires.
  if (visitor != nullptr) {
    visitor->OnReply(ftn, std::nullopt);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::Stream::OnSubscribeErrorMessage(
    const MoqtSubscribeError& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->active_subscribes_.find(message.subscribe_id);
  if (it == session_->active_subscribes_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR for nonexistent subscribe");
    return;
  }
  if (it->second.received_object) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR after object");
    return;
  }
  MoqtSubscribe& subscribe = it->second.message;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_ERROR for "
                  << "subscribe_id = " << message.subscribe_id << " ("
                  << subscribe.track_namespace << ":" << subscribe.track_name
                  << ")" << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  RemoteTrack::Visitor* visitor = it->second.visitor;
  FullTrackName ftn(subscribe.track_namespace, subscribe.track_name);
  if (message.error_code == SubscribeErrorCode::kRetryTrackAlias) {
    // Automatically resubscribe with new alias.
    session_->remote_track_aliases_[ftn] = message.track_alias;
    session_->Subscribe(subscribe, visitor);
  } else if (visitor != nullptr) {
    visitor->OnReply(ftn, message.reason_phrase);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::Stream::OnUnsubscribeMessage(const MoqtUnsubscribe& message) {
  // Search all the tracks to find the subscribe ID.
  for (auto& [name, track] : session_->local_tracks_) {
    track.DeleteWindow(message.subscribe_id);
  }
  // TODO(martinduke): Send SUBSCRIBE_DONE in response.
}

void MoqtSession::Stream::OnAnnounceMessage(const MoqtAnnounce& message) {
  if (session_->peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Subscriber peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE from Subscriber");
    return;
  }
  if (!CheckIfIsControlStream()) {
    return;
  }
  std::optional<MoqtAnnounceErrorReason> error =
      session_->callbacks_.incoming_announce_callback(message.track_namespace);
  if (error.has_value()) {
    MoqtAnnounceError reply;
    reply.track_namespace = message.track_namespace;
    reply.error_code = error->error_code;
    reply.reason_phrase = error->reason_phrase;
    SendOrBufferMessage(session_->framer_.SerializeAnnounceError(reply));
    return;
  }
  MoqtAnnounceOk ok;
  ok.track_namespace = message.track_namespace;
  SendOrBufferMessage(session_->framer_.SerializeAnnounceOk(ok));
}

void MoqtSession::Stream::OnAnnounceOkMessage(const MoqtAnnounceOk& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_OK for nonexistent announce");
    return;
  }
  std::move(it->second)(message.track_namespace, std::nullopt);
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::Stream::OnAnnounceErrorMessage(
    const MoqtAnnounceError& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_ERROR for nonexistent announce");
    return;
  }
  std::move(it->second)(
      message.track_namespace,
      MoqtAnnounceErrorReason{message.error_code,
                              std::string(message.reason_phrase)});
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::Stream::OnParsingError(MoqtError error_code,
                                         absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

bool MoqtSession::Stream::CheckIfIsControlStream() {
  if (!is_control_stream_.has_value()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_REQUEST as first message");
    return false;
  }
  if (!*is_control_stream_) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_REQUEST on non-control stream");
    return false;
  }
  return true;
}

std::optional<FullSequence> MoqtSession::LocationToAbsoluteNumber(
    const LocalTrack& track, const std::optional<MoqtSubscribeLocation>& group,
    const std::optional<MoqtSubscribeLocation>& object) {
  FullSequence sequence;
  if (!group.has_value() || !object.has_value()) {
    return std::nullopt;
  }
  if (group->absolute) {
    sequence.group = group->absolute_value;
  } else {
    sequence.group = track.next_sequence().group + group->relative_value;
  }
  if (object->absolute) {
    sequence.object = object->absolute_value;
  } else {
    // Subtract 1 because the relative value is computed from the largest sent
    // sequence number, not the next one.
    sequence.object = track.next_sequence().object + object->relative_value - 1;
  }
  return sequence;
}

void MoqtSession::Stream::SendOrBufferMessage(quiche::QuicheBuffer message,
                                              bool fin) {
  quiche::StreamWriteOptions options;
  options.set_send_fin(fin);
  options.set_buffer_unconditionally(true);
  std::array<absl::string_view, 1> write_vector = {message.AsStringView()};
  absl::Status success = stream_->Writev(absl::MakeSpan(write_vector), options);
  if (!success.ok()) {
    session_->Error(MoqtError::kInternalError,
                    "Failed to write a control message");
  }
}

}  // namespace moqt
