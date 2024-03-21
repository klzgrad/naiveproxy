// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

using ::quic::Perspective;

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error(MoqtError::kGenericError, "Unable to open a control stream");
    return;
  }
  control_stream->SetVisitor(std::make_unique<Stream>(
      this, control_stream, /*is_control_stream=*/true));
  control_stream_ = control_stream->GetStreamId();
  MoqtClientSetup setup = MoqtClientSetup{
      .supported_versions = std::vector<MoqtVersion>{parameters_.version},
      .role = MoqtRole::kBoth,
  };
  if (!parameters_.using_webtrans) {
    setup.path = parameters_.path;
  }
  quiche::QuicheBuffer serialized_setup = framer_.SerializeClientSetup(setup);
  bool success = control_stream->Write(serialized_setup.AsStringView());
  if (!success) {
    Error(MoqtError::kGenericError, "Failed to write client SETUP message");
    return;
  }
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
  std::move(session_terminated_callback_)(error_message);
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

void MoqtSession::Error(MoqtError code, absl::string_view error) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "MOQT session closed with code: "
                    << static_cast<int>(code) << " and message: " << error;
  error_ = std::string(error);
  session_->CloseSession(static_cast<uint64_t>(code), error);
  std::move(session_terminated_callback_)(error);
}

void MoqtSession::AddLocalTrack(const FullTrackName& full_track_name,
                                LocalTrack::Visitor* visitor) {
  local_tracks_.try_emplace(full_track_name, full_track_name, visitor);
}

