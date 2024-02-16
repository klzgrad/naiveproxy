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

constexpr uint64_t kMoqtErrorTrackDoesntExist = 1;
constexpr uint64_t kMoqtErrorObjectDoesntExist = 2;

constexpr int kMaxBufferedObjects = 1000;

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error("Unable to open a control stream");
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
    Error("Failed to write client SETUP message");
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

void MoqtSession::Error(absl::string_view error) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "MOQT session closed with message: " << error;
  error_ = std::string(error);
  // TODO(vasilvv): figure out the error code.
  session_->CloseSession(1, error);
  std::move(session_terminated_callback_)(error);
}

void MoqtSession::AddLocalTrack(const FullTrackName& full_track_name,
                                LocalTrack::Visitor* visitor) {
  local_tracks_.try_emplace(full_track_name, full_track_name,
                            next_track_alias_++, visitor);
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
    Error("Failed to write ANNOUNCE message");
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
  MoqtSubscribeRequest message;
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
  MoqtSubscribeRequest message;
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
  MoqtSubscribeRequest message;
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
  MoqtSubscribeRequest message;
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

bool MoqtSession::Subscribe(const MoqtSubscribeRequest& message,
                            RemoteTrack::Visitor* visitor) {
  // TODO(martinduke): support authorization info
  bool success =
      session_->GetStreamById(*control_stream_)
          ->Write(framer_.SerializeSubscribeRequest(message).AsStringView());
  if (!success) {
    Error("Failed to write SUBSCRIBE_REQUEST message");
    return false;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_REQUEST message for "
                  << message.track_namespace << ":" << message.track_name;
  FullTrackName ftn(std::string(message.track_namespace),
                    std::string(message.track_name));
  remote_tracks_.try_emplace(ftn, ftn, visitor);
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

// increment object_sequence or group_sequence depending on |start_new_group|
void MoqtSession::PublishObjectToStream(webtransport::StreamId stream_id,
                                        FullTrackName full_track_name,
                                        bool start_new_group,
                                        absl::string_view payload) {
  // TODO: check that the peer is subscribed to the next sequence.
  webtransport::Stream* stream = session_->GetStreamById(stream_id);
  if (stream == nullptr) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT to nonexistent stream";
    return;
  }
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return;
  }
  MoqtObject object;
  LocalTrack& track = track_it->second;
  object.track_id = track.track_alias();
  FullSequence& next_sequence = track.next_sequence_mutable();
  object.group_sequence = next_sequence.group;
  if (start_new_group) {
    ++object.group_sequence;
    object.object_sequence = 0;
  } else {
    object.object_sequence = next_sequence.object;
  }
  next_sequence.group = object.group_sequence;
  next_sequence.object = object.object_sequence + 1;
  if (!track.ShouldSend(object.group_sequence, object.object_sequence)) {
    QUICHE_LOG(INFO) << ENDPOINT << "Not sending object "
                     << full_track_name.track_namespace << ":"
                     << full_track_name.track_name << " with sequence "
                     << object.group_sequence << ":" << object.object_sequence
                     << " because peer is not subscribed";
    return;
  }
  object.object_send_order = 0;
  object.payload_length = payload.size();
  bool success =
      stream->Write(framer_.SerializeObject(object, payload).AsStringView());
  if (!success) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Failed to write OBJECT message";
    return;
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Sending object "
                   << full_track_name.track_namespace << ":"
                   << full_track_name.track_name << " with sequence "
                   << object.group_sequence << ":" << object.object_sequence;
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
        absl::StrCat("Control stream reset with error code ", error));
  }
}
void MoqtSession::Stream::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  if (is_control_stream_.has_value() && *is_control_stream_) {
    session_->Error(
        absl::StrCat("Control stream reset with error code ", error));
  }
}

