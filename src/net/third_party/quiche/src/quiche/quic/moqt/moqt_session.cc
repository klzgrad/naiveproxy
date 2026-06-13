// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_namespace_stream.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_stream_map.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

namespace {

using ::quic::Perspective;

// WebTransport lets applications split a session into multiple send groups
// that have equal weight for scheduling. We don't have a use for that, so the
// send group is always the same.
constexpr webtransport::SendGroupId kMoqtSendGroupId = 0;

class DefaultPublisher : public MoqtPublisher {
 public:
  static DefaultPublisher* GetInstance() {
    static DefaultPublisher* instance = new DefaultPublisher();
    return instance;
  }

  // MoqtPublisher implementation.
  absl_nullable std::shared_ptr<MoqtTrackPublisher> GetTrack(
      const FullTrackName& track_name) override {
    QUICHE_DCHECK(track_name.IsValid());
    return nullptr;
  }
};
}  // namespace

MoqtSession::MoqtSession(webtransport::Session* session,
                         MoqtSessionParameters parameters,
                         std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
                         MoqtSessionCallbacks callbacks)
    : session_(session),
      parameters_(parameters),
      callbacks_(std::move(callbacks)),
      framer_(parameters.using_webtrans),
      publisher_(DefaultPublisher::GetInstance()),
      local_max_request_id_(parameters.max_request_id),
      alarm_factory_(std::move(alarm_factory)),
      weak_ptr_factory_(this),
      liveness_token_(std::make_shared<Empty>()) {
  if (parameters_.using_webtrans) {
    session_->SetOnDraining([this]() {
      QUICHE_DLOG(INFO) << "WebTransport session is draining";
      received_goaway_ = true;
      if (callbacks_.goaway_received_callback != nullptr) {
        std::move(callbacks_.goaway_received_callback)(absl::string_view());
      }
    });
  }
  if (parameters_.perspective == Perspective::IS_SERVER) {
    next_request_id_ = 1;
  }
  QUICHE_DCHECK(parameters_.moqt_implementation.empty());
  parameters_.moqt_implementation = kImplementationName;
}

void MoqtSession::SendControlMessage(quiche::QuicheBuffer message) {
  ControlStream* control_stream = GetControlStream();
  if (control_stream == nullptr) {
    QUICHE_LOG(DFATAL) << "Trying to send a message on the control stream "
                          "while it does not exist";
    return;
  }
  control_stream->SendOrBufferMessage(std::move(message));
}

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  std::optional<std::string> version = session_->GetNegotiatedSubprotocol();
  if (version != parameters_.version) {
    Error(MoqtError::kVersionNegotiationFailed,
          "MOQT peer chose wrong subprotocol");
    return;
  }
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }
  auto control_stream = std::make_unique<ControlStream>(this);
  if (!session_->CanOpenNextOutgoingBidirectionalStream()) {
    Error(MoqtError::kControlMessageTimeout, "Unable to open a control stream");
    return;
  }
  webtransport::Stream* stream = session_->OpenOutgoingBidirectionalStream();
  if (stream == nullptr) {
    Error(MoqtError::kInternalError, "Unable to open a control stream");
    return;
  }
  control_stream_ = control_stream->GetWeakPtr();
  control_stream->set_stream(stream);
  trace_recorder_.RecordControlStreamCreated(stream->GetStreamId());
  stream->SetVisitor(std::move(control_stream));
  MoqtClientSetup setup;
  parameters_.ToSetupParameters(setup.parameters);
  SendControlMessage(framer_.SerializeClientSetup(setup));
  QUIC_DLOG(INFO) << ENDPOINT << "Send CLIENT_SETUP";
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
  CleanUpState();
  std::move(callbacks_.session_terminated_callback)(error_message);
}

void MoqtSession::OnIncomingBidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingBidirectionalStream()) {
    auto bidi_stream = std::make_unique<UnknownBidiStream>(this, stream);
    stream->SetVisitor(std::move(bidi_stream));
    stream->visitor()->OnCanRead();
  }
}