// TODO: Create state that allows ANNOUNCE_OK/ERROR on spurious namespaces to
// trigger session errors.
void MoqtSession::Announce(absl::string_view track_namespace,
                           MoqtAnnounceCallback announce_callback) {
  if (pending_outgoing_announces_.contains(track_namespace)) {
    std::move(announce_callback)(
        track_namespace, "ANNOUNCE message already outstanding for namespace");
    return;
  }
  MoqtAnnounce message;
  message.track_namespace = track_namespace;
  bool success = session_->GetStreamById(*control_stream_)
                     ->Write(framer_.SerializeAnnounce(message).AsStringView());
  if (!success) {
    Error(MoqtError::kGenericError, "Failed to write ANNOUNCE message");
    return;
  }
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
  bool success =
      session_->GetStreamById(*control_stream_)
          ->Write(framer_.SerializeSubscribe(message).AsStringView());
  if (!success) {
    Error(MoqtError::kGenericError, "Failed to write SUBSCRIBE message");
    return false;
  }
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

bool MoqtSession::PublishObject(FullTrackName& full_track_name,
                                uint64_t group_id, uint64_t object_id,
                                uint64_t object_send_order,
                                MoqtForwardingPreference forwarding_preference,
                                absl::string_view payload,
                                std::optional<uint64_t> payload_length,
                                bool end_of_stream) {
  if (payload_length.has_value() && *payload_length < payload.length()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Payload too short";
    return false;
  }
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return false;
  }
  LocalTrack& track = track_it->second;
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
  object.payload_length = payload_length;
  int failures = 0;
  quiche::StreamWriteOptions write_options;
  write_options.set_send_fin(end_of_stream);
  for (auto subscription : subscriptions) {
    // TODO: kPreferDatagram should bypass stream stuff. For now, send it on the
    // stream.
    bool new_stream = false;
    std::optional<webtransport::StreamId> stream_id =
        subscription->GetStreamForSequence({group_id, object_id},
                                           forwarding_preference);
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
        subscription->AddStream(forwarding_preference, *stream_id, group_id,
                                object_id);
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
    if (quiche::WriteIntoStream(
            *stream,
            framer_.SerializeObject(object, payload, new_stream).AsStringView(),
            write_options) != absl::OkStatus()) {
      QUICHE_DLOG(ERROR) << ENDPOINT << "Failed to write OBJECT message";
      ++failures;
      continue;
    }
    QUICHE_LOG(INFO) << ENDPOINT << "Sending object "
                     << full_track_name.track_namespace << ":"
                     << full_track_name.track_name << " with sequence "
                     << object.group_id << ":" << object.object_id
                     << " on stream " << *stream_id;
    if (end_of_stream && !new_stream) {
      subscription->RemoveStream(forwarding_preference, group_id, object_id);
    }
  }
  return (failures == 0);
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
  auto it = session_->remote_tracks_.find(message.track_alias);
  RemoteTrack::Visitor* visitor = nullptr;
  absl::string_view track_namespace;
  absl::string_view track_name;
  if (it == session_->remote_tracks_.end()) {
    // SUBSCRIBE_OK has not arrived yet, but deliver it.
    auto subscribe_it = session_->active_subscribes_.find(message.subscribe_id);
    if (subscribe_it == session_->active_subscribes_.end()) {
      return;
    }
    visitor = subscribe_it->second.visitor;
    track_namespace = subscribe_it->second.message.track_namespace;
    track_name = subscribe_it->second.message.track_name;
  } else {
    visitor = it->second.visitor();
    track_namespace = it->second.full_track_name().track_namespace;
    track_name = it->second.full_track_name().track_name;
  }
  if (visitor != nullptr) {
    visitor->OnObjectFragment(
        FullTrackName(track_namespace, track_name), message.group_id,
        message.object_id, message.object_send_order,
        message.forwarding_preference, payload, end_of_message);
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
    response.role = MoqtRole::kBoth;
    bool success = stream_->Write(
        session_->framer_.SerializeServerSetup(response).AsStringView());
    if (!success) {
      session_->Error(MoqtError::kGenericError,
                      "Failed to write server SETUP message");
      return;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle role and path.
  std::move(session_->session_established_callback_)();
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
  std::move(session_->session_established_callback_)();
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
  bool success =
      stream_->Write(session_->framer_.SerializeSubscribeError(subscribe_error)
                         .AsStringView());
  if (!success) {
    session_->Error(MoqtError::kGenericError,
                    "Failed to write SUBSCRIBE_ERROR message");
  }
}

void MoqtSession::Stream::OnSubscribeMessage(const MoqtSubscribe& message) {
  std::string reason_phrase = "";
  if (!CheckIfIsControlStream()) {
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
    SendSubscribeError(message, SubscribeErrorCode::kGenericError,
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
  if (start < track.next_sequence() && track.visitor() != nullptr) {
    // TODO: Rework this. It's not good that the session notifies the
    // application -- presumably triggering the send of a bunch of objects --
    // and only then sends the Subscribe OK.
    SubscribeWindow window =
        end.has_value()
            ? SubscribeWindow(message.subscribe_id, start->group, start->object,
                              end->group, end->object)
            : SubscribeWindow(message.subscribe_id, start->group,
                              start->object);
    std::optional<absl::string_view> past_objects_available =
        track.visitor()->OnSubscribeForPast(window);
    if (past_objects_available.has_value()) {
      SendSubscribeError(message, SubscribeErrorCode::kGenericError,
                         "Object does not exist", message.track_alias);
      return;
    }
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.subscribe_id = message.subscribe_id;
  bool success = stream_->Write(
      session_->framer_.SerializeSubscribeOk(subscribe_ok).AsStringView());
  if (!success) {
    session_->Error(MoqtError::kGenericError,
                    "Failed to write SUBSCRIBE_OK message");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << message.track_namespace << ":" << message.track_name;
  if (!end.has_value()) {
    track.AddWindow(
        SubscribeWindow(message.subscribe_id, start->group, start->object));
    return;
  }
  track.AddWindow(SubscribeWindow(message.subscribe_id, start->group,
                                  start->object, end->group, end->object));
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
                  << "subscribe_id = " << message.subscribe_id
                  << subscribe.track_namespace << ":" << subscribe.track_name;
  // Copy the Remote Track from session_->active_subscribes_ to
  // session_->remote_tracks_.
  FullTrackName ftn(subscribe.track_namespace, subscribe.track_name);
  RemoteTrack::Visitor* visitor = it->second.visitor;
  session_->remote_tracks_.try_emplace(subscribe.track_alias, ftn,
                                       subscribe.track_alias, visitor);
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
}

void MoqtSession::Stream::OnAnnounceMessage(const MoqtAnnounce& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  MoqtAnnounceOk ok;
  ok.track_namespace = message.track_namespace;
  bool success =
      stream_->Write(session_->framer_.SerializeAnnounceOk(ok).AsStringView());
  if (!success) {
    session_->Error(MoqtError::kGenericError,
                    "Failed to write ANNOUNCE_OK message");
    return;
  }
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
  std::move(it->second)(message.track_namespace, message.reason_phrase);
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

}  // namespace moqt