void MoqtSession::Stream::OnObjectMessage(const MoqtObject& message,
                                          absl::string_view payload,
                                          bool end_of_message) {
  if (is_control_stream_ == true) {
    session_->Error("Received OBJECT message on control stream");
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Received OBJECT message on stream "
                    << stream_->GetStreamId() << " for track alias "
                    << message.track_id << " with sequence "
                    << message.group_sequence << ":" << message.object_sequence
                    << " length " << payload.size() << " explicit length "
                    << (message.payload_length.has_value()
                            ? (int)*message.payload_length
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
  auto it = session_->tracks_by_alias_.find(message.track_id);
  if (it == session_->tracks_by_alias_.end()) {
    // No SUBSCRIBE_OK received with this alias, buffer it.
    auto it2 = session_->object_queue_.find(message.track_id);
    std::vector<BufferedObject>* queue;
    if (it2 == session_->object_queue_.end()) {
      queue = &session_->object_queue_[message.track_id];
    } else {
      queue = &it2->second;
    }
    if (session_->num_buffered_objects_ >= kMaxBufferedObjects) {
      session_->num_buffered_objects_++;
      session_->Error("Too many buffered objects");
      return;
    }
    queue->push_back(BufferedObject(stream_->GetStreamId(), message, payload,
                                    end_of_message));
    QUIC_DLOG(INFO) << ENDPOINT << "Buffering OBJECT for track alias "
                    << message.track_id;
    return;
  }
  RemoteTrack* subscription = it->second;
  if (subscription->visitor() != nullptr) {
    subscription->visitor()->OnObjectFragment(
        subscription->full_track_name(), stream_->GetStreamId(),
        message.group_sequence, message.object_sequence,
        message.object_send_order, payload, end_of_message);
  }
  partial_object_.clear();
}

void MoqtSession::Stream::OnClientSetupMessage(const MoqtClientSetup& message) {
  if (is_control_stream_.has_value()) {
    if (!*is_control_stream_) {
      session_->Error("Received SETUP on non-control stream");
      return;
    }
  } else {
    is_control_stream_ = true;
  }
  if (perspective() == Perspective::IS_CLIENT) {
    session_->Error("Received CLIENT_SETUP from server");
    return;
  }
  if (absl::c_find(message.supported_versions, session_->parameters_.version) ==
      message.supported_versions.end()) {
    session_->Error(absl::StrCat("Version mismatch: expected 0x",
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
      session_->Error("Failed to write server SETUP message");
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
      session_->Error("Received SETUP on non-control stream");
      return;
    }
  } else {
    is_control_stream_ = true;
  }
  if (perspective() == Perspective::IS_SERVER) {
    session_->Error("Received SERVER_SETUP from client");
    return;
  }
  if (message.selected_version != session_->parameters_.version) {
    session_->Error(absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle role and path.
  std::move(session_->session_established_callback_)();
}

void MoqtSession::Stream::SendSubscribeError(
    const MoqtSubscribeRequest& message, uint64_t error_code,
    absl::string_view reason_phrase) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.track_namespace = message.track_namespace;
  subscribe_error.track_name = message.track_name;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  bool success =
      stream_->Write(session_->framer_.SerializeSubscribeError(subscribe_error)
                         .AsStringView());
  if (!success) {
    session_->Error("Failed to write SUBSCRIBE_ERROR message");
  }
}

void MoqtSession::Stream::OnSubscribeRequestMessage(
    const MoqtSubscribeRequest& message) {
  std::string reason_phrase = "";
  if (!CheckIfIsControlStream()) {
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE_REQUEST for "
                  << message.track_namespace << ":" << message.track_name;
  auto it = session_->local_tracks_.find(FullTrackName(
      std::string(message.track_namespace), std::string(message.track_name)));
  if (it == session_->local_tracks_.end()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Rejected because "
                    << message.track_namespace << ":" << message.track_name
                    << " does not exist";
    SendSubscribeError(message, kMoqtErrorTrackDoesntExist,
                       "Track does not exist");
    return;
  }
  LocalTrack& track = it->second;
  std::optional<FullSequence> start = session_->LocationToAbsoluteNumber(
      track, message.start_group, message.start_object);
  QUICHE_DCHECK(start.has_value());  // Parser enforces this.
  std::optional<FullSequence> end = session_->LocationToAbsoluteNumber(
      track, message.end_group, message.end_object);
  if (start < track.next_sequence() && track.visitor() != nullptr) {
    SubscribeWindow window = end.has_value()
                                 ? SubscribeWindow(start->group, start->object,
                                                   end->group, end->object)
                                 : SubscribeWindow(start->group, start->object);
    std::optional<absl::string_view> past_objects_available =
        track.visitor()->OnSubscribeRequestForPast(window);
    if (!past_objects_available.has_value()) {
      SendSubscribeError(message, kMoqtErrorObjectDoesntExist,
                         "Object does not exist");
      return;
    }
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.track_namespace = message.track_namespace;
  subscribe_ok.track_name = message.track_name;
  subscribe_ok.track_id = track.track_alias();
  bool success = stream_->Write(
      session_->framer_.SerializeSubscribeOk(subscribe_ok).AsStringView());
  if (!success) {
    session_->Error("Failed to write SUBSCRIBE_OK message");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << message.track_namespace << ":" << message.track_name;
  if (!end.has_value()) {
    track.AddWindow(SubscribeWindow(start->group, start->object));
    return;
  }
  track.AddWindow(
      SubscribeWindow(start->group, start->object, end->group, end->object));
}

void MoqtSession::Stream::OnSubscribeOkMessage(const MoqtSubscribeOk& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  if (session_->tracks_by_alias_.contains(message.track_id)) {
    session_->Error("Received duplicate track_alias");
    return;
  }
  auto it = session_->remote_tracks_.find(FullTrackName(
      std::string(message.track_namespace), std::string(message.track_name)));
  if (it == session_->remote_tracks_.end()) {
    session_->Error("Received SUBSCRIBE_OK for nonexistent subscribe");
    return;
  }
  // Note that if there are multiple SUBSCRIBE_OK for the same track,
  // RemoteTrack.track_alias() will be the last alias received, but
  // tracks_by_alias_ will have an entry for every track_alias received.
  // TODO: revise this data structure to make it easier to clean up
  // RemoteTracks, unless draft changes make it irrelevant.
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                  << message.track_namespace << ":" << message.track_name
                  << ", track_alias = " << message.track_id;
  RemoteTrack& track = it->second;
  track.set_track_alias(message.track_id);
  session_->tracks_by_alias_[message.track_id] = &track;
  // TODO: handle expires.
  if (track.visitor() != nullptr) {
    track.visitor()->OnReply(track.full_track_name(), std::nullopt);
  }
  // Clear the buffer for this track alias.
  auto it2 = session_->object_queue_.find(message.track_id);
  if (it2 == session_->object_queue_.end() || track.visitor() == nullptr) {
    // Nothing is buffered, or the app hasn't registered a visitor anyway.
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Processing buffered OBJECTs for track_alias "
                  << message.track_id;
  std::vector<BufferedObject>& queue = it2->second;
  for (BufferedObject& to_deliver : queue) {
    track.visitor()->OnObjectFragment(
        track.full_track_name(), to_deliver.stream_id,
        to_deliver.message.group_sequence, to_deliver.message.object_sequence,
        to_deliver.message.object_send_order, to_deliver.payload,
        to_deliver.eom);
    session_->num_buffered_objects_--;
  }
  session_->object_queue_.erase(it2);
}

void MoqtSession::Stream::OnSubscribeErrorMessage(
    const MoqtSubscribeError& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->remote_tracks_.find(FullTrackName(
      std::string(message.track_namespace), std::string(message.track_name)));
  if (it == session_->remote_tracks_.end()) {
    session_->Error("Received SUBSCRIBE_ERROR for nonexistent subscribe");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_ERROR for "
                  << message.track_namespace << ":" << message.track_name
                  << ", error = " << message.reason_phrase;
  if (it->second.visitor() != nullptr) {
    it->second.visitor()->OnReply(it->second.full_track_name(),
                                  message.reason_phrase);
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
    session_->Error("Failed to write ANNOUNCE_OK message");
    return;
  }
}

void MoqtSession::Stream::OnAnnounceOkMessage(const MoqtAnnounceOk& message) {
  if (!CheckIfIsControlStream()) {
    return;
  }
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error("Received ANNOUNCE_OK for nonexistent announce");
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
    session_->Error("Received ANNOUNCE_ERROR for nonexistent announce");
    return;
  }
  std::move(it->second)(message.track_namespace, message.reason_phrase);
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::Stream::OnParsingError(absl::string_view reason) {
  session_->Error(absl::StrCat("Parse error: ", reason));
}

bool MoqtSession::Stream::CheckIfIsControlStream() {
  if (!is_control_stream_.has_value()) {
    session_->Error("Received SUBSCRIBE_REQUEST as first message");
    return false;
  }
  if (!*is_control_stream_) {
    session_->Error("Received SUBSCRIBE_REQUEST on non-control stream");
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