void MoqtSession::OnIncomingUnidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingUnidirectionalStream()) {
    stream->SetVisitor(std::make_unique<IncomingDataStream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}

void MoqtSession::OnDatagramReceived(absl::string_view datagram) {
  MoqtObject message;
  bool use_default_priority;
  std::optional<absl::string_view> payload =
      ParseDatagram(datagram, message, use_default_priority);
  if (!payload.has_value()) {
    Error(MoqtError::kProtocolViolation, "Malformed datagram received");
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "Received OBJECT message in datagram for request_id "
                    << " for track alias " << message.track_alias
                    << " with sequence " << message.group_id << ":"
                    << message.object_id << " priority "
                    << message.publisher_priority << " length "
                    << payload->size();
  SubscribeRemoteTrack* track = RemoteTrackByAlias(message.track_alias);
  if (track == nullptr) {
    return;
  }
  track->OnObjectOrOk();
  if (use_default_priority) {
    message.publisher_priority = track->default_publisher_priority();
  }
  if (!track->InWindow(Location(message.group_id, message.object_id))) {
    // TODO(martinduke): a recent REQUEST_UPDATE could put us here, and it's
    // not an error.
    return;
  }
  QUICHE_CHECK(!track->is_fetch());
  SubscribeVisitor* visitor = track->visitor();
  if (visitor != nullptr) {
    // TODO(martinduke): Handle extension headers.
    PublishedObjectMetadata metadata;
    metadata.location = Location(message.group_id, message.object_id);
    metadata.subgroup = std::nullopt;
    metadata.status = message.object_status;
    metadata.publisher_priority = message.publisher_priority;
    metadata.arrival_time = callbacks_.clock->Now();
    visitor->OnObjectFragment(track->full_track_name(), metadata, *payload,
                              true);
  }
}

void MoqtSession::OnCanCreateNewOutgoingBidirectionalStream() {
  while (!pending_bidi_streams_.empty() &&
         session_->CanOpenNextOutgoingBidirectionalStream()) {
    webtransport::Stream* stream = session_->OpenOutgoingBidirectionalStream();
    pending_bidi_streams_.front()->set_stream(stream);
    // TODO(vasilvv): Distinguish between control and and non-control bidi
    // streams in trace_recorder_.
    trace_recorder_.RecordControlStreamCreated(stream->GetStreamId());
    stream->SetVisitor(std::move(pending_bidi_streams_.front()));
    pending_bidi_streams_.pop_front();
    stream->visitor()->OnCanWrite();
  }
}

void MoqtSession::Error(MoqtError code, absl::string_view error) {
  if (!error_.empty() || is_closing_) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "MOQT session closed with code: "
                    << static_cast<int>(code) << " and message: " << error;
  error_ = std::string(error);
  session_->CloseSession(static_cast<uint64_t>(code), error);
  std::move(callbacks_.session_terminated_callback)(error);
  CleanUpState();
}

std::unique_ptr<MoqtNamespaceTask> MoqtSession::SubscribeNamespace(
    TrackNamespace& prefix, SubscribeNamespaceOption option,
    const MessageParameters& parameters,
    MoqtResponseCallback response_callback) {
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Tried to send SUBSCRIBE_NAMESPACE after GOAWAY";
    return nullptr;
  }
  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE_NAMESPACE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return nullptr;
  }
  // Sanitize the option.
  switch (option) {
    case SubscribeNamespaceOption::kNamespace:
      break;
    case SubscribeNamespaceOption::kPublish:
      // TODO(martinduke): Support PUBLISH.
      return nullptr;
    case SubscribeNamespaceOption::kBoth:
      option = SubscribeNamespaceOption::kNamespace;
      break;
  }
  QUICHE_DCHECK(option == SubscribeNamespaceOption::kNamespace);
  if (!outgoing_subscribe_namespace_.SubscribeNamespace(prefix)) {
    std::move(response_callback)(MoqtRequestErrorInfo{
        RequestErrorCode::kInternalError, std::nullopt,
        "SUBSCRIBE_NAMESPACE already outstanding for namespace"});
    return nullptr;
  }
  std::unique_ptr<MoqtNamespaceSubscriberStream> state =
      std::make_unique<MoqtNamespaceSubscriberStream>(
          &framer_, next_request_id_,
          [session_weak_ptr = GetWeakPtr(), this, pref = prefix]() {
            if (!session_weak_ptr.IsValid() || is_closing_) {
              return;
            }
            outgoing_subscribe_namespace_.UnsubscribeNamespace(pref);
          },
          [session_weak_ptr = GetWeakPtr(), this](MoqtError error,
                                                  absl::string_view reason) {
            if (!session_weak_ptr.IsValid() || is_closing_) {
              return;
            }
            Error(error, reason);
          },
          std::move(response_callback));
  MoqtNamespaceSubscriberStream* state_ptr = state.get();
  if (session_->CanOpenNextOutgoingBidirectionalStream()) {
    webtransport::Stream* stream = session_->OpenOutgoingBidirectionalStream();
    state->set_stream(stream);
    stream->SetVisitor(std::move(state));
  } else {
    pending_bidi_streams_.push_back(std::move(state));
  }
  MoqtSubscribeNamespace message;
  message.request_id = next_request_id_;
  message.track_namespace_prefix = prefix;
  message.subscribe_options = SubscribeNamespaceOption::kNamespace;
  message.parameters = parameters;
  state_ptr->SendOrBufferMessage(framer_.SerializeSubscribeNamespace(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_NAMESPACE message for "
                  << message.track_namespace_prefix;
  return state_ptr->CreateTask(prefix);
}

bool MoqtSession::PublishNamespace(
    const TrackNamespace& track_namespace, const MessageParameters& parameters,
    MoqtResponseCallback response_callback,
    quiche::SingleUseCallback<void(MoqtRequestErrorInfo)> cancel_callback) {
  if (is_closing_) {
    return false;
  }
  if (publish_namespace_by_namespace_.contains(track_namespace)) {
    return false;
  }
  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send PUBLISH_NAMESPACE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Tried to send PUBLISH_NAMESPACE after GOAWAY";
    return false;
  }
  publish_namespace_by_namespace_[track_namespace] = next_request_id_;
  publish_namespace_by_id_[next_request_id_] =
      PublishNamespaceState{track_namespace, std::move(response_callback),
                            std::move(cancel_callback)};
  MoqtPublishNamespace message;
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  message.track_namespace = track_namespace;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializePublishNamespace(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent PUBLISH_NAMESPACE message for "
                  << message.track_namespace;
  return true;
}

bool MoqtSession::PublishNamespaceUpdate(
    const TrackNamespace& track_namespace, MessageParameters& parameters,
    MoqtResponseCallback response_callback) {
  if (is_closing_) {
    return false;
  }
  auto it = publish_namespace_by_namespace_.find(track_namespace);
  if (it == publish_namespace_by_namespace_.end()) {
    return false;  // Could have been destroyed by PUBLISH_NAMESPACE_CANCEL.
  }
  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send PUBLISH_NAMESPACE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  MoqtRequestUpdate message;
  message.request_id = next_request_id_;
  message.existing_request_id = it->second;
  message.parameters = parameters;
  publish_namespace_updates_[next_request_id_] = std::move(response_callback);
  next_request_id_ += 2;
  SendControlMessage(framer_.SerializeRequestUpdate(message));
  return true;
}

bool MoqtSession::PublishNamespaceDone(const TrackNamespace& track_namespace) {
  if (is_closing_) {
    return false;
  }
  auto it = publish_namespace_by_namespace_.find(track_namespace);
  if (it == publish_namespace_by_namespace_.end()) {
    return false;  // Could have been destroyed by PUBLISH_NAMESPACE_CANCEL.
  }
  MoqtPublishNamespaceDone message;
  message.request_id = it->second;
  SendControlMessage(framer_.SerializePublishNamespaceDone(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent PUBLISH_NAMESPACE_DONE message for "
                  << track_namespace;
  publish_namespace_by_id_.erase(it->second);
  publish_namespace_by_namespace_.erase(it);
  return true;
}

bool MoqtSession::PublishNamespaceCancel(const TrackNamespace& track_namespace,
                                         RequestErrorCode code,
                                         absl::string_view reason) {
  auto it = incoming_publish_namespaces_by_namespace_.find(track_namespace);
  if (it == incoming_publish_namespaces_by_namespace_.end()) {
    return false;  // Could have been destroyed by PUBLISH_NAMESPACE_DONE.
  }
  MoqtPublishNamespaceCancel message{it->second, code, std::string(reason)};
  incoming_publish_namespaces_by_id_.erase(it->second);
  incoming_publish_namespaces_by_namespace_.erase(it);
  SendControlMessage(framer_.SerializePublishNamespaceCancel(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent PUBLISH_NAMESPACE_CANCEL message for "
                  << track_namespace << " with reason " << reason;
  return true;
}

bool MoqtSession::Subscribe(const FullTrackName& name,
                            SubscribeVisitor* visitor,
                            const MessageParameters& parameters) {
  QUICHE_DCHECK(name.IsValid());

  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  if (subscribe_by_name_.contains(name)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE for track " << name
                    << " which is already subscribed";
    return false;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE after GOAWAY";
    return false;
  }
  MoqtSubscribe message(next_request_id_, name, parameters);
  next_request_id_ += 2;
  if (SupportsObjectAck() && visitor != nullptr) {
    // Since we do not expose subscribe IDs directly in the API, instead wrap
    // the session and subscribe ID in a callback.
    visitor->OnCanAckObjects(absl::bind_front(&MoqtSession::SendObjectAck, this,
                                              message.request_id));
  } else {
    QUICHE_DLOG_IF(WARNING, message.parameters.oack_window_size.has_value())
        << "Attempting to set object_ack_window on a connection that does not "
           "support it.";
    message.parameters.oack_window_size = std::nullopt;
  }
  SendControlMessage(framer_.SerializeSubscribe(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE message for "
                  << message.full_track_name;
  auto track = std::make_unique<SubscribeRemoteTrack>(message, visitor);
  subscribe_by_name_.emplace(message.full_track_name, track.get());
  upstream_by_id_.emplace(message.request_id, std::move(track));
  return true;
}

bool MoqtSession::SubscribeUpdate(const FullTrackName& name,
                                  const MessageParameters& parameters,
                                  MoqtResponseCallback response_callback) {
  QUICHE_DCHECK(name.IsValid());
  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  auto it = subscribe_by_name_.find(name);
  if (it == subscribe_by_name_.end()) {
    return false;
  }
  pending_subscribe_updates_[next_request_id_] = {name, parameters,
                                                  std::move(response_callback)};
  MoqtRequestUpdate update{next_request_id_, it->second->request_id(),
                           parameters};
  next_request_id_ += 2;
  SendControlMessage(framer_.SerializeRequestUpdate(update));
  return true;
}

void MoqtSession::Unsubscribe(const FullTrackName& name) {
  if (is_closing_) {
    return;
  }
  QUICHE_DCHECK(name.IsValid());
  SubscribeRemoteTrack* track = RemoteTrackByName(name);
  if (track == nullptr) {
    return;
  }
  QUICHE_DCHECK(name.IsValid());
  QUIC_DLOG(INFO) << ENDPOINT << "Sent UNSUBSCRIBE message for " << name;
  MoqtUnsubscribe message;
  message.request_id = track->request_id();
  SendControlMessage(framer_.SerializeUnsubscribe(message));
  DestroySubscription(track);
}

bool MoqtSession::Fetch(const FullTrackName& name,
                        FetchResponseCallback callback, Location start,
                        uint64_t end_group, std::optional<uint64_t> end_object,
                        MessageParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  if (next_request_id_ >= peer_max_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send FETCH with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send FETCH after GOAWAY";
    return false;
  }
  MoqtFetch message;
  Location end_location = end_object.has_value()
                              ? Location(end_group, *end_object)
                              : Location(end_group, kMaxObjectId);
  message.fetch = StandaloneFetch(name, start, end_location);
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeFetch(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent FETCH message for " << name;
  auto fetch = std::make_unique<UpstreamFetch>(
      message, std::get<StandaloneFetch>(message.fetch), std::move(callback));
  upstream_by_id_.emplace(message.request_id, std::move(fetch));
  return true;
}

bool MoqtSession::RelativeJoiningFetch(const FullTrackName& name,
                                       SubscribeVisitor* visitor,
                                       uint64_t num_previous_groups,
                                       MessageParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  return RelativeJoiningFetch(
      name, visitor,
      [this, id = next_request_id_](std::unique_ptr<MoqtFetchTask> fetch_task) {
        // Move the fetch_task to the subscribe to plumb into its visitor.
        RemoteTrack* track = RemoteTrackById(id);
        if (track == nullptr || track->is_fetch()) {
          fetch_task.release();
          return;
        }
        auto* subscribe = absl::down_cast<SubscribeRemoteTrack*>(track);
        RemoteTrackByName(track->full_track_name());
        subscribe->OnJoiningFetchReady(std::move(fetch_task));
      },
      num_previous_groups, parameters);
}

bool MoqtSession::RelativeJoiningFetch(const FullTrackName& name,
                                       SubscribeVisitor* visitor,
                                       FetchResponseCallback callback,
                                       uint64_t num_previous_groups,
                                       MessageParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  if ((next_request_id_ + 2) >= peer_max_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send JOINING_FETCH with ID "
                    << (next_request_id_ + 2)
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  MessageParameters subscribe_parameters = parameters;
  subscribe_parameters.subscription_filter.emplace(
      MoqtFilterType::kLargestObject);
  if (!Subscribe(name, visitor, subscribe_parameters)) {
    return false;
  }

  MoqtFetch fetch;
  fetch.request_id = next_request_id_;
  next_request_id_ += 2;
  fetch.fetch = JoiningFetchRelative{fetch.request_id - 2, num_previous_groups};
  fetch.parameters = parameters;
  SendControlMessage(framer_.SerializeFetch(fetch));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent Joining FETCH message for " << name;
  auto upstream_fetch =
      std::make_unique<UpstreamFetch>(fetch, name, std::move(callback));
  upstream_by_id_.emplace(fetch.request_id, std::move(upstream_fetch));
  return true;
}

void MoqtSession::GoAway(absl::string_view new_session_uri) {
  if (sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send multiple GOAWAY";
    return;
  }
  if (!new_session_uri.empty() && !new_session_uri.empty()) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Client tried to send GOAWAY with new session URI";
    return;
  }
  MoqtGoAway message;
  message.new_session_uri = std::string(new_session_uri);
  SendControlMessage(framer_.SerializeGoAway(message));
  sent_goaway_ = true;
  goaway_timeout_alarm_ = absl::WrapUnique(
      alarm_factory_->CreateAlarm(new GoAwayTimeoutDelegate(this)));
  goaway_timeout_alarm_->Set(callbacks_.clock->ApproximateNow() +
                             kDefaultGoAwayTimeout);
}

void MoqtSession::PublishedFetch::FetchStreamVisitor::OnCanWrite() {
  std::shared_ptr<PublishedFetch> fetch = fetch_.lock();
  if (fetch == nullptr) {
    return;
  }
  PublishedObject object;
  while (stream_->CanWrite()) {
    MoqtFetchTask::GetNextObjectResult result =
        fetch->fetch_task()->GetNextObject(object);
    switch (result) {
      case MoqtFetchTask::GetNextObjectResult::kSuccess:
        // Skip ObjectDoesNotExist in FETCH.
        if (object.metadata.status != MoqtObjectStatus::kNormal) {
          QUIC_BUG(quic_bug_got_doesnotexist_in_fetch)
              << "Got Non-normal object in FETCH";
          continue;
        }
        if (fetch->session_->WriteObjectToStream(
                stream_, fetch->request_id(), object.metadata,
                std::move(object.payload), MoqtDataStreamType::Fetch(),
                // last Object ID doesn't matter for FETCH, just use zero.
                last_object_, /*fin=*/false)) {
          last_object_ = object.metadata;
        }
        break;
      case MoqtFetchTask::GetNextObjectResult::kPending:
        return;
      case MoqtFetchTask::GetNextObjectResult::kEof:
        // TODO(martinduke): Either prefetch the next object, or alter the API
        // so that we're not sending FIN in a separate frame.
        if (!webtransport::SendFinOnStream(*stream_).ok()) {
          QUIC_DVLOG(1) << "Sending FIN onStream " << stream_->GetStreamId()
                        << " failed";
        }
        return;
      case MoqtFetchTask::GetNextObjectResult::kError:
        stream_->ResetWithUserCode(static_cast<webtransport::StreamErrorCode>(
            fetch->fetch_task()->GetStatus().code()));
        return;
    }
  }
}

void MoqtSession::GoAwayTimeoutDelegate::OnAlarm() {
  session_->Error(MoqtError::kGoawayTimeout,
                  "Peer did not close session after GOAWAY");
}

bool MoqtSession::PublishIsDone(uint64_t request_id, PublishDoneCode code,
                                absl::string_view error_reason) {
  if (is_closing_) {
    return false;
  }
  auto it = published_subscriptions_.find(request_id);
  if (it == published_subscriptions_.end()) {
    return false;
  }

  PublishedSubscription& subscription = *it->second;

  MoqtPublishDone publish_done;
  publish_done.request_id = request_id;
  publish_done.status_code = code;
  publish_done.stream_count = subscription.streams_opened();
  publish_done.error_reason = error_reason;
  // TODO(martinduke): It is technically correct, but not good, to simply
  // reset all the streams in order to send PUBLISH_DONE. It's better to wait
  // until streams FIN naturally, where possible.
  QUIC_DLOG(INFO) << ENDPOINT << "Sending PUBLISH_DONE message for "
                  << subscription.publisher().GetTrackName();
  published_subscriptions_.erase(it);
  SendControlMessage(framer_.SerializePublishDone(publish_done));
  return true;
}

void MoqtSession::MaybeDestroySubscription(SubscribeRemoteTrack* subscribe) {
  if (subscribe != nullptr && subscribe->all_streams_closed()) {
    DestroySubscription(subscribe);
  }
}

void MoqtSession::DestroySubscription(SubscribeRemoteTrack* subscribe) {
  if (subscribe->ErrorIsAllowed()) {
    subscribe->visitor()->OnReply(
        subscribe->full_track_name(),
        MoqtRequestErrorInfo{RequestErrorCode::kNotSupported, std::nullopt,
                             "Subscription closed"});
  } else {
    subscribe->visitor()->OnPublishDone(subscribe->full_track_name());
  }
  subscribe_by_name_.erase(subscribe->full_track_name());
  if (subscribe->track_alias().has_value()) {
    subscribe_by_alias_.erase(*subscribe->track_alias());
  }
  upstream_by_id_.erase(subscribe->request_id());
}

webtransport::Stream* MoqtSession::OpenOrQueueDataStream(
    uint64_t subscription_id, const NewStreamParameters& parameters) {
  auto it = published_subscriptions_.find(subscription_id);
  if (it == published_subscriptions_.end()) {
    // It is possible that the subscription has been discarded while the stream
    // was in the queue; discard those streams.
    return nullptr;
  }
  PublishedSubscription& subscription = *it->second;
  if (!session_->CanOpenNextOutgoingUnidirectionalStream()) {
    subscription.AddQueuedOutgoingDataStream(parameters);
    // The subscription will notify the session about how to update the
    // session's queue.
    // TODO: limit the number of streams in the queue.
    return nullptr;
  }
  return OpenDataStream(subscription, parameters);
}

webtransport::Stream* MoqtSession::OpenDataStream(
    PublishedSubscription& subscription,
    const NewStreamParameters& parameters) {
  webtransport::Stream* new_stream =
      session_->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    QUICHE_BUG(MoqtSession_OpenDataStream_blocked)
        << "OpenDataStream called when creation of new streams is blocked.";
    return nullptr;
  }
  new_stream->SetVisitor(std::make_unique<OutgoingDataStream>(
      this, new_stream, subscription, parameters));
  subscription.OnDataStreamCreated(new_stream->GetStreamId(), parameters.index);
  return new_stream;
}

bool MoqtSession::OpenDataStream(std::shared_ptr<PublishedFetch> fetch,
                                 webtransport::SendOrder send_order) {
  webtransport::Stream* new_stream =
      session_->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    QUICHE_BUG(MoqtSession_OpenDataStream_blocked)
        << "OpenDataStream called when creation of new streams is blocked.";
    return false;
  }
  fetch->SetStreamId(new_stream->GetStreamId());
  new_stream->SetPriority(webtransport::StreamPriority{
      /*send_group_id=*/kMoqtSendGroupId, send_order});
  // The line below will lead to updating ObjectsAvailableCallback in the
  // FetchTask to call OnCanWrite() on the stream. If there is an object
  // available, the callback will be invoked synchronously (i.e. before
  // SetVisitor() returns).
  new_stream->SetVisitor(
      std::make_unique<PublishedFetch::FetchStreamVisitor>(fetch, new_stream));
  return true;
}

SubscribeRemoteTrack* MoqtSession::RemoteTrackByAlias(uint64_t track_alias) {
  auto it = subscribe_by_alias_.find(track_alias);
  if (it == subscribe_by_alias_.end()) {
    return nullptr;
  }
  return it->second;
}

RemoteTrack* MoqtSession::RemoteTrackById(uint64_t request_id) {
  auto it = upstream_by_id_.find(request_id);
  if (it == upstream_by_id_.end()) {
    return nullptr;
  }
  return it->second.get();
}

SubscribeRemoteTrack* MoqtSession::RemoteTrackByName(
    const FullTrackName& name) {
  QUICHE_DCHECK(name.IsValid());
  auto it = subscribe_by_name_.find(name);
  if (it == subscribe_by_name_.end()) {
    return nullptr;
  }
  return it->second;
}

void MoqtSession::OnCanCreateNewOutgoingUnidirectionalStream() {
  while (!subscribes_with_queued_outgoing_data_streams_.empty() &&
         session_->CanOpenNextOutgoingUnidirectionalStream()) {
    auto next = subscribes_with_queued_outgoing_data_streams_.rbegin();
    auto subscription = published_subscriptions_.find(next->subscription_id);
    if (subscription == published_subscriptions_.end()) {
      auto fetch = incoming_fetches_.find(next->subscription_id);
      // Create the stream if the fetch still exists.
      if (fetch != incoming_fetches_.end() &&
          !OpenDataStream(fetch->second, next->send_order)) {
        return;  // A QUIC_BUG has fired because this shouldn't happen.
      }
      // FETCH needs only one stream, and can be deleted from the queue. Or,
      // there is no subscribe and no fetch; the entry in the queue is invalid.
      subscribes_with_queued_outgoing_data_streams_.erase((++next).base());
      continue;
    }
    // Pop the item from the subscription's queue, which might update
    // subscribes_with_queued_outgoing_data_streams_.
    NewStreamParameters next_queued_stream =
        subscription->second->NextQueuedOutgoingDataStream();
    // Check if Group is too old.
    if (next_queued_stream.index.group <
        subscription->second->first_active_group()) {
      // The stream is too old to be sent.
      continue;
    }
    // Open the stream.
    webtransport::Stream* stream =
        OpenDataStream(*subscription->second, next_queued_stream);
    if (stream != nullptr) {
      stream->visitor()->OnCanWrite();
    }
  }
}

void MoqtSession::UpdateQueuedSendOrder(
    uint64_t request_id, std::optional<webtransport::SendOrder> old_send_order,
    std::optional<webtransport::SendOrder> new_send_order) {
  if (old_send_order == new_send_order) {
    return;
  }
  if (old_send_order.has_value()) {
    subscribes_with_queued_outgoing_data_streams_.erase(
        SubscriptionWithQueuedStream{*old_send_order, request_id});
  }
  if (new_send_order.has_value()) {
    subscribes_with_queued_outgoing_data_streams_.emplace(*new_send_order,
                                                          request_id);
  }
}

void MoqtSession::GrantMoreRequests(uint64_t num_requests) {
  local_max_request_id_ += (num_requests * 2);
  MoqtMaxRequestId message;
  message.max_request_id = local_max_request_id_;
  SendControlMessage(framer_.SerializeMaxRequestId(message));
}

bool MoqtSession::ValidateRequestId(uint64_t request_id) {
  if (request_id >= local_max_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received request with too large ID";
    Error(MoqtError::kTooManyRequests, "Received request with too large ID");
    return false;
  }
  if ((request_id % 2 == 0) !=
      (parameters_.perspective == Perspective::IS_SERVER)) {
    QUICHE_DLOG(INFO) << ENDPOINT << "Request ID evenness incorrect";
    Error(MoqtError::kInvalidRequestId, "Request ID evenness incorrect");
    return false;
  }
  if (published_subscriptions_.contains(request_id) ||
      incoming_fetches_.contains(request_id) ||
      incoming_track_status_.contains(request_id) ||
      incoming_publish_namespaces_by_id_.contains(request_id)) {
    QUICHE_DLOG(INFO) << ENDPOINT << "Duplicate request ID";
    Error(MoqtError::kInvalidRequestId, "Duplicate request ID");
    return false;
  }
  return true;
}

void MoqtSession::UnknownBidiStream::OnCanRead() {
  if (!parser_.ReadUntilMessageTypeKnown()) {
    // Got an early FIN.
    stream_->ResetWithUserCode(kResetCodeCancelled);
    return;
  }
  if (!parser_.message_type().has_value()) {
    return;
  }
  MoqtMessageType message_type =
      static_cast<MoqtMessageType>(*parser_.message_type());
  switch (message_type) {
    case MoqtMessageType::kClientSetup: {
      if (session_->control_stream_.GetIfAvailable() != nullptr) {
        session_->Error(MoqtError::kProtocolViolation,
                        "Multiple control streams");
        return;
      }
      auto control_stream = std::make_unique<ControlStream>(session_);
      // Store a reference to the stream context when the current context is
      // destroyed below.
      ControlStream* temp_stream = control_stream.get();
      session_->control_stream_ = temp_stream->GetWeakPtr();
      control_stream->set_stream(stream_);
      // Deletes the UnknownBidiStream object; no class access after this
      // point.
      stream_->SetVisitor(std::move(control_stream));
      temp_stream->OnCanRead();
      break;
    }
    case MoqtMessageType::kSubscribeNamespace: {
      auto namespace_stream = std::make_unique<MoqtNamespacePublisherStream>(
          &session_->framer_, stream_,
          [session = session_](MoqtError code, absl::string_view reason) {
            session->Error(code, reason);
          },
          &session_->incoming_subscribe_namespace_,
          session_->callbacks_.incoming_subscribe_namespace_callback);
      MoqtNamespacePublisherStream* temp_stream = namespace_stream.get();
      stream_->SetVisitor(std::move(namespace_stream));
      // The UnknownBidiStream object is deleted; no class access after this
      // point.
      temp_stream->OnCanRead();
      break;
    }
    default:
      session_->Error(MoqtError::kProtocolViolation,
                      "Unexpected message type received to start bidi stream");
      return;
  }
}

void MoqtSession::ControlStream::set_stream(
    webtransport::Stream* absl_nonnull stream) {
  stream->SetPriority(
      webtransport::StreamPriority{/*send_group_id=*/kMoqtSendGroupId,
                                   /*send_order=*/kMoqtControlStreamSendOrder});
  if (session_->perspective() == Perspective::IS_SERVER) {
    MoqtBidiStreamBase::set_stream(stream, MoqtMessageType::kClientSetup);
  } else {
    MoqtBidiStreamBase::set_stream(stream, std::nullopt);
  }
}

void MoqtSession::ControlStream::OnClientSetupMessage(
    const MoqtClientSetup& message) {
  if (session_->perspective() == Perspective::IS_CLIENT) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received CLIENT_SETUP from server");
    return;
  }
  session_->peer_supports_object_ack_ =
      message.parameters.support_object_acks.value_or(
          kDefaultSupportObjectAcks);
  session_->peer_max_request_id_ =
      message.parameters.max_request_id.value_or(kDefaultMaxRequestId);
  QUICHE_DLOG(INFO) << "Received CLIENT_SETUP";
  MoqtServerSetup response;
  session_->parameters_.ToSetupParameters(response.parameters);
  SendOrBufferMessage(session_->framer_.SerializeServerSetup(response));
  QUICHE_DLOG(INFO) << "Sent SERVER_SETUP";
  // TODO: handle path.
  std::move(session_->callbacks_.session_established_callback)();
}

void MoqtSession::ControlStream::OnServerSetupMessage(
    const MoqtServerSetup& message) {
  if (perspective() == Perspective::IS_SERVER) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SERVER_SETUP from client");
    return;
  }
  session_->peer_supports_object_ack_ =
      message.parameters.support_object_acks.value_or(
          kDefaultSupportObjectAcks);
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle path.
  session_->peer_max_request_id_ =
      message.parameters.max_request_id.value_or(kDefaultMaxRequestId);
  std::move(session_->callbacks_.session_established_callback)();
}

void MoqtSession::ControlStream::OnSubscribeMessage(
    const MoqtSubscribe& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE for "
                  << message.full_track_name;
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE after GOAWAY";
    SendRequestError(message.request_id, RequestErrorCode::kUnauthorized,
                     std::nullopt, "SUBSCRIBE after GOAWAY");
    return;
  }
  if (session_->subscribed_track_names_.contains(message.full_track_name)) {
    SendRequestError(message.request_id,
                     RequestErrorCode::kDuplicateSubscription, std::nullopt,
                     "");
    return;
  }
  const FullTrackName& track_name = message.full_track_name;
  std::shared_ptr<MoqtTrackPublisher> track_publisher =
      session_->publisher_->GetTrack(track_name);
  if (track_publisher == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "SUBSCRIBE for " << track_name
                    << " rejected by the application: does not exist";
    SendRequestError(message.request_id, RequestErrorCode::kDoesNotExist,
                     std::nullopt, "not found");
    return;
  }

  MoqtPublishingMonitorInterface* monitoring = nullptr;
  auto monitoring_it =
      session_->monitoring_interfaces_for_published_tracks_.find(track_name);
  if (monitoring_it !=
      session_->monitoring_interfaces_for_published_tracks_.end()) {
    monitoring = monitoring_it->second;
    session_->monitoring_interfaces_for_published_tracks_.erase(monitoring_it);
  }

  MoqtTrackPublisher* track_publisher_ptr = track_publisher.get();
  auto subscription = std::make_unique<PublishedSubscription>(
      session_, track_publisher, message, session_->next_local_track_alias_++,
      monitoring);
  PublishedSubscription* subscription_ptr = subscription.get();
  auto [it, success] = session_->published_subscriptions_.emplace(
      message.request_id, std::move(subscription));
  if (!success) {
    QUICHE_NOTREACHED();  // ValidateRequestId() should have caught this.
  }
  track_publisher_ptr->AddObjectListener(subscription_ptr);
}

void MoqtSession::ControlStream::OnSubscribeOkMessage(
    const MoqtSubscribeOk& message) {
  RemoteTrack* track = session_->RemoteTrackById(message.request_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "request_id = " << message.request_id
                    << " but no track exists";
    // Subscription state might have been destroyed for internal reasons.
    return;
  }
  if (track->is_fetch()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_OK for a FETCH");
    return;
  }
  if (message.parameters.largest_object.has_value()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "request_id = " << message.request_id << " "
                    << track->full_track_name()
                    << " largest_id = " << *message.parameters.largest_object;
  } else {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "request_id = " << message.request_id << " "
                    << track->full_track_name();
  }
  SubscribeRemoteTrack* subscribe =
      absl::down_cast<SubscribeRemoteTrack*>(track);
  subscribe->OnObjectOrOk();
  auto [it, success] =
      session_->subscribe_by_alias_.try_emplace(message.track_alias, subscribe);
  if (!success) {
    session_->Error(MoqtError::kDuplicateTrackAlias, "");
    return;
  }
  subscribe->set_track_alias(message.track_alias);
  std::optional<SubscriptionFilter> filter =
      subscribe->parameters().subscription_filter;
  if (filter.has_value()) {
    filter->OnLargestObject(message.parameters.largest_object);
  }
  subscribe->set_publisher_delivery_timeout(
      message.extensions.delivery_timeout());
  // TODO(martinduke): Is there anything to do with EXPIRES?
  subscribe->set_default_publisher_priority(
      message.extensions.default_publisher_priority());
  if (!subscribe->parameters().group_order.has_value()) {
    // Use publisher default because the subscriber didn't care.
    subscribe->parameters().group_order =
        message.extensions.default_publisher_group_order();
  }
  subscribe->set_dynamic_groups(message.extensions.dynamic_groups());
  if (subscribe->visitor() != nullptr) {
    subscribe->visitor()->OnReply(
        track->full_track_name(),
        SubscribeOkData{message.parameters, message.extensions});
  }
}

void MoqtSession::ControlStream::OnRequestOkMessage(
    const MoqtRequestOk& message) {
  if (session_->upstream_by_id_.contains(message.request_id)) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received REQUEST_OK for SUBSCRIBE, FETCH, or PUBLISH");
    return;
  }
  // Response to REQUEST_UPDATE for a subscribe.
  auto ru_it = session_->pending_subscribe_updates_.find(message.request_id);
  if (ru_it != session_->pending_subscribe_updates_.end()) {
    auto sub_it = session_->subscribe_by_name_.find(ru_it->second.name);
    if (sub_it == session_->subscribe_by_name_.end()) {
      std::move(ru_it->second.response_callback)(
          MoqtRequestErrorInfo{RequestErrorCode::kDoesNotExist, std::nullopt,
                               "subscription does not exist anymore"});
      session_->pending_subscribe_updates_.erase(ru_it);
      return;
    }
    sub_it->second->parameters().Update(ru_it->second.parameters);
    std::move(ru_it->second.response_callback)(std::nullopt);
    session_->pending_subscribe_updates_.erase(ru_it);
    return;
  }
  // Response to PUBLISH_NAMESPACE.
  auto pn_it = session_->publish_namespace_by_id_.find(message.request_id);
  if (pn_it != session_->publish_namespace_by_id_.end()) {
    if (pn_it->second.response_callback == nullptr) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Multiple responses for PUBLISH_NAMESPACE");
      return;
    }
    std::move(pn_it->second.response_callback)(std::nullopt);
    return;
  }
  // Response to SUBSCRIBE_NAMESPACE is handled in the NamespaceStream.
  // TRACK_STATUS response would go here, but we don't support upstream
  // TRACK_STATUS.
  // If it doesn't match any state, it might be because the local application
  // cancelled the request. Do nothing.
  // TODO(martinduke): Do something with parameters.
}

void MoqtSession::ControlStream::OnRequestErrorMessage(
    const MoqtRequestError& message) {
  MoqtRequestErrorInfo error_info{message.error_code, message.retry_interval,
                                  message.reason_phrase};
  // TODO(martinduke): Do something with retry_interval.
  RemoteTrack* track = session_->RemoteTrackById(message.request_id);
  if (track != nullptr) {
    // It's in response to SUBSCRIBE or FETCH.
    if (!track->ErrorIsAllowed()) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Received REQUEST_ERROR after REQUEST_OK or objects");
      return;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Received the REQUEST_ERROR for "
                    << "request_id = " << message.request_id << " ("
                    << track->full_track_name() << ")"
                    << ", error = " << static_cast<uint64_t>(message.error_code)
                    << " (" << message.reason_phrase << ")";
    if (track->is_fetch()) {
      UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(track);
      absl::Status status =
          RequestErrorCodeToStatus(message.error_code, message.reason_phrase);
      fetch->OnFetchResult(Location(0, 0), status, nullptr);
    } else {
      SubscribeRemoteTrack* subscribe =
          absl::down_cast<SubscribeRemoteTrack*>(track);
      // Delete the by-name entry at this point prevents Subscribe() from
      // throwing an error due to a duplicate track name. The other entries for
      // this subscribe will be deleted after calling Subscribe().
      session_->subscribe_by_name_.erase(subscribe->full_track_name());
      if (subscribe->visitor() != nullptr) {
        subscribe->visitor()->OnReply(subscribe->full_track_name(), error_info);
      }
    }
    if (!session_->is_closing_) {
      // The visitor might have closed the session.
      session_->upstream_by_id_.erase(message.request_id);
    }
    return;
  }
  // Response to REQUEST_UPDATE for a subscribe.
  auto ru_it = session_->pending_subscribe_updates_.find(message.request_id);
  if (ru_it != session_->pending_subscribe_updates_.end()) {
    std::move(ru_it->second.response_callback)(error_info);
    session_->pending_subscribe_updates_.erase(ru_it);
    return;
  }
  // Response to PUBLISH_NAMESPACE.
  auto pn_it = session_->publish_namespace_by_id_.find(message.request_id);
  if (pn_it != session_->publish_namespace_by_id_.end()) {
    if (pn_it->second.response_callback == nullptr) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Multiple responses for PUBLISH_NAMESPACE");
      return;
    }
    std::move(pn_it->second.response_callback)(error_info);
    session_->publish_namespace_by_namespace_.erase(
        pn_it->second.track_namespace);
    session_->publish_namespace_by_id_.erase(pn_it);
    return;
  }
  // Response to SUBSCRIBE_NAMESPACE is handled in the NamespaceStream.
  // TRACK_STATUS response would go here, but we don't support upstream
  // TRACK_STATUS.
  // If it doesn't match any state, it might be because the local application
  // cancelled the request. Do nothing.
}

void MoqtSession::ControlStream::OnUnsubscribeMessage(
    const MoqtUnsubscribe& message) {
  auto it = session_->published_subscriptions_.find(message.request_id);
  if (it == session_->published_subscriptions_.end()) {
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received an UNSUBSCRIBE for "
                  << it->second->publisher().GetTrackName();
  session_->published_subscriptions_.erase(it);
}

void MoqtSession::ControlStream::OnPublishDoneMessage(
    const MoqtPublishDone& message) {
  auto it = session_->upstream_by_id_.find(message.request_id);
  if (it == session_->upstream_by_id_.end()) {
    return;
  }
  auto* subscribe = absl::down_cast<SubscribeRemoteTrack*>(it->second.get());
  QUIC_DLOG(INFO) << ENDPOINT << "Received a PUBLISH_DONE for "
                  << it->second->full_track_name();
  subscribe->OnPublishDone(
      message.stream_count, session_->callbacks_.clock,
      absl::WrapUnique(session_->alarm_factory_->CreateAlarm(
          new PublishDoneDelegate(session_, subscribe))));
  session_->MaybeDestroySubscription(subscribe);
}

void MoqtSession::ControlStream::OnRequestUpdateMessage(
    const MoqtRequestUpdate& message) {
  auto it =
      session_->published_subscriptions_.find(message.existing_request_id);
  if (it != session_->published_subscriptions_.end()) {
    // It's updating SUBSCRIBE.
    it->second->Update(message.parameters);
    SendRequestOk(message.request_id, MessageParameters());
    return;
  }
  auto pn_it =
      session_->publish_namespace_by_id_.find(message.existing_request_id);
  if (pn_it != session_->publish_namespace_by_id_.end()) {
    // It's updating PUBLISH_NAMESPACE.
    quiche::QuicheWeakPtr<MoqtSessionInterface> session_weakptr =
        session_->GetWeakPtr();
    TrackNamespace track_namespace = pn_it->second.track_namespace;
    session_->callbacks().incoming_publish_namespace_callback(
        pn_it->second.track_namespace, message.parameters,
        [&](std::optional<MoqtRequestErrorInfo> error) {
          MoqtSession* session =
              absl::down_cast<MoqtSession*>(session_weakptr.GetIfAvailable());
          if (session == nullptr) {
            return;
          }
          if (error.has_value()) {
            SendRequestError(message.request_id, *error);
            session->incoming_publish_namespaces_by_id_.erase(
                message.request_id);
            session->incoming_publish_namespaces_by_namespace_.erase(
                track_namespace);
          } else {
            SendRequestOk(message.request_id, MessageParameters());
          }
        });
    return;
  }
  // TODO(martinduke): Check all the request types.
  // Does not match any known request.
  SendRequestError(message.request_id, RequestErrorCode::kNotSupported,
                   std::nullopt, "No support for update of this type");
}

void MoqtSession::ControlStream::OnPublishNamespaceMessage(
    const MoqtPublishNamespace& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received a PUBLISH_NAMESPACE after GOAWAY";
    SendRequestError(message.request_id, RequestErrorCode::kUnauthorized,
                     std::nullopt, "PUBLISH_NAMESPACE after GOAWAY");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a PUBLISH_NAMESPACE for "
                  << message.track_namespace;
  auto [it, inserted] =
      session_->incoming_publish_namespaces_by_namespace_.emplace(
          message.track_namespace, message.request_id);
  if (!inserted) {
    SendRequestError(message.request_id,
                     RequestErrorCode::kDuplicateSubscription, std::nullopt,
                     "Duplicate PUBLISH_NAMESPACE");
    return;
  }
  quiche::QuicheWeakPtr<MoqtSessionInterface> session_weakptr =
      session_->GetWeakPtr();
  session_->incoming_publish_namespaces_by_id_[message.request_id] =
      message.track_namespace;
  session_->callbacks_.incoming_publish_namespace_callback(
      message.track_namespace, message.parameters,
      [&](std::optional<MoqtRequestErrorInfo> error) {
        MoqtSession* session =
            absl::down_cast<MoqtSession*>(session_weakptr.GetIfAvailable());
        if (session == nullptr) {
          return;
        }
        if (error.has_value()) {
          SendRequestError(message.request_id, *error);
          session->incoming_publish_namespaces_by_id_.erase(message.request_id);
          session->incoming_publish_namespaces_by_namespace_.erase(
              message.track_namespace);
        } else {
          SendRequestOk(message.request_id, MessageParameters());
        }
      });
}

void MoqtSession::ControlStream::OnPublishNamespaceDoneMessage(
    const MoqtPublishNamespaceDone& message) {
  auto it =
      session_->incoming_publish_namespaces_by_id_.find(message.request_id);
  if (it == session_->incoming_publish_namespaces_by_id_.end()) {
    return;
  }
  session_->callbacks_.incoming_publish_namespace_callback(
      it->second, std::nullopt, nullptr);
  session_->incoming_publish_namespaces_by_namespace_.erase(it->second);
  session_->incoming_publish_namespaces_by_id_.erase(it);
}

void MoqtSession::ControlStream::OnPublishNamespaceCancelMessage(
    const MoqtPublishNamespaceCancel& message) {
  auto it = session_->publish_namespace_by_id_.find(message.request_id);
  if (it == session_->publish_namespace_by_id_.end()) {
    return;  // State might have been destroyed due to PUBLISH_NAMESPACE_DONE.
  }
  std::move(it->second.cancel_callback)(MoqtRequestErrorInfo{
      message.error_code, std::nullopt, std::string(message.error_reason)});
  session_->publish_namespace_by_namespace_.erase(it->second.track_namespace);
  session_->publish_namespace_by_id_.erase(it);
}

void MoqtSession::ControlStream::OnTrackStatusMessage(
    const MoqtTrackStatus& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received a TRACK_STATUS_REQUEST after GOAWAY";
    SendRequestError(message.request_id, RequestErrorCode::kUnauthorized,
                     std::nullopt, "TRACK_STATUS_REQUEST after GOAWAY");
    return;
  }
  // TODO(martinduke): Handle authentication.
  std::shared_ptr<MoqtTrackPublisher> track =
      session_->publisher_->GetTrack(message.full_track_name);
  if (track == nullptr) {
    SendRequestError(message.request_id, RequestErrorCode::kDoesNotExist,
                     std::nullopt, "Track does not exist");
    return;
  }
  auto [it, inserted] = session_->incoming_track_status_.emplace(
      message.request_id, std::make_unique<DownstreamTrackStatus>(
                              message.request_id, session_, track.get()));
  track->AddObjectListener(it->second.get());
}

void MoqtSession::ControlStream::OnGoAwayMessage(const MoqtGoAway& message) {
  if (!message.new_session_uri.empty() &&
      perspective() == quic::Perspective::IS_SERVER) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received GOAWAY with new_session_uri on the server");
    return;
  }
  if (session_->received_goaway_) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received multiple GOAWAY messages");
    return;
  }
  session_->received_goaway_ = true;
  if (session_->callbacks_.goaway_received_callback != nullptr) {
    std::move(session_->callbacks_.goaway_received_callback)(
        message.new_session_uri);
  }
}

void MoqtSession::ControlStream::OnMaxRequestIdMessage(
    const MoqtMaxRequestId& message) {
  if (message.max_request_id < session_->peer_max_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Peer sent MAX_REQUEST_ID message with "
                       "lower value than previous";
    session_->Error(MoqtError::kProtocolViolation,
                    "MAX_REQUEST_ID has lower value than previous");
    return;
  }
  session_->peer_max_request_id_ = message.max_request_id;
}

void MoqtSession::ControlStream::OnFetchMessage(const MoqtFetch& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received a FETCH after GOAWAY";
    SendRequestError(message.request_id, RequestErrorCode::kUnauthorized,
                     std::nullopt, "FETCH after GOAWAY");
    return;
  }
  std::unique_ptr<MoqtFetchTask> fetch;
  FullTrackName track_name;
  if (std::holds_alternative<StandaloneFetch>(message.fetch)) {
    const StandaloneFetch& standalone_fetch =
        std::get<StandaloneFetch>(message.fetch);
    track_name = standalone_fetch.full_track_name;
    std::shared_ptr<MoqtTrackPublisher> track_publisher =
        session_->publisher_->GetTrack(track_name);
    if (track_publisher == nullptr) {
      QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                      << " rejected by the application: not found";
      SendRequestError(message.request_id, RequestErrorCode::kDoesNotExist,
                       std::nullopt, "not found");
      return;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Received a StandaloneFETCH for "
                    << track_name;
    // The check for end_object < start_object is done in
    // MoqtTrackPublisher::Fetch().
    fetch = track_publisher->StandaloneFetch(
        standalone_fetch.start_location, standalone_fetch.end_location,
        message.parameters.group_order.value_or(MoqtDeliveryOrder::kAscending));
  } else {
    // Joining Fetch processing.
    uint64_t joining_request_id =
        std::holds_alternative<JoiningFetchRelative>(message.fetch)
            ? std::get<struct JoiningFetchRelative>(message.fetch)
                  .joining_request_id
            : std::get<JoiningFetchAbsolute>(message.fetch).joining_request_id;
    auto it = session_->published_subscriptions_.find(joining_request_id);
    if (it == session_->published_subscriptions_.end()) {
      QUIC_DLOG(INFO) << ENDPOINT << "Received a JOINING_FETCH for "
                      << "request_id " << joining_request_id
                      << " that does not exist";
      SendRequestError(message.request_id,
                       RequestErrorCode::kInvalidJoiningRequestId, std::nullopt,
                       "Joining Fetch for non-existent request");
      return;
    }
    if (!it->second->can_have_joining_fetch()) {
      QUIC_DLOG(INFO) << ENDPOINT << "Received a JOINING_FETCH for "
                      << "joining_request_id " << joining_request_id
                      << " that is not forwarding";
      session_->Error(MoqtError::kProtocolViolation,
                      "Joining Fetch for non-forwarding subscribe");
      return;
    }
    track_name = it->second->publisher().GetTrackName();
    if (it->second->established()) {
      if (!it->second->parameters().largest_object.has_value()) {
        // Nothing to Fetch.
        SendRequestError(message.request_id, RequestErrorCode::kDoesNotExist,
                         std::nullopt, "not found");
        return;
      }
      const Location largest_location =
          *it->second->parameters().largest_object;
      uint64_t start_group;
      if (std::holds_alternative<JoiningFetchRelative>(message.fetch)) {
        const JoiningFetchRelative& relative_fetch =
            std::get<JoiningFetchRelative>(message.fetch);
        start_group =
            (relative_fetch.joining_start > largest_location.group)
                ? 0
                : (largest_location.group - relative_fetch.joining_start);
      } else {
        const JoiningFetchAbsolute& absolute_fetch =
            std::get<JoiningFetchAbsolute>(message.fetch);
        start_group = absolute_fetch.joining_start;
        if (start_group > largest_location.group) {
          SendRequestError(message.request_id, RequestErrorCode::kInvalidRange,
                           std::nullopt, "invalid range");
          return;
        }
      }
      fetch = it->second->publisher().StandaloneFetch(
          Location{start_group, 0}, largest_location,
          message.parameters.group_order.value_or(
              MoqtDeliveryOrder::kAscending));
    } else {
      // Subscription is in PENDING state.
      if (std::holds_alternative<JoiningFetchRelative>(message.fetch)) {
        fetch = it->second->publisher().RelativeFetch(
            std::get<JoiningFetchRelative>(message.fetch).joining_start,
            message.parameters.group_order.value_or(
                MoqtDeliveryOrder::kAscending));
      } else {
        fetch = it->second->publisher().AbsoluteFetch(
            std::get<JoiningFetchAbsolute>(message.fetch).joining_start,
            message.parameters.group_order.value_or(
                MoqtDeliveryOrder::kAscending));
      }
    }
  }
  if (!fetch->GetStatus().ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " could not initialize the task";
    SendRequestError(message.request_id, RequestErrorCode::kInvalidRange,
                     std::nullopt, fetch->GetStatus().message());
    return;
  }
  auto published_fetch = std::make_unique<PublishedFetch>(
      message.request_id, session_, std::move(fetch));
  auto result = session_->incoming_fetches_.emplace(message.request_id,
                                                    std::move(published_fetch));
  if (!result.second) {  // Emplace failed.
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " could not be added to the session";
    SendRequestError(message.request_id, RequestErrorCode::kInternalError,
                     std::nullopt, "Could not initialize FETCH state");
  }
  MoqtFetchTask* fetch_task = result.first->second->fetch_task();
  fetch_task->SetFetchResponseCallback(
      [this, request_id = message.request_id](
          std::variant<MoqtFetchOk, MoqtRequestError> message) {
        if (!session_->incoming_fetches_.contains(request_id)) {
          return;  // FETCH was cancelled.
        }
        if (std::holds_alternative<MoqtFetchOk>(message)) {
          MoqtFetchOk& fetch_ok = std::get<MoqtFetchOk>(message);
          fetch_ok.request_id = request_id;
          SendOrBufferMessage(session_->framer_.SerializeFetchOk(fetch_ok));
          return;
        }
        SendRequestError(request_id,
                         std::get<MoqtRequestError>(message).error_code,
                         std::get<MoqtRequestError>(message).retry_interval,
                         std::get<MoqtRequestError>(message).reason_phrase);
      });
  // Set a temporary new-object callback that creates a data stream. When
  // created, the stream visitor will replace this callback.
  fetch_task->SetObjectAvailableCallback(
      [this,
       send_order =
           SendOrderForFetch(message.parameters.subscriber_priority.value_or(
               kDefaultSubscriberPriority)),
       request_id = message.request_id]() {
        auto it = session_->incoming_fetches_.find(request_id);
        if (it == session_->incoming_fetches_.end()) {
          return;
        }
        if (!session_->session()->CanOpenNextOutgoingUnidirectionalStream() ||
            !session_->OpenDataStream(it->second, send_order)) {
          if (!session_->subscribes_with_queued_outgoing_data_streams_.contains(
                  SubscriptionWithQueuedStream(request_id, send_order))) {
            // Put the FETCH in the queue for a new stream unless it has already
            // done so.
            session_->UpdateQueuedSendOrder(request_id, std::nullopt,
                                            send_order);
          }
        }
      });
}

void MoqtSession::ControlStream::OnFetchOkMessage(const MoqtFetchOk& message) {
  RemoteTrack* track = session_->RemoteTrackById(message.request_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_OK for "
                    << "request_id = " << message.request_id
                    << " but no track exists";
    // Subscription state might have been destroyed for internal reasons.
    return;
  }
  if (!track->is_fetch()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received FETCH_OK for a SUBSCRIBE");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_OK for request_id = "
                  << message.request_id << " " << track->full_track_name();
  UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(track);
  fetch->OnFetchResult(
      message.end_location, absl::OkStatus(),
      [=, session = session_]() { session->CancelFetch(message.request_id); });
}

void MoqtSession::ControlStream::OnRequestsBlockedMessage(
    const MoqtRequestsBlocked& message) {
  // TODO(martinduke): Derive logic for granting more subscribes.
}

void MoqtSession::ControlStream::OnPublishMessage(const MoqtPublish& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  RequestErrorCode error_code = session_->sent_goaway_
                                    ? RequestErrorCode::kUnauthorized
                                    : RequestErrorCode::kNotSupported;
  absl::string_view error_reason = session_->sent_goaway_
                                       ? "Received a PUBLISH after GOAWAY"
                                       : "PUBLISH is not supported";
  SendRequestError(message.request_id, error_code, std::nullopt, error_reason);
}

void MoqtSession::IncomingDataStream::OnObjectMessage(const MoqtObject& message,
                                                      absl::string_view payload,
                                                      bool end_of_message) {
  QUICHE_DVLOG(1) << ENDPOINT << "Received OBJECT message on stream "
                  << stream_->GetStreamId() << " for track alias "
                  << message.track_alias << " with sequence "
                  << message.group_id << ":" << message.object_id
                  << " priority " << message.publisher_priority << " length "
                  << payload.size() << " length " << message.payload_length
                  << (end_of_message ? "F" : "");
  if (!session_->parameters_.deliver_partial_objects) {
    if (!end_of_message) {  // Buffer partial object.
      if (partial_object_.empty()) {
        // Avoid redundant allocations by reserving the appropriate amount of
        // memory if known.
        partial_object_.reserve(message.payload_length);
      }
      absl::StrAppend(&partial_object_, payload);
      return;
    }
    if (!partial_object_.empty()) {  // Completes the object
      absl::StrAppend(&partial_object_, payload);
      payload = absl::string_view(partial_object_);
    }
  }
  if (!parser_.stream_type().has_value()) {
    QUICHE_BUG(quic_bug_object_with_no_stream_type)
        << "Object delivered without a stream type";
    return;
  }
  // Get a pointer to the upstream state.
  RemoteTrack* track = track_.GetIfAvailable();
  if (track == nullptr) {
    track = (parser_.stream_type()->IsFetch())
                // message.track_alias is actually a fetch ID for fetches.
                ? session_->RemoteTrackById(message.track_alias)
                : session_->RemoteTrackByAlias(message.track_alias);
    if (track == nullptr) {
      stream_->SendStopSending(kResetCodeCancelled);
      // Received object for nonexistent track.
      return;
    }
    track_ = track->weak_ptr();
  }
  if (!track->CheckDataStreamType(*parser_.stream_type())) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received object for a track with a different stream type");
    return;
  }
  Location location(message.group_id, message.object_id);
  if (!track->InWindow(Location(message.group_id, message.object_id))) {
    // This is not an error. It can be the result of a recent REQUEST_UPDATE.
    return;
  }
  if (!track->is_fetch()) {
    if (!index_.has_value()) {
      if (!message.subgroup_id.has_value()) {
        QUICHE_BUG(quiche_bug_moqt_subgroup_id_missing)
            << "Missing subgroup ID on SUBSCRIBE stream";
        return;
      }
      index_ = DataStreamIndex(message.group_id, *message.subgroup_id);
    }
    if (no_more_objects_) {
      // Already got a stream-ending object. While the lower layer won't
      // deliver data after the FIN, there could have been an EndOfGroup or
      // EndOfTrack signal.
      session_->OnMalformedTrack(track);
      return;
    }
    if (end_of_message) {
      next_object_id_ = message.object_id + 1;
      if (message.object_status == MoqtObjectStatus::kEndOfTrack ||
          message.object_status == MoqtObjectStatus::kEndOfGroup) {
        no_more_objects_ = true;
      }
    }
    SubscribeRemoteTrack* subscribe =
        absl::down_cast<SubscribeRemoteTrack*>(track);
    subscribe->OnObjectOrOk();
    if (subscribe->visitor() != nullptr) {
      PublishedObjectMetadata metadata;
      metadata.location = Location(message.group_id, message.object_id);
      metadata.subgroup = message.subgroup_id;
      metadata.extensions = message.extension_headers;
      metadata.status = message.object_status;
      metadata.publisher_priority = message.publisher_priority;
      metadata.arrival_time = session_->callbacks_.clock->Now();
      subscribe->visitor()->OnObjectFragment(track->full_track_name(), metadata,
                                             payload, end_of_message);
    }
  } else {  // FETCH
    track->OnObjectOrOk();
    UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(track);
    if (!fetch->LocationIsValid(Location(message.group_id, message.object_id),
                                message.object_status, end_of_message)) {
      // TODO(martinduke): in https://github.com/moq-wg/moq-transport/pull/1409
      // I make the case that this should be a protocol violation. Update if
      // that proposal is accepted (at which point
      // QuicSession::OnMalformedTrack can be removed, since all the
      // remaining conditions are at the application layer).
      session_->OnMalformedTrack(track);
      return;
    }
    UpstreamFetch::UpstreamFetchTask* task = fetch->task();
    if (task == nullptr) {
      // The application killed the FETCH.
      stream_->SendStopSending(kResetCodeCancelled);
      return;
    }
    if (!task->HasObject()) {
      task->NewObject(message);
    }
    if (task->NeedsMorePayload() && !payload.empty()) {
      task->AppendPayloadToObject(payload);
    }
  }
  partial_object_.clear();
}

MoqtSession::IncomingDataStream::~IncomingDataStream() {
  QUICHE_DVLOG(1) << ENDPOINT << "Destroying incoming data stream "
                  << stream_->GetStreamId();
  if (!parser_.track_alias().has_value()) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Destroying incoming data stream before "
                     "learning track alias";
    return;
  }
  if (!track_.IsValid()) {
    return;
  }
  if (parser_.stream_type().has_value() && parser_.stream_type()->IsFetch()) {
    session_->upstream_by_id_.erase(*parser_.track_alias());
    return;
  }
  if (session_->is_closing_) {
    return;
  }
  // It's a subscribe.
  SubscribeRemoteTrack* subscribe =
      absl::down_cast<SubscribeRemoteTrack*>(track_.GetIfAvailable());
  if (subscribe == nullptr) {
    return;
  }
  subscribe->OnStreamClosed(fin_received_, index_);
  session_->MaybeDestroySubscription(subscribe);
}

void MoqtSession::IncomingDataStream::MaybeReadOneObject() {
  if (!parser_.track_alias().has_value() ||
      !parser_.stream_type().has_value() || !parser_.stream_type()->IsFetch()) {
    QUICHE_BUG(quic_bug_read_one_object_parser_unexpected_state)
        << "Requesting object, parser in unexpected state";
  }
  RemoteTrack* track = session_->RemoteTrackById(*parser_.track_alias());
  if (track == nullptr || !track->is_fetch()) {
    QUICHE_BUG(quic_bug_read_one_object_track_unexpected_state)
        << "Requesting object, track in unexpected state";
    return;
  }
  UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(track);
  UpstreamFetch::UpstreamFetchTask* task = fetch->task();
  if (task == nullptr) {
    return;
  }
  if (task->HasObject() && !task->NeedsMorePayload()) {
    return;
  }
  parser_.ReadAtMostOneObject();
  // If it read an object, it called OnObjectMessage and may have altered the
  // task's object state.
  if (task->HasObject() && !task->NeedsMorePayload()) {
    task->NotifyNewObject();
  }
}

void MoqtSession::IncomingDataStream::OnCanRead() {
  if (!parser_.stream_type().has_value()) {
    parser_.ReadStreamType();
    if (!parser_.stream_type().has_value()) {
      return;
    }
  }
  if (parser_.stream_type()->IsPadding()) {
    (void)stream_->SkipBytes(stream_->ReadableBytes());
    return;
  }
  bool knew_track_alias = parser_.track_alias().has_value();
  if (!knew_track_alias) {
    parser_.ReadTrackAlias();
    if (!parser_.track_alias().has_value()) {
      return;
    }
  }
  QUICHE_CHECK(parser_.stream_type().has_value());
  QUICHE_CHECK(parser_.track_alias().has_value());
  if (parser_.stream_type()->IsSubgroup()) {
    if (!knew_track_alias) {
      // This is a new stream for a subscribe. Notify the subscription.
      auto it = session_->subscribe_by_alias_.find(*parser_.track_alias());
      if (it == session_->subscribe_by_alias_.end()) {
        QUIC_DLOG(INFO) << ENDPOINT
                        << "Received object for a track with no SUBSCRIBE";
        // This is a not a session error because there might be an UNSUBSCRIBE
        // or SUBSCRIBE_OK (containing the track alias) in flight.
        stream_->SendStopSending(kResetCodeCancelled);
        return;
      }
      it->second->OnStreamOpened();
      parser_.set_default_publisher_priority(
          it->second->default_publisher_priority());
    }
    parser_.ReadAllData();
    return;
  }
  auto it = session_->upstream_by_id_.find(*parser_.track_alias());
  if (it == session_->upstream_by_id_.end()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received object for a track with no FETCH";
    // This is a not a session error because there might be an UNSUBSCRIBE in
    // flight.
    stream_->SendStopSending(kResetCodeCancelled);
    return;
  }
  if (it->second == nullptr) {
    QUICHE_BUG(quiche_bug_moqt_fetch_pointer_is_null)
        << "Fetch pointer is null";
    return;
  }
  UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(it->second.get());
  if (!knew_track_alias) {
    // If the task already exists (FETCH_OK has arrived), the callback will
    // immediately execute to read the first object. Otherwise, it will only
    // execute when the task is created or a cached object is read.
    fetch->OnStreamOpened([this]() { MaybeReadOneObject(); });
    return;
  }
  MaybeReadOneObject();
}

void MoqtSession::IncomingDataStream::OnControlMessageReceived() {
  session_->Error(MoqtError::kProtocolViolation,
                  "Received a control message on a data stream");
}

void MoqtSession::IncomingDataStream::OnParsingError(MoqtError error_code,
                                                     absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

MoqtSession::PublishedSubscription::PublishedSubscription(
    MoqtSession* session, std::shared_ptr<MoqtTrackPublisher> track_publisher,
    const MoqtSubscribe& subscribe, uint64_t track_alias,
    MoqtPublishingMonitorInterface* monitoring_interface)
    : session_(session),
      track_publisher_(track_publisher),
      request_id_(subscribe.request_id),
      can_have_joining_fetch_(subscribe.parameters.forward()),
      track_alias_(track_alias),
      parameters_(subscribe.parameters),
      monitoring_interface_(monitoring_interface) {
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckSupportKnown(
        subscribe.parameters.oack_window_size);
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << subscribe.full_track_name;
  session_->subscribed_track_names_.insert(subscribe.full_track_name);
  // TODO(martinduke): Handle NEW_GROUP_REQUEST
}

MoqtSession::PublishedSubscription::~PublishedSubscription() {
  track_publisher_->RemoveObjectListener(this);
  if (session_->is_closing_) {
    return;
  }
  session_->subscribed_track_names_.erase(track_publisher_->GetTrackName());
  // Reset all streams.
  for (const webtransport::StreamId stream_id : stream_map().GetAllStreams()) {
    webtransport::Stream* stream = session_->session_->GetStreamById(stream_id);
    if (stream != nullptr) {
      stream->ResetWithUserCode(kResetCodeCancelled);
    }
  }
}

SendStreamMap& MoqtSession::PublishedSubscription::stream_map() {
  // The stream map is lazily initialized, since initializing it requires
  // knowing the forwarding preference in advance, and it might not be known
  // when the subscription is first created.
  if (!lazily_initialized_stream_map_.has_value()) {
    lazily_initialized_stream_map_.emplace();
  }
  return *lazily_initialized_stream_map_;
}

void MoqtSession::PublishedSubscription::Update(
    const MessageParameters& parameters) {
  // TODO(martinduke): If there are auth tokens, this probably has to go to the
  // application.
  parameters_.Update(parameters);
  can_have_joining_fetch_ = parameters_.forward();
}

void MoqtSession::PublishedSubscription::set_subscriber_priority(
    MoqtPriority priority) {
  if (priority ==
      parameters_.subscriber_priority.value_or(kDefaultSubscriberPriority)) {
    return;
  }
  if (queued_outgoing_data_streams_.empty()) {
    parameters_.subscriber_priority = priority;
    return;
  }
  webtransport::SendOrder old_send_order =
      FinalizeSendOrder(queued_outgoing_data_streams_.rbegin()->first);
  parameters_.subscriber_priority = priority;
  session_->UpdateQueuedSendOrder(request_id_, old_send_order,
                                  FinalizeSendOrder(old_send_order));
};

void MoqtSession::PublishedSubscription::OnSubscribeAccepted() {
  ControlStream* stream = session_->GetControlStream();
  QUICHE_DCHECK(!established_);
  established_ = true;
  parameters_.largest_object = track_publisher_->largest_location();
  if (parameters_.subscription_filter.has_value()) {
    parameters_.subscription_filter->OnLargestObject(
        parameters_.largest_object);
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.request_id = request_id_;
  subscribe_ok.track_alias = track_alias_;
  subscribe_ok.parameters.expires = track_publisher_->expiration();
  subscribe_ok.parameters.largest_object = parameters_.largest_object;
  subscribe_ok.extensions = track_publisher_->extensions();
  if (!parameters_.group_order.has_value()) {
    parameters_.group_order =
        subscribe_ok.extensions.default_publisher_group_order();
  }
  // TODO(martinduke): Support sending DELIVERY_TIMEOUT parameter as the
  // publisher.
  default_publisher_priority_ =
      subscribe_ok.extensions.default_publisher_priority();
  stream->SendOrBufferMessage(
      session_->framer_.SerializeSubscribeOk(subscribe_ok));
  // TODO(martinduke): If we buffer objects that arrived previously, the arrival
  // of the track alias disambiguates what subscription they belong to. Send
  // them.";
}

void MoqtSession::PublishedSubscription::OnSubscribeRejected(
    MoqtRequestErrorInfo info) {
  session_->GetControlStream()->SendRequestError(request_id_, info);
  session_->published_subscriptions_.erase(request_id_);
  // No class access below this line!
}

void MoqtSession::PublishedSubscription::OnNewObjectAvailable(
    Location location, std::optional<uint64_t> subgroup,
    MoqtPriority publisher_priority) {
  if (!InWindow(location)) {
    return;
  }

  if (monitoring_interface_ != nullptr) {
    // Notify the monitoring interface about all newly published normal objects.
    // Objects with other statuses are not guaranteed to be acknowledged, thus
    // passing them into the monitoring interface can lead to confusion.
    std::optional<PublishedObject> object = track_publisher_->GetCachedObject(
        location.group, subgroup, location.object);
    QUICHE_DCHECK(object.has_value())
        << "Object " << absl::StrCat(location) << " on track "
        << track_publisher_->GetTrackName().ToString()
        << " does not exist, despite OnNewObjectAvailable being called";
    if (object.has_value() && object->metadata.location == location &&
        object->metadata.status == MoqtObjectStatus::kNormal) {
      monitoring_interface_->OnNewObjectEnqueued(location);
    }
  }

  // TODO(vasilvv): This currently sends UINT64_MAX for datagram subgroups.
  // Maybe do something more satisfactory?
  session_->trace_recorder_.RecordNewObjectAvaliable(
      track_alias_, *track_publisher_, location, subgroup.value_or(UINT64_MAX),
      publisher_priority);

  std::optional<webtransport::StreamId> stream_id;
  if (subgroup.has_value()) {
    DataStreamIndex index(location.group, *subgroup);
    if (reset_subgroups_.contains(index)) {
      // This subgroup has already been reset, ignore.
      return;
    }
    stream_id = stream_map().GetStreamFor(index);
  }
  if (session_->alternate_delivery_timeout_ &&
      !delivery_timeout().IsInfinite() && largest_sent_.has_value() &&
      location.group >= largest_sent_->group) {
    // Start the delivery timeout timer on all previous groups.
    for (uint64_t group = first_active_group_; group < location.group;
         ++group) {
      for (webtransport::StreamId stream_to_update :
           stream_map().GetStreamsForGroup(group)) {
        webtransport::Stream* raw_stream =
            session_->session_->GetStreamById(stream_to_update);
        if (raw_stream == nullptr) {
          continue;
        }
        OutgoingDataStream* stream =
            absl::down_cast<OutgoingDataStream*>(raw_stream->visitor());
        stream->CreateAndSetAlarm(session_->callbacks_.clock->ApproximateNow() +
                                  delivery_timeout());
      }
    }
  }
  QUICHE_DCHECK_GE(location.group, first_active_group_);
  if (!subgroup.has_value()) {
    SendDatagram(location);
    return;
  }

  webtransport::Stream* raw_stream = nullptr;
  if (stream_id.has_value()) {
    raw_stream = session_->session_->GetStreamById(*stream_id);
  } else {
    raw_stream = session_->OpenOrQueueDataStream(
        request_id_,
        NewStreamParameters(location.group, *subgroup, location.object,
                            (publisher_priority == default_publisher_priority_)
                                ? std::nullopt
                                : std::make_optional(publisher_priority)));
  }
  if (raw_stream == nullptr) {
    return;
  }

  OutgoingDataStream* stream =
      absl::down_cast<OutgoingDataStream*>(raw_stream->visitor());
  stream->SendObjects(*this);
}

void MoqtSession::PublishedSubscription::OnTrackPublisherGone() {
  session_->PublishIsDone(request_id_, PublishDoneCode::kGoingAway,
                          "Publisher is gone");
}

// TODO(martinduke): Revise to check if the last object has been delivered.
void MoqtSession::PublishedSubscription::OnNewFinAvailable(Location location,
                                                           uint64_t subgroup) {
  if (!InWindow(location.group)) {
    return;
  }
  DataStreamIndex index(location.group, subgroup);
  if (reset_subgroups_.contains(index)) {
    // This subgroup has already been reset, ignore.
    return;
  }
  QUICHE_DCHECK_GE(location.group, first_active_group_);
  std::optional<webtransport::StreamId> stream_id =
      stream_map().GetStreamFor(index);
  if (!stream_id.has_value()) {
    return;
  }
  webtransport::Stream* raw_stream =
      session_->session_->GetStreamById(*stream_id);
  if (raw_stream == nullptr) {
    return;
  }
  OutgoingDataStream* stream =
      absl::down_cast<OutgoingDataStream*>(raw_stream->visitor());
  stream->Fin(location);
}

void MoqtSession::PublishedSubscription::OnSubgroupAbandoned(
    uint64_t group, uint64_t subgroup,
    webtransport::StreamErrorCode error_code) {
  if (session_->is_closing_) {
    return;
  }
  if (!InWindow(group)) {
    return;
  }
  DataStreamIndex index(group, subgroup);
  if (reset_subgroups_.contains(index)) {
    // This subgroup has already been reset, ignore.
    return;
  }
  reset_subgroups_.insert(index);
  QUICHE_DCHECK_GE(group, first_active_group_);
  std::optional<webtransport::StreamId> stream_id =
      stream_map().GetStreamFor(index);
  if (!stream_id.has_value()) {
    return;
  }
  webtransport::Stream* raw_stream =
      session_->session_->GetStreamById(*stream_id);
  if (raw_stream == nullptr) {
    return;
  }
  raw_stream->ResetWithUserCode(error_code);
}

void MoqtSession::PublishedSubscription::OnGroupAbandoned(uint64_t group_id) {
  if (session_->is_closing_) {
    return;
  }
  if (!InWindow(group_id)) {
    // The group is not in the window, ignore.
    return;
  }
  std::vector<webtransport::StreamId> streams =
      stream_map().GetStreamsForGroup(group_id);
  if (delivery_timeout().IsInfinite() && largest_sent_.has_value() &&
      largest_sent_->group <= group_id) {
    session_->PublishIsDone(request_id_, PublishDoneCode::kTooFarBehind, "");
    // No class access below this line!
    return;
  }
  for (webtransport::StreamId stream_id : streams) {
    webtransport::Stream* raw_stream =
        session_->session_->GetStreamById(stream_id);
    if (raw_stream == nullptr) {
      continue;
    }
    raw_stream->ResetWithUserCode(kResetCodeDeliveryTimeout);
    // Sending the Reset will call the destructor for OutgoingDataStream, which
    // will erase it from the SendStreamMap.
  }
  first_active_group_ = std::max(first_active_group_, group_id + 1);
  absl::erase_if(reset_subgroups_, [&](const DataStreamIndex& index) {
    return index.group < first_active_group_;
  });
}

std::vector<webtransport::StreamId>
MoqtSession::PublishedSubscription::GetAllStreams() const {
  if (!lazily_initialized_stream_map_.has_value()) {
    return {};
  }
  return lazily_initialized_stream_map_->GetAllStreams();
}

webtransport::SendOrder MoqtSession::PublishedSubscription::GetSendOrder(
    Location sequence, std::optional<uint64_t> subgroup,
    MoqtPriority publisher_priority) const {
  if (!parameters_.group_order.has_value()) {
    QUICHE_BUG(GetSendOrder_no_delivery_order)
        << "Can't compute send order without a group order.";
    return 0;
  }
  if (!subgroup.has_value()) {
    return SendOrderForDatagram(
        parameters_.subscriber_priority.value_or(kDefaultSubscriberPriority),
        publisher_priority, sequence.group, sequence.object,
        *parameters_.group_order);
  }
  return SendOrderForStream(
      parameters_.subscriber_priority.value_or(kDefaultSubscriberPriority),
      publisher_priority, sequence.group, *subgroup, *parameters_.group_order);
}

// Returns the highest send order in the subscription.
void MoqtSession::PublishedSubscription::AddQueuedOutgoingDataStream(
    const NewStreamParameters& parameters) {
  std::optional<webtransport::SendOrder> start_send_order =
      queued_outgoing_data_streams_.empty()
          ? std::optional<webtransport::SendOrder>()
          : queued_outgoing_data_streams_.rbegin()->first;
  webtransport::SendOrder send_order = GetSendOrder(
      Location(parameters.index.group, parameters.first_object),
      parameters.index.subgroup,
      parameters.publisher_priority.value_or(default_publisher_priority()));
  // Zero out the subscriber priority bits, since these will be added when
  // updating the session.
  queued_outgoing_data_streams_.emplace(
      UpdateSendOrderForSubscriberPriority(send_order, 0), parameters);
  if (!start_send_order.has_value()) {
    session_->UpdateQueuedSendOrder(request_id_, std::nullopt, send_order);
  } else if (*start_send_order < send_order) {
    session_->UpdateQueuedSendOrder(
        request_id_, FinalizeSendOrder(*start_send_order), send_order);
  }
}

MoqtSession::NewStreamParameters
MoqtSession::PublishedSubscription::NextQueuedOutgoingDataStream() {
  QUICHE_DCHECK(!queued_outgoing_data_streams_.empty());
  if (queued_outgoing_data_streams_.empty()) {
    QUICHE_BUG(NextQueuedOutgoingDataStream_no_stream)
        << "NextQueuedOutgoingDataStream called when there are no streams "
           "pending.";
    return NewStreamParameters(0, 0, 0, 0);
  }
  auto it = queued_outgoing_data_streams_.rbegin();
  webtransport::SendOrder old_send_order = FinalizeSendOrder(it->first);
  NewStreamParameters first_stream = it->second;
  // converting a reverse iterator to an iterator involves incrementing it and
  // then taking base().
  queued_outgoing_data_streams_.erase((++it).base());
  if (queued_outgoing_data_streams_.empty()) {
    session_->UpdateQueuedSendOrder(request_id_, old_send_order, std::nullopt);
  } else {
    webtransport::SendOrder new_send_order =
        FinalizeSendOrder(queued_outgoing_data_streams_.rbegin()->first);
    if (old_send_order != new_send_order) {
      session_->UpdateQueuedSendOrder(request_id_, old_send_order,
                                      new_send_order);
    }
  }
  return first_stream;
}

void MoqtSession::PublishedSubscription::OnDataStreamCreated(
    webtransport::StreamId id, DataStreamIndex start_sequence) {
  ++streams_opened_;
  stream_map().AddStream(start_sequence, id);
}
void MoqtSession::PublishedSubscription::OnDataStreamDestroyed(
    webtransport::StreamId id, DataStreamIndex end_sequence) {
  stream_map().RemoveStream(end_sequence);
}

void MoqtSession::PublishedSubscription::OnObjectSent(Location sequence) {
  if (largest_sent_.has_value()) {
    largest_sent_ = std::max(*largest_sent_, sequence);
  } else {
    largest_sent_ = sequence;
  }
  // TODO: send PUBLISH_DONE if the subscription is done.
}

MoqtSession::OutgoingDataStream::OutgoingDataStream(
    MoqtSession* session, webtransport::Stream* stream,
    PublishedSubscription& subscription, const NewStreamParameters& parameters)
    : session_(session),
      stream_(stream),
      subscription_id_(subscription.request_id()),
      index_(parameters.index),
      publisher_priority_(parameters.publisher_priority.value_or(
          subscription.default_publisher_priority())),
      // Always include extension header length, because it's difficult to know
      // a priori if they're going to appear on a stream.
      stream_type_(MoqtDataStreamType::Subgroup(
          index_.subgroup, parameters.first_object, false,
          !parameters.publisher_priority.has_value())),
      next_object_(parameters.first_object),
      session_liveness_(session->liveness_token_) {
  UpdateSendOrder(subscription);
  session->trace_recorder_.RecordSubgroupStreamCreated(
      stream->GetStreamId(), subscription.track_alias(), parameters.index);
}

MoqtSession::OutgoingDataStream::~OutgoingDataStream() {
  // Though it might seem intuitive that the session object has to outlive the
  // connection object (and this is indeed how something like QuicSession and
  // QuicStream works), this is not the true for WebTransport visitors: the
  // session getting destroyed will inevitably lead to all related streams being
  // destroyed, but the actual order of destruction is not guaranteed.  Thus, we
  // need to check if the session still exists while accessing it in a stream
  // destructor.
  if (session_liveness_.expired()) {
    return;
  }
  if (delivery_timeout_alarm_ != nullptr) {
    delivery_timeout_alarm_->PermanentCancel();
  }
  auto it = session_->published_subscriptions_.find(subscription_id_);
  if (it != session_->published_subscriptions_.end()) {
    it->second->OnDataStreamDestroyed(stream_->GetStreamId(), index_);
  }
}

void MoqtSession::OutgoingDataStream::OnCanWrite() {
  PublishedSubscription* subscription = GetSubscriptionIfValid();
  if (subscription == nullptr) {
    return;
  }
  SendObjects(*subscription);
}

void MoqtSession::OutgoingDataStream::OnStopSendingReceived(
    webtransport::StreamErrorCode error_code) {
  PublishedSubscription* subscription = GetSubscriptionIfValid();
  if (subscription == nullptr) {
    return;
  }
  subscription->OnSubgroupAbandoned(index_.group, index_.subgroup, error_code);
}

void MoqtSession::OutgoingDataStream::DeliveryTimeoutDelegate::OnAlarm() {
  auto it = stream_->session_->published_subscriptions_.find(
      stream_->subscription_id_);
  if (it != stream_->session_->published_subscriptions_.end()) {
    it->second->OnStreamTimeout(stream_->index());
  }
  stream_->stream_->ResetWithUserCode(kResetCodeDeliveryTimeout);
}

MoqtSession::PublishedSubscription*
MoqtSession::OutgoingDataStream::GetSubscriptionIfValid() {
  auto it = session_->published_subscriptions_.find(subscription_id_);
  if (it == session_->published_subscriptions_.end()) {
    stream_->ResetWithUserCode(kResetCodeCancelled);
    return nullptr;
  }

  PublishedSubscription* subscription = it->second.get();
  if (!subscription->publisher().largest_location().has_value()) {
    QUICHE_BUG(GetSubscriptionIfValid_InvalidTrackStatusOk)
        << "The track publisher returned a status indicating that no objects "
           "are available, but a stream for those objects exists.";
    session_->Error(MoqtError::kInternalError,
                    "Invalid track state provided by application");
    return nullptr;
  }
  return subscription;
}

void MoqtSession::OutgoingDataStream::SendObjects(
    PublishedSubscription& subscription) {
  while (stream_->CanWrite()) {
    std::optional<PublishedObject> object =
        subscription.publisher().GetCachedObject(index_.group, index_.subgroup,
                                                 next_object_);
    if (!object.has_value()) {
      break;
    }

    QUICHE_DCHECK_EQ(object->metadata.location.group, index_.group);
    QUICHE_DCHECK(object->metadata.subgroup == index_.subgroup);
    if (!subscription.InWindow(object->metadata.location)) {
      // It is possible that the next object became irrelevant due to a
      // REQUEST_UPDATE.  Close the stream if so.
      bool success = stream_->SendFin();
      QUICHE_BUG_IF(OutgoingDataStream_fin_due_to_update, !success)
          << "Writing FIN failed despite CanWrite() being true.";
      return;
    }

    quic::QuicTimeDelta delivery_timeout = subscription.delivery_timeout();
    if (!session_->alternate_delivery_timeout_ &&
        session_->callbacks_.clock->ApproximateNow() -
                object->metadata.arrival_time >
            delivery_timeout) {
      subscription.OnStreamTimeout(index_);
      stream_->ResetWithUserCode(kResetCodeDeliveryTimeout);
      return;
    }
    if (!session_->WriteObjectToStream(stream_, subscription.track_alias(),
                                       object->metadata,
                                       std::move(object->payload), stream_type_,
                                       last_object_, object->fin_after_this)) {
      // WriteObjectToStream() closes the connection on error, meaning that
      // there is no need to process the stream any further.
      return;
    }
    last_object_ = object->metadata;

    next_object_ = last_object_->location.object + 1;
    subscription.OnObjectSent(object->metadata.location);

    if (object->fin_after_this && !delivery_timeout.IsInfinite() &&
        !session_->alternate_delivery_timeout_) {
      CreateAndSetAlarm(object->metadata.arrival_time + delivery_timeout);
    }
  }
}

void MoqtSession::OutgoingDataStream::Fin(Location last_object) {
  QUICHE_DCHECK_EQ(last_object.group, index_.group);
  if (next_object_ <= last_object.object) {
    // There is still data to send, do nothing.
    return;
  }
  // All data has already been sent; send a pure FIN.
  bool success = stream_->SendFin();
  QUICHE_BUG_IF(OutgoingDataStream_fin_failed, !success)
      << "Writing pure FIN failed.";
  auto it = session_->published_subscriptions_.find(subscription_id_);
  if (it == session_->published_subscriptions_.end()) {
    return;
  }
  quic::QuicTimeDelta delivery_timeout = it->second->delivery_timeout();
  if (!delivery_timeout.IsInfinite()) {
    CreateAndSetAlarm(session_->callbacks_.clock->ApproximateNow() +
                      delivery_timeout);
  }
}

bool MoqtSession::WriteObjectToStream(
    webtransport::Stream* stream, uint64_t id,
    const PublishedObjectMetadata& metadata, quiche::QuicheMemSlice payload,
    MoqtDataStreamType type, std::optional<PublishedObjectMetadata> last_object,
    bool fin) {
  QUICHE_DCHECK(stream->CanWrite());
  MoqtObject header;
  header.track_alias = id;
  header.group_id = metadata.location.group;
  header.subgroup_id = metadata.subgroup;
  header.object_id = metadata.location.object;
  header.publisher_priority = metadata.publisher_priority;
  header.extension_headers = metadata.extensions;
  header.object_status = metadata.status;
  header.payload_length = payload.length();

  quiche::QuicheBuffer serialized_header =
      framer_.SerializeObjectHeader(header, type, last_object);
  // TODO(vasilvv): add a version of WebTransport write API that accepts
  // memslices so that we can avoid a copy here.
  std::array write_vector = {
      quiche::QuicheMemSlice(std::move(serialized_header)), std::move(payload)};
  webtransport::StreamWriteOptions options;
  options.set_send_fin(fin);
  absl::Status write_status =
      stream->Writev(absl::MakeSpan(write_vector), options);
  if (!write_status.ok()) {
    QUICHE_BUG(MoqtSession_WriteObjectToStream_write_failed)
        << "Writing into MoQT stream failed despite CanWrite() being true "
           "before; status: "
        << write_status;
    Error(MoqtError::kInternalError, "Data stream write error");
    return false;
  }

  QUIC_DVLOG(1) << "Stream " << stream->GetStreamId() << " successfully wrote "
                << metadata.location << ", fin = " << fin;
  return true;
}

void MoqtSession::OnMalformedTrack(RemoteTrack* track) {
  if (!track->is_fetch()) {
    absl::down_cast<SubscribeRemoteTrack*>(track)->visitor()->OnMalformedTrack(
        track->full_track_name());
    Unsubscribe(track->full_track_name());
    return;
  }
  UpstreamFetch::UpstreamFetchTask* task =
      absl::down_cast<UpstreamFetch*>(track)->task();
  if (task != nullptr) {
    task->OnStreamAndFetchClosed(kResetCodeMalformedTrack,
                                 "Malformed track received");
  }
  CancelFetch(track->request_id());
}

void MoqtSession::CleanUpState() {
  if (is_closing_) {
    return;
  }
  is_closing_ = true;
  if (goaway_timeout_alarm_ != nullptr) {
    goaway_timeout_alarm_->PermanentCancel();
  }
  // Incoming SUBSCRIBE_NAMESPACE is automatically cleaned up; the destroyed
  // session owns the webtransport stream, which owns the StreamVisitor, which
  // owns the task. Destroying the task notifies the application.
  published_subscriptions_.clear();
  for (auto& it : incoming_publish_namespaces_by_namespace_) {
    callbacks_.incoming_publish_namespace_callback(it.first, std::nullopt,
                                                   nullptr);
  }
  for (auto& it : publish_namespace_by_id_) {
    std::move(it.second.cancel_callback)(MoqtRequestErrorInfo{
        RequestErrorCode::kUninterested, std::nullopt, "Session closed"});
  }
  while (!upstream_by_id_.empty()) {
    auto upstream = upstream_by_id_.begin();
    if (upstream->second->is_fetch()) {
      upstream_by_id_.erase(upstream);
      continue;
    }
    DestroySubscription(
        absl::down_cast<SubscribeRemoteTrack*>(upstream->second.get()));
  }
}

void MoqtSession::CancelFetch(uint64_t request_id) {
  if (is_closing_) {
    return;
  }
  // This is only called from the callback where UpstreamFetchTask has been
  // destroyed, so there is no need to notify the application.
  upstream_by_id_.erase(request_id);
  ControlStream* stream = GetControlStream();
  if (stream == nullptr) {
    return;
  }
  MoqtFetchCancel message;
  message.request_id = request_id;
  stream->SendOrBufferMessage(framer_.SerializeFetchCancel(message));
  // The FETCH_CANCEL will cause a RESET_STREAM to return, which would be the
  // same as a STOP_SENDING. However, a FETCH_CANCEL works even if the stream
  // hasn't opened yet.
}

void MoqtSession::PublishedSubscription::SendDatagram(Location sequence) {
  std::optional<PublishedObject> object = track_publisher_->GetCachedObject(
      sequence.group, std::nullopt, sequence.object);
  if (!object.has_value()) {
    QUICHE_BUG(PublishedSubscription_SendDatagram_object_not_in_cache)
        << "Got notification about an object that is not in the cache";
    return;
  }
  MoqtObject header;
  header.track_alias = track_alias_;
  header.group_id = object->metadata.location.group;
  header.object_id = object->metadata.location.object;
  header.publisher_priority = object->metadata.publisher_priority;
  header.extension_headers = object->metadata.extensions;
  header.object_status = object->metadata.status;
  header.subgroup_id = std::nullopt;
  header.payload_length = object->payload.length();
  quiche::QuicheBuffer datagram = session_->framer_.SerializeObjectDatagram(
      header, object->payload.AsStringView(),
      default_publisher_priority_.value_or(kDefaultPublisherPriority));
  session_->session_->SendOrQueueDatagram(datagram.AsStringView());
  OnObjectSent(object->metadata.location);
}

void MoqtSession::OutgoingDataStream::UpdateSendOrder(
    PublishedSubscription& subscription) {
  stream_->SetPriority(webtransport::StreamPriority{
      /*send_group_id=*/kMoqtSendGroupId,
      subscription.GetSendOrder(Location(index_.group, next_object_),
                                index_.subgroup, publisher_priority_)});
}

void MoqtSession::OutgoingDataStream::CreateAndSetAlarm(
    quic::QuicTime deadline) {
  if (delivery_timeout_alarm_ != nullptr) {
    return;
  }
  delivery_timeout_alarm_ = absl::WrapUnique(
      session_->alarm_factory_->CreateAlarm(new DeliveryTimeoutDelegate(this)));
  delivery_timeout_alarm_->Set(deadline);
}

MoqtSession::PublishedFetch::FetchStreamVisitor::FetchStreamVisitor(
    std::shared_ptr<PublishedFetch> fetch, webtransport::Stream* stream)
    : fetch_(fetch), stream_(stream) {
  fetch->fetch_task()->SetObjectAvailableCallback(
      [this]() { this->OnCanWrite(); });
  fetch->session()->trace_recorder_.RecordFetchStreamCreated(
      stream->GetStreamId());
}

void MoqtSession::PublishedSubscription::ProcessObjectAck(
    const MoqtObjectAck& message) {
  session_->trace_recorder_.RecordObjectAck(
      track_alias_, Location(message.group_id, message.object_id),
      message.delta_from_deadline);

  if (monitoring_interface_ == nullptr) {
    return;
  }
  monitoring_interface_->OnObjectAckReceived(
      Location(message.group_id, message.object_id),
      message.delta_from_deadline);
}

void MoqtSessionParameters::ToSetupParameters(SetupParameters& out) const {
  if (perspective == quic::Perspective::IS_CLIENT && !using_webtrans) {
    out.path = path;
    out.authority = authority;
  }
  if (max_request_id != kDefaultMaxRequestId) {
    out.max_request_id = max_request_id;
  }
  if (max_auth_token_cache_size != kDefaultMaxAuthTokenCacheSize) {
    out.max_auth_token_cache_size = max_auth_token_cache_size;
  }
  if (support_object_acks != kDefaultSupportObjectAcks) {
    out.support_object_acks = support_object_acks;
  }
  if (!moqt_implementation.empty()) {
    out.moqt_implementation = moqt_implementation;
  }
  for (const AuthToken& token : authorization_token) {
    out.authorization_tokens.push_back(token);
  }
}

}  // namespace moqt
