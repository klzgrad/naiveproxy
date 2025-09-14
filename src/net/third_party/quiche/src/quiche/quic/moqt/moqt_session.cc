// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>


#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
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

bool PublisherHasData(const MoqtTrackPublisher& publisher) {
  absl::StatusOr<MoqtTrackStatusCode> status = publisher.GetTrackStatus();
  return status.ok() && DoesTrackStatusImplyHavingData(*status);
}

std::optional<SubscribeWindow> SubscribeMessageToWindow(
    const MoqtSubscribe& subscribe) {
  if (!subscribe.forward ||
      subscribe.filter_type == MoqtFilterType::kLatestObject ||
      subscribe.filter_type == MoqtFilterType::kNextGroupStart) {
    return std::nullopt;
  }
  if (!subscribe.start.has_value()) {
    return std::nullopt;
  }
  return SubscribeWindow(*subscribe.start, subscribe.end_group);
}

class DefaultPublisher : public MoqtPublisher {
 public:
  static DefaultPublisher* GetInstance() {
    static DefaultPublisher* instance = new DefaultPublisher();
    return instance;
  }

  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> GetTrack(
      const FullTrackName& track_name) override {
    QUICHE_DCHECK(track_name.IsValid());
    return absl::NotFoundError("No tracks published");
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
      framer_(quiche::SimpleBufferAllocator::Get(), parameters.using_webtrans),
      publisher_(DefaultPublisher::GetInstance()),
      local_max_request_id_(parameters.max_request_id),
      alarm_factory_(std::move(alarm_factory)),
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
  } else {
    next_incoming_request_id_ = 1;
  }
}

MoqtSession::ControlStream* MoqtSession::GetControlStream() {
  if (!control_stream_.has_value()) {
    return nullptr;
  }
  webtransport::Stream* raw_stream = session_->GetStreamById(*control_stream_);
  if (raw_stream == nullptr) {
    return nullptr;
  }
  return static_cast<ControlStream*>(raw_stream->visitor());
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
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error(MoqtError::kInternalError, "Unable to open a control stream");
    return;
  }
  control_stream->SetVisitor(
      std::make_unique<ControlStream>(this, control_stream));
  control_stream_ = control_stream->GetStreamId();
  MoqtClientSetup setup = MoqtClientSetup{
      .supported_versions = std::vector<MoqtVersion>{parameters_.version},
      .parameters = parameters_,
  };
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
    stream->SetVisitor(std::make_unique<ControlStream>(this, stream));
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
  std::optional<absl::string_view> payload = ParseDatagram(datagram, message);
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
  if (!track->OnObject(/*is_datagram=*/true)) {
    OnMalformedTrack(track);
    return;
  }
  if (!track->InWindow(Location(message.group_id, message.object_id))) {
    // TODO(martinduke): a recent SUBSCRIBE_UPDATE could put us here, and it's
    // not an error.
    return;
  }
  QUICHE_CHECK(!track->is_fetch());
  SubscribeRemoteTrack::Visitor* visitor = track->visitor();
  if (visitor != nullptr) {
    // TODO(martinduke): Handle extension headers.
    PublishedObjectMetadata metadata;
    metadata.location = Location(message.group_id, message.object_id);
    metadata.subgroup = message.object_id;
    metadata.status = message.object_status;
    metadata.publisher_priority = message.publisher_priority;
    metadata.arrival_time = callbacks_.clock->Now();
    visitor->OnObjectFragment(track->full_track_name(), metadata, *payload,
                              true);
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

bool MoqtSession::SubscribeAnnounces(
    TrackNamespace track_namespace,
    MoqtOutgoingSubscribeAnnouncesCallback callback,
    VersionSpecificParameters parameters) {
  QUICHE_DCHECK(track_namespace.IsValid());
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Tried to send SUBSCRIBE_ANNOUNCES after GOAWAY";
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
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE_ANNOUNCES with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  if (outgoing_subscribe_announces_.contains(track_namespace)) {
    std::move(callback)(
        track_namespace, RequestErrorCode::kInternalError,
        "SUBSCRIBE_ANNOUNCES already outstanding for namespace");
    return false;
  }
  MoqtSubscribeAnnounces message;
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  message.track_namespace = track_namespace;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeSubscribeAnnounces(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_ANNOUNCES message for "
                  << message.track_namespace;
  pending_outgoing_subscribe_announces_[message.request_id] =
      PendingSubscribeAnnouncesData{track_namespace, std::move(callback)};
  outgoing_subscribe_announces_.emplace(track_namespace);
  return true;
}

bool MoqtSession::UnsubscribeAnnounces(TrackNamespace track_namespace) {
  QUICHE_DCHECK(track_namespace.IsValid());
  if (!outgoing_subscribe_announces_.contains(track_namespace)) {
    return false;
  }
  MoqtUnsubscribeAnnounces message;
  message.track_namespace = track_namespace;
  SendControlMessage(framer_.SerializeUnsubscribeAnnounces(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent UNSUBSCRIBE_ANNOUNCES message for "
                  << message.track_namespace;
  outgoing_subscribe_announces_.erase(track_namespace);
  return true;
}

void MoqtSession::Announce(TrackNamespace track_namespace,
                           MoqtOutgoingAnnounceCallback announce_callback,
                           VersionSpecificParameters parameters) {
  QUICHE_DCHECK(track_namespace.IsValid());
  if (outgoing_announces_.contains(track_namespace)) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{RequestErrorCode::kInternalError,
                                "ANNOUNCE already outstanding for namespace"});
    return;
  }
  if (next_request_id_ >= peer_max_request_id_) {
    if (!last_requests_blocked_sent_.has_value() ||
        peer_max_request_id_ > *last_requests_blocked_sent_) {
      MoqtRequestsBlocked requests_blocked;
      requests_blocked.max_request_id = peer_max_request_id_;
      SendControlMessage(framer_.SerializeRequestsBlocked(requests_blocked));
      last_requests_blocked_sent_ = peer_max_request_id_;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send ANNOUNCE with ID "
                    << next_request_id_
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send ANNOUNCE after GOAWAY";
    return;
  }
  MoqtAnnounce message;
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  message.track_namespace = track_namespace;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeAnnounce(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE message for "
                  << message.track_namespace;
  pending_outgoing_announces_[message.request_id] = track_namespace;
  outgoing_announces_[track_namespace] = std::move(announce_callback);
}

bool MoqtSession::Unannounce(TrackNamespace track_namespace) {
  QUICHE_DCHECK(track_namespace.IsValid());
  auto it = outgoing_announces_.find(track_namespace);
  if (it == outgoing_announces_.end()) {
    return false;  // Could have been destroyed by ANNOUNCE_CANCEL.
  }
  MoqtUnannounce message;
  message.track_namespace = track_namespace;
  SendControlMessage(framer_.SerializeUnannounce(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent UNANNOUNCE message for "
                  << message.track_namespace;
  outgoing_announces_.erase(it);
  return true;
}

void MoqtSession::CancelAnnounce(TrackNamespace track_namespace,
                                 RequestErrorCode code,
                                 absl::string_view reason) {
  QUICHE_DCHECK(track_namespace.IsValid());
  MoqtAnnounceCancel message{track_namespace, code, std::string(reason)};

  SendControlMessage(framer_.SerializeAnnounceCancel(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE_CANCEL message for "
                  << message.track_namespace << " with reason " << reason;
}

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    SubscribeRemoteTrack::Visitor* visitor,
                                    VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.forward = true;
  message.filter_type = MoqtFilterType::kAbsoluteStart;
  message.start = Location(start_group, start_object);
  message.end_group = std::nullopt;
  message.parameters = parameters;
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group,
                                    SubscribeRemoteTrack::Visitor* visitor,
                                    VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.forward = true;
  message.filter_type = MoqtFilterType::kAbsoluteRange;
  message.start = Location(start_group, start_object);
  message.end_group = end_group;
  message.parameters = parameters;
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentObject(const FullTrackName& name,
                                         SubscribeRemoteTrack::Visitor* visitor,
                                         VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.forward = true;
  message.filter_type = MoqtFilterType::kLatestObject;
  message.start = std::nullopt;
  message.end_group = std::nullopt;
  message.parameters = parameters;
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeNextGroup(const FullTrackName& name,
                                     SubscribeRemoteTrack::Visitor* visitor,
                                     VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.forward = true;
  message.filter_type = MoqtFilterType::kNextGroupStart;
  message.start = std::nullopt;
  message.end_group = std::nullopt;
  message.parameters = parameters;
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeUpdate(
    const FullTrackName& name, std::optional<Location> start,
    std::optional<uint64_t> end_group,
    std::optional<MoqtPriority> subscriber_priority,
    std::optional<bool> forward, VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  auto it = subscribe_by_name_.find(name);
  if (it == subscribe_by_name_.end()) {
    return false;
  }
  QUICHE_DCHECK(name.IsValid());
  SubscribeRemoteTrack* track = it->second;
  MoqtSubscribeUpdate subscribe_update;
  subscribe_update.request_id = track->request_id();
  subscribe_update.start = start.value_or(track->window().start());
  subscribe_update.end_group = end_group.value_or(track->window().end().group);
  if (subscribe_update.end_group == UINT64_MAX) {
    subscribe_update.end_group = std::nullopt;
  }
  subscribe_update.subscriber_priority =
      subscriber_priority.value_or(track->subscriber_priority());
  subscribe_update.forward = forward.value_or(track->forward());
  subscribe_update.parameters = parameters;
  if (subscribe_update.start < track->window().start() ||
      (subscribe_update.end_group.has_value() &&
       (*subscribe_update.end_group > track->window().end().group ||
        *subscribe_update.end_group < subscribe_update.start.group))) {
    // Invalid range.
    return false;
  }
  // Input is valid. Update subscription properties.
  track->TruncateStart(subscribe_update.start);
  if (subscribe_update.end_group.has_value()) {
    track->TruncateEnd(*subscribe_update.end_group);
  }
  track->set_subscriber_priority(subscribe_update.subscriber_priority);
  track->set_forward(subscribe_update.forward);
  SendControlMessage(framer_.SerializeSubscribeUpdate(subscribe_update));
  return true;
};

void MoqtSession::Unsubscribe(const FullTrackName& name) {
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
                        MoqtPriority priority,
                        std::optional<MoqtDeliveryOrder> delivery_order,
                        VersionSpecificParameters parameters) {
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
  message.fetch = StandaloneFetch(name, start, end_group, end_object);
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  message.subscriber_priority = priority;
  message.group_order = delivery_order;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeFetch(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent FETCH message for " << name;
  auto fetch = std::make_unique<UpstreamFetch>(
      message, std::get<StandaloneFetch>(message.fetch), std::move(callback));
  upstream_by_id_.emplace(message.request_id, std::move(fetch));
  return true;
}

bool MoqtSession::RelativeJoiningFetch(const FullTrackName& name,
                                       SubscribeRemoteTrack::Visitor* visitor,
                                       uint64_t num_previous_groups,
                                       VersionSpecificParameters parameters) {
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
        auto* subscribe = static_cast<SubscribeRemoteTrack*>(track);
        RemoteTrackByName(track->full_track_name());
        subscribe->OnJoiningFetchReady(std::move(fetch_task));
      },
      num_previous_groups, kDefaultSubscriberPriority, std::nullopt,
      parameters);
}

bool MoqtSession::RelativeJoiningFetch(
    const FullTrackName& name, SubscribeRemoteTrack::Visitor* visitor,
    FetchResponseCallback callback, uint64_t num_previous_groups,
    MoqtPriority priority, std::optional<MoqtDeliveryOrder> delivery_order,
    VersionSpecificParameters parameters) {
  QUICHE_DCHECK(name.IsValid());
  if ((next_request_id_ + 2) >= peer_max_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send JOINING_FETCH with ID "
                    << (next_request_id_ + 2)
                    << " which is greater than the maximum ID "
                    << peer_max_request_id_;
    return false;
  }
  MoqtSubscribe subscribe;
  subscribe.full_track_name = name;
  subscribe.subscriber_priority = priority;
  subscribe.group_order = delivery_order;
  subscribe.forward = true;
  subscribe.filter_type = MoqtFilterType::kLatestObject;
  subscribe.start = std::nullopt;
  subscribe.end_group = std::nullopt;
  subscribe.parameters = parameters;
  if (!Subscribe(subscribe, visitor)) {
    return false;
  }
  MoqtFetch fetch;
  fetch.request_id = next_request_id_;
  next_request_id_ += 2;
  fetch.subscriber_priority = priority;
  fetch.group_order = delivery_order;
  fetch.fetch = JoiningFetchRelative{subscribe.request_id, num_previous_groups};
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
        if (object.metadata.status == MoqtObjectStatus::kObjectDoesNotExist) {
          QUIC_BUG(quic_bug_got_doesnotexist_in_fetch)
              << "Got ObjectDoesNotExist in FETCH";
          continue;
        }
        if (fetch->session_->WriteObjectToStream(
                stream_, fetch->request_id(), object.metadata,
                std::move(object.payload), MoqtDataStreamType::Fetch(),
                !stream_header_written_,
                /*fin=*/false)) {
          stream_header_written_ = true;
        }
        break;
      case MoqtFetchTask::GetNextObjectResult::kPending:
        return;
      case MoqtFetchTask::GetNextObjectResult::kEof:
        // TODO(martinduke): Either prefetch the next object, or alter the API
        // so that we're not sending FIN in a separate frame.
        if (!quiche::SendFinOnStream(*stream_).ok()) {
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

bool MoqtSession::SubscribeIsDone(uint64_t request_id, SubscribeDoneCode code,
                                  absl::string_view error_reason) {
  auto it = published_subscriptions_.find(request_id);
  if (it == published_subscriptions_.end()) {
    return false;
  }

  PublishedSubscription& subscription = *it->second;
  std::vector<webtransport::StreamId> streams_to_reset =
      subscription.GetAllStreams();

  MoqtSubscribeDone subscribe_done;
  subscribe_done.request_id = request_id;
  subscribe_done.status_code = code;
  subscribe_done.stream_count = subscription.streams_opened();
  subscribe_done.error_reason = error_reason;
  SendControlMessage(framer_.SerializeSubscribeDone(subscribe_done));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_DONE message for "
                  << subscription.publisher().GetTrackName();
  // Clean up the subscription
  published_subscriptions_.erase(it);
  for (webtransport::StreamId stream_id : streams_to_reset) {
    webtransport::Stream* stream = session_->GetStreamById(stream_id);
    if (stream == nullptr) {
      continue;
    }
    stream->ResetWithUserCode(kResetCodeCanceled);
  }
  return true;
}

void MoqtSession::MaybeDestroySubscription(SubscribeRemoteTrack* subscribe) {
  if (subscribe != nullptr && subscribe->all_streams_closed()) {
    DestroySubscription(subscribe);
  }
}

void MoqtSession::DestroySubscription(SubscribeRemoteTrack* subscribe) {
  subscribe->visitor()->OnSubscribeDone(subscribe->full_track_name());
  subscribe_by_name_.erase(subscribe->full_track_name());
  if (subscribe->track_alias().has_value()) {
    subscribe_by_alias_.erase(*subscribe->track_alias());
  }
}

bool MoqtSession::Subscribe(MoqtSubscribe& message,
                            SubscribeRemoteTrack::Visitor* visitor) {
  // TODO(martinduke): support authorization info
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
  if (subscribe_by_name_.contains(message.full_track_name)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE for track "
                    << message.full_track_name
                    << " which is already subscribed";
    return false;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE after GOAWAY";
    return false;
  }
  message.request_id = next_request_id_;
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
  if (request_id != next_incoming_request_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Request ID not monotonically increasing";
    Error(MoqtError::kInvalidRequestId,
          "Request ID not monotonically increasing");
    return false;
  }
  next_incoming_request_id_ = request_id + 2;
  return true;
}

MoqtSession::ControlStream::ControlStream(MoqtSession* session,
                                          webtransport::Stream* stream)
    : session_(session),
      stream_(stream),
      parser_(session->parameters_.using_webtrans, stream, *this) {
  stream_->SetPriority(
      webtransport::StreamPriority{/*send_group_id=*/kMoqtSendGroupId,
                                   /*send_order=*/kMoqtControlStreamSendOrder});
}

void MoqtSession::ControlStream::OnCanRead() {
  parser_.ReadAndDispatchMessages();
}
void MoqtSession::ControlStream::OnCanWrite() {
  // We buffer serialized control frames unconditionally, thus OnCanWrite()
  // requires no handling for control streams.
}

void MoqtSession::ControlStream::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  session_->Error(MoqtError::kProtocolViolation,
                  absl::StrCat("Control stream reset with error code ", error));
}
void MoqtSession::ControlStream::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  session_->Error(MoqtError::kProtocolViolation,
                  absl::StrCat("Control stream reset with error code ", error));
}

void MoqtSession::ControlStream::OnClientSetupMessage(
    const MoqtClientSetup& message) {
  session_->control_stream_ = stream_->GetStreamId();
  if (perspective() == Perspective::IS_CLIENT) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received CLIENT_SETUP from server");
    return;
  }
  if (absl::c_find(message.supported_versions, session_->parameters_.version) ==
      message.supported_versions.end()) {
    // TODO(martinduke): Is this the right error code? See issue #346.
    session_->Error(MoqtError::kVersionNegotiationFailed,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  session_->peer_supports_object_ack_ = message.parameters.support_object_acks;
  QUICHE_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  if (session_->parameters_.perspective == Perspective::IS_SERVER) {
    MoqtServerSetup response;
    response.parameters = session_->parameters_;
    response.selected_version = session_->parameters_.version;
    SendOrBufferMessage(session_->framer_.SerializeServerSetup(response));
    QUIC_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle path.
  session_->peer_max_request_id_ = message.parameters.max_request_id;
  std::move(session_->callbacks_.session_established_callback)();
}

void MoqtSession::ControlStream::OnServerSetupMessage(
    const MoqtServerSetup& message) {
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
  session_->peer_supports_object_ack_ = message.parameters.support_object_acks;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle path.
  session_->peer_max_request_id_ = message.parameters.max_request_id;
  std::move(session_->callbacks_.session_established_callback)();
}

void MoqtSession::ControlStream::SendSubscribeError(
    uint64_t request_id, RequestErrorCode error_code,
    absl::string_view reason_phrase) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.request_id = request_id;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  SendOrBufferMessage(
      session_->framer_.SerializeSubscribeError(subscribe_error));
}

void MoqtSession::ControlStream::SendFetchError(
    uint64_t request_id, RequestErrorCode error_code,
    absl::string_view error_reason) {
  MoqtFetchError fetch_error;
  fetch_error.request_id = request_id;
  fetch_error.error_code = error_code;
  fetch_error.error_reason = error_reason;
  SendOrBufferMessage(session_->framer_.SerializeFetchError(fetch_error));
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
    SendSubscribeError(message.request_id, RequestErrorCode::kUnauthorized,
                       "SUBSCRIBE after GOAWAY");
    return;
  }
  if (session_->subscribed_track_names_.contains(message.full_track_name)) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Duplicate subscribe for track");
    return;
  }
  const FullTrackName& track_name = message.full_track_name;
  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> track_publisher =
      session_->publisher_->GetTrack(track_name);
  if (!track_publisher.ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "SUBSCRIBE for " << track_name
                    << " rejected by the application: "
                    << track_publisher.status();
    SendSubscribeError(message.request_id, RequestErrorCode::kTrackDoesNotExist,
                       track_publisher.status().message());
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

  MoqtTrackPublisher* track_publisher_ptr = track_publisher->get();
  auto subscription = std::make_unique<MoqtSession::PublishedSubscription>(
      session_, *std::move(track_publisher), message, monitoring);
  subscription->set_delivery_timeout(message.parameters.delivery_timeout);
  MoqtSession::PublishedSubscription* subscription_ptr = subscription.get();
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
  if (message.largest_location.has_value()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "request_id = " << message.request_id << " "
                    << track->full_track_name()
                    << " largest_id = " << *message.largest_location;
  } else {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "request_id = " << message.request_id << " "
                    << track->full_track_name();
  }
  SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
  subscribe->OnObjectOrOk();
  auto [it, success] =
      session_->subscribe_by_alias_.try_emplace(message.track_alias, subscribe);
  if (!success) {
    session_->Error(MoqtError::kDuplicateTrackAlias, "");
    return;
  }
  subscribe->set_track_alias(message.track_alias);
  // TODO(martinduke): Handle expires field.
  if (message.largest_location.has_value()) {
    subscribe->TruncateStart(message.largest_location->next());
  }
  if (subscribe->visitor() != nullptr) {
    subscribe->visitor()->OnReply(track->full_track_name(),
                                  message.largest_location, std::nullopt);
  }
}

void MoqtSession::ControlStream::OnSubscribeErrorMessage(
    const MoqtSubscribeError& message) {
  RemoteTrack* track = session_->RemoteTrackById(message.request_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_ERROR for "
                    << "request_id = " << message.request_id
                    << " but no track exists";
    // Subscription state might have been destroyed for internal reasons.
    return;
  }
  if (track->is_fetch()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR for a FETCH");
    return;
  }
  if (!track->ErrorIsAllowed()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR after SUBSCRIBE_OK or objects");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_ERROR for "
                  << "request_id = " << message.request_id << " ("
                  << track->full_track_name() << ")"
                  << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
  // Delete the by-name entry at this point prevents Subscribe() from throwing
  // an error due to a duplicate track name. The other entries for this
  // subscribe will be deleted after calling Subscribe().
  session_->subscribe_by_name_.erase(subscribe->full_track_name());
  if (subscribe->visitor() != nullptr) {
    subscribe->visitor()->OnReply(subscribe->full_track_name(), std::nullopt,
                                  message.reason_phrase);
  }
  session_->upstream_by_id_.erase(subscribe->request_id());
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

void MoqtSession::ControlStream::OnSubscribeDoneMessage(
    const MoqtSubscribeDone& message) {
  auto it = session_->upstream_by_id_.find(message.request_id);
  if (it == session_->upstream_by_id_.end()) {
    return;
  }
  auto* subscribe = static_cast<SubscribeRemoteTrack*>(it->second.get());
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE_DONE for "
                  << it->second->full_track_name();
  subscribe->OnSubscribeDone(
      message.stream_count, session_->callbacks_.clock,
      absl::WrapUnique(session_->alarm_factory_->CreateAlarm(
          new SubscribeDoneDelegate(session_, subscribe))));
  session_->MaybeDestroySubscription(subscribe);
}

void MoqtSession::ControlStream::OnSubscribeUpdateMessage(
    const MoqtSubscribeUpdate& message) {
  auto it = session_->published_subscriptions_.find(message.request_id);
  if (it == session_->published_subscriptions_.end()) {
    return;
  }
  it->second->Update(message.start, message.end_group,
                     message.subscriber_priority);
  it->second->set_delivery_timeout(message.parameters.delivery_timeout);
}

void MoqtSession::ControlStream::OnAnnounceMessage(
    const MoqtAnnounce& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an ANNOUNCE after GOAWAY";
    MoqtAnnounceError error;
    error.request_id = message.request_id;
    error.error_code = RequestErrorCode::kUnauthorized;
    error.error_reason = "ANNOUNCE after GOAWAY";
    SendOrBufferMessage(session_->framer_.SerializeAnnounceError(error));
    return;
  }
  std::optional<MoqtAnnounceErrorReason> error =
      session_->callbacks_.incoming_announce_callback(message.track_namespace,
                                                      message.parameters);
  if (error.has_value()) {
    MoqtAnnounceError reply;
    reply.request_id = message.request_id;
    reply.error_code = error->error_code;
    reply.error_reason = error->reason_phrase;
    SendOrBufferMessage(session_->framer_.SerializeAnnounceError(reply));
    return;
  }
  MoqtAnnounceOk ok;
  ok.request_id = message.request_id;
  SendOrBufferMessage(session_->framer_.SerializeAnnounceOk(ok));
}

// Do not enforce that there is only one of OK or ERROR per ANNOUNCE. Upon
// ERROR, we immediately destroy the state.
void MoqtSession::ControlStream::OnAnnounceOkMessage(
    const MoqtAnnounceOk& message) {
  auto it = session_->pending_outgoing_announces_.find(message.request_id);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_OK for unknown request_id");
    return;
  }
  TrackNamespace track_namespace = it->second;
  session_->pending_outgoing_announces_.erase(it);
  auto callback_it = session_->outgoing_announces_.find(track_namespace);
  if (callback_it == session_->outgoing_announces_.end()) {
    // It might have already been destroyed due to UNANNOUNCE.
    return;
  }
  std::move(callback_it->second)(track_namespace, std::nullopt);
}

void MoqtSession::ControlStream::OnAnnounceErrorMessage(
    const MoqtAnnounceError& message) {
  auto it = session_->pending_outgoing_announces_.find(message.request_id);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_ERROR for unknown request_id");
    return;
  }
  TrackNamespace track_namespace = it->second;
  session_->pending_outgoing_announces_.erase(it);
  auto it2 = session_->outgoing_announces_.find(track_namespace);
  if (it2 == session_->outgoing_announces_.end()) {
    return;  // State might have been destroyed due to UNANNOUNCE.
  }
  std::move(it2->second)(
      track_namespace,
      MoqtAnnounceErrorReason{message.error_code,
                              std::string(message.error_reason)});
  session_->outgoing_announces_.erase(it2);
}

void MoqtSession::ControlStream::OnAnnounceCancelMessage(
    const MoqtAnnounceCancel& message) {
  // The spec currently says that if a later SUBSCRIBE arrives for this
  // namespace, that SHOULD be a session error. I'm hoping that via Issue #557,
  // this will go away. Regardless, a SHOULD will not compel the session to keep
  // state forever, so there is no support for this requirement.
  auto it = session_->outgoing_announces_.find(message.track_namespace);
  if (it == session_->outgoing_announces_.end()) {
    return;  // State might have been destroyed due to UNANNOUNCE.
  }
  std::move(it->second)(
      message.track_namespace,
      MoqtAnnounceErrorReason{message.error_code,
                              std::string(message.error_reason)});
  session_->outgoing_announces_.erase(it);
}

void MoqtSession::ControlStream::OnTrackStatusRequestMessage(
    const MoqtTrackStatusRequest& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received a TRACK_STATUS_REQUEST after GOAWAY";
    SendOrBufferMessage(session_->framer_.SerializeTrackStatus(
        MoqtTrackStatus(message.request_id, MoqtTrackStatusCode::kDoesNotExist,
                        Location(0, 0))));
    return;
  }
  // TODO(martinduke): Handle authentication.
  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> track =
      session_->publisher_->GetTrack(message.full_track_name);
  if (!track.ok()) {
    SendOrBufferMessage(session_->framer_.SerializeTrackStatus(
        MoqtTrackStatus(message.request_id, MoqtTrackStatusCode::kDoesNotExist,
                        Location(0, 0))));
    return;
  }
  session_->incoming_track_status_.emplace(
      std::pair<uint64_t, DownstreamTrackStatus>(
          message.request_id,
          DownstreamTrackStatus(message.request_id, session_, track->get())));
}

void MoqtSession::ControlStream::OnUnannounceMessage(
    const MoqtUnannounce& message) {
  session_->callbacks_.incoming_announce_callback(message.track_namespace,
                                                  std::nullopt);
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

void MoqtSession::ControlStream::OnSubscribeAnnouncesMessage(
    const MoqtSubscribeAnnounces& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  // TODO(martinduke): Handle authentication.
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received a SUBSCRIBE_ANNOUNCES after GOAWAY";
    MoqtSubscribeAnnouncesError error;
    error.request_id = message.request_id;
    error.error_code = RequestErrorCode::kUnauthorized;
    error.error_reason = "SUBSCRIBE_ANNOUNCES after GOAWAY";
    SendOrBufferMessage(
        session_->framer_.SerializeSubscribeAnnouncesError(error));
    return;
  }
  std::optional<MoqtSubscribeErrorReason> result =
      session_->callbacks_.incoming_subscribe_announces_callback(
          message.track_namespace, message.parameters);
  if (result.has_value()) {
    MoqtSubscribeAnnouncesError error;
    error.request_id = message.request_id;
    error.error_code = result->error_code;
    error.error_reason = result->reason_phrase;
    SendOrBufferMessage(
        session_->framer_.SerializeSubscribeAnnouncesError(error));
    return;
  }
  MoqtSubscribeAnnouncesOk ok;
  ok.request_id = message.request_id;
  SendOrBufferMessage(session_->framer_.SerializeSubscribeAnnouncesOk(ok));
}

void MoqtSession::ControlStream::OnSubscribeAnnouncesOkMessage(
    const MoqtSubscribeAnnouncesOk& message) {
  auto it =
      session_->pending_outgoing_subscribe_announces_.find(message.request_id);
  if (it == session_->pending_outgoing_subscribe_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ANNOUNCES_OK for unknown request_id");
    return;  // UNSUBSCRIBE_ANNOUNCES may already have deleted the entry.
  }
  std::move(it->second.callback)(it->second.track_namespace, std::nullopt, "");
  session_->pending_outgoing_subscribe_announces_.erase(it);
}

void MoqtSession::ControlStream::OnSubscribeAnnouncesErrorMessage(
    const MoqtSubscribeAnnouncesError& message) {
  auto it =
      session_->pending_outgoing_subscribe_announces_.find(message.request_id);
  if (it == session_->pending_outgoing_subscribe_announces_.end()) {
    session_->Error(
        MoqtError::kProtocolViolation,
        "Received SUBSCRIBE_ANNOUNCES_ERROR for unknown request_id");
    return;  // UNSUBSCRIBE_ANNOUNCES may already have deleted the entry.
  }
  std::move(it->second.callback)(it->second.track_namespace, message.error_code,
                                 absl::string_view(message.error_reason));
  session_->outgoing_subscribe_announces_.erase(it->second.track_namespace);
  session_->pending_outgoing_subscribe_announces_.erase(it);
}

void MoqtSession::ControlStream::OnUnsubscribeAnnouncesMessage(
    const MoqtUnsubscribeAnnounces& message) {
  // MoqtSession keeps no state here, so just tell the application.
  std::optional<MoqtSubscribeErrorReason> result =
      session_->callbacks_.incoming_subscribe_announces_callback(
          message.track_namespace, std::nullopt);
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
    SendFetchError(message.request_id, RequestErrorCode::kUnauthorized,
                   "FETCH after GOAWAY");
    return;
  }
  FullTrackName track_name;
  Location start_object;
  uint64_t end_group;
  std::optional<uint64_t> end_object;
  if (std::holds_alternative<StandaloneFetch>(message.fetch)) {
    const StandaloneFetch& standalone_fetch =
        std::get<StandaloneFetch>(message.fetch);
    track_name = standalone_fetch.full_track_name;
    start_object = standalone_fetch.start_object;
    end_group = standalone_fetch.end_group;
    end_object = standalone_fetch.end_object;
  } else {
    uint64_t joining_subscribe_id =
        std::holds_alternative<JoiningFetchRelative>(message.fetch)
            ? std::get<struct JoiningFetchRelative>(message.fetch)
                  .joining_subscribe_id
            : std::get<JoiningFetchAbsolute>(message.fetch)
                  .joining_subscribe_id;
    auto it = session_->published_subscriptions_.find(joining_subscribe_id);
    if (it == session_->published_subscriptions_.end()) {
      QUIC_DLOG(INFO) << ENDPOINT << "Received a JOINING_FETCH for "
                      << "subscribe_id " << joining_subscribe_id
                      << " that does not exist";
      SendFetchError(message.request_id, RequestErrorCode::kTrackDoesNotExist,
                     "Joining Fetch for non-existent subscribe");
      return;
    }
    if (it->second->filter_type() != MoqtFilterType::kLatestObject) {
      // Current state variables do not allow us to distinguish between
      // LatestObject and AbsoluteStart with object ID > 0, but accept
      // JoiningFetch for AbsoluteStart.
      QUIC_DLOG(INFO) << ENDPOINT << "Received a JOINING_FETCH for "
                      << "subscribe_id " << joining_subscribe_id
                      << " that is not a LatestObject";
      session_->Error(MoqtError::kProtocolViolation,
                      "Joining Fetch for non-LatestObject subscribe");
      return;
    }
    track_name = it->second->publisher().GetTrackName();
    Location fetch_end = it->second->GetWindowStart();
    if (std::holds_alternative<JoiningFetchRelative>(message.fetch)) {
      const JoiningFetchRelative& relative_fetch =
          std::get<JoiningFetchRelative>(message.fetch);
      if (relative_fetch.joining_start > fetch_end.group) {
        start_object = Location(0, 0);
      } else {
        start_object =
            Location(fetch_end.group - relative_fetch.joining_start, 0);
      }
    } else {
      const JoiningFetchAbsolute& absolute_fetch =
          std::get<JoiningFetchAbsolute>(message.fetch);
      start_object =
          Location(fetch_end.group - absolute_fetch.joining_start, 0);
    }
    end_group = fetch_end.group;
    end_object = fetch_end.object - 1;
  }
  // The check for end_object < start_object is done in
  // MoqtTrackPublisher::Fetch().
  QUIC_DLOG(INFO) << ENDPOINT << "Received a FETCH for " << track_name;
  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> track_publisher =
      session_->publisher_->GetTrack(track_name);
  if (!track_publisher.ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " rejected by the application: "
                    << track_publisher.status();
    SendFetchError(message.request_id, RequestErrorCode::kTrackDoesNotExist,
                   track_publisher.status().message());
    return;
  }
  std::unique_ptr<MoqtFetchTask> fetch =
      (*track_publisher)
          ->Fetch(start_object, end_group, end_object,
                  message.group_order.value_or(
                      (*track_publisher)->GetDeliveryOrder()));
  if (!fetch->GetStatus().ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " could not initialize the task";
    SendFetchError(message.request_id, RequestErrorCode::kInvalidRange,
                   fetch->GetStatus().message());
    return;
  }
  auto published_fetch = std::make_unique<PublishedFetch>(
      message.request_id, session_, std::move(fetch));
  auto result = session_->incoming_fetches_.emplace(message.request_id,
                                                    std::move(published_fetch));
  if (!result.second) {  // Emplace failed.
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " could not be added to the session";
    SendFetchError(message.request_id, RequestErrorCode::kInternalError,
                   "Could not initialize FETCH state");
  }
  MoqtFetchTask* fetch_task = result.first->second->fetch_task();
  fetch_task->SetFetchResponseCallback(
      [this, request_id = message.request_id, fetch_start = start_object,
       fetch_end = Location(end_group, end_object.value_or(UINT64_MAX))](
          std::variant<MoqtFetchOk, MoqtFetchError> message) {
        if (!session_->incoming_fetches_.contains(request_id)) {
          return;  // FETCH was cancelled.
        }
        if (std::holds_alternative<MoqtFetchOk>(message)) {
          MoqtFetchOk& fetch_ok = std::get<MoqtFetchOk>(message);
          fetch_ok.request_id = request_id;
          if (fetch_ok.end_location < fetch_start ||
              fetch_ok.end_location > fetch_end) {
            // TODO(martinduke): Add end_of_track to fetch_ok and check it's
            // larger than end_location.
            QUIC_BUG(quic_bug_fetch_ok_status_error)
                << "FETCH_OK end or end_of_track is invalid";
            session_->Error(MoqtError::kInternalError, "FETCH_OK status error");
            return;
          }
          SendOrBufferMessage(session_->framer_.SerializeFetchOk(fetch_ok));
          return;
        }
        MoqtFetchError& fetch_error = std::get<MoqtFetchError>(message);
        fetch_error.request_id = request_id;
        SendOrBufferMessage(session_->framer_.SerializeFetchError(fetch_error));
      });
  // Set a temporary new-object callback that creates a data stream. When
  // created, the stream visitor will replace this callback.
  fetch_task->SetObjectAvailableCallback(
      [this, send_order = SendOrderForFetch(message.subscriber_priority),
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
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
  fetch->OnFetchResult(
      message.end_location, message.group_order, absl::OkStatus(),
      [=, session = session_]() { session->CancelFetch(message.request_id); });
}

void MoqtSession::ControlStream::OnFetchErrorMessage(
    const MoqtFetchError& message) {
  RemoteTrack* track = session_->RemoteTrackById(message.request_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_ERROR for "
                    << "request_id = " << message.request_id
                    << " but no track exists";
    // Subscription state might have been destroyed for internal reasons.
    return;
  }
  if (!track->is_fetch()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received FETCH_ERROR for a SUBSCRIBE");
    return;
  }
  if (!track->ErrorIsAllowed()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received FETCH_ERROR after FETCH_OK or objects");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_ERROR for "
                  << "request_id = " << message.request_id << " ("
                  << track->full_track_name() << ")"
                  << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.error_reason << ")";
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
  absl::Status status =
      RequestErrorCodeToStatus(message.error_code, message.error_reason);
  fetch->OnFetchResult(Location(0, 0), MoqtDeliveryOrder::kAscending, status,
                       nullptr);
  session_->upstream_by_id_.erase(message.request_id);
}

void MoqtSession::ControlStream::OnRequestsBlockedMessage(
    const MoqtRequestsBlocked& message) {
  // TODO(martinduke): Derive logic for granting more subscribes.
}

void MoqtSession::ControlStream::OnPublishMessage(const MoqtPublish& message) {
  if (!session_->ValidateRequestId(message.request_id)) {
    return;
  }
  MoqtPublishError publish_error = {
      .request_id = message.request_id,
      .error_code = RequestErrorCode::kNotSupported,
      .error_reason = "PUBLISH is not supported",
  };
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received a PUBLISH after GOAWAY";
    publish_error.error_code = RequestErrorCode::kUnauthorized;
    publish_error.error_reason = "Received a PUBLISH after GOAWAY";
  }
  // TODO(martinduke): Process these messages.
  SendOrBufferMessage(session_->framer_.SerializePublishError(publish_error));
}

void MoqtSession::ControlStream::OnParsingError(MoqtError error_code,
                                                absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

void MoqtSession::ControlStream::SendOrBufferMessage(
    quiche::QuicheBuffer message, bool fin) {
  quiche::StreamWriteOptions options;
  options.set_send_fin(fin);
  // TODO: while we buffer unconditionally, we should still at some point tear
  // down the connection if we've buffered too many control messages; otherwise,
  // there is potential for memory exhaustion attacks.
  options.set_buffer_unconditionally(true);
  std::array write_vector = {quiche::QuicheMemSlice(std::move(message))};
  absl::Status success = stream_->Writev(absl::MakeSpan(write_vector), options);
  if (!success.ok()) {
    session_->Error(MoqtError::kInternalError,
                    "Failed to write a control message");
  }
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
      stream_->SendStopSending(kResetCodeCanceled);
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
  if (!track->InWindow(Location(message.group_id, message.object_id))) {
    // This is not an error. It can be the result of a recent SUBSCRIBE_UPDATE.
    return;
  }
  if (!track->is_fetch()) {
    if (no_more_objects_) {
      // Already got a stream-ending object.
      session_->OnMalformedTrack(track);
      return;
    }
    if (message.object_id < next_object_id_) {
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
    SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
    if (!subscribe->OnObject(/*is_datagram=*/false)) {
      session_->OnMalformedTrack(track);
      return;
    }
    if (subscribe->visitor() != nullptr) {
      // TODO(martinduke): Send extension headers.
      PublishedObjectMetadata metadata;
      metadata.location = Location(message.group_id, message.object_id);
      metadata.subgroup = message.subgroup_id;
      metadata.status = message.object_status;
      metadata.publisher_priority = message.publisher_priority;
      metadata.arrival_time = session_->callbacks_.clock->Now();
      subscribe->visitor()->OnObjectFragment(track->full_track_name(), metadata,
                                             payload, end_of_message);
    }
  } else {  // FETCH
    track->OnObjectOrOk();
    UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
    if (!fetch->LocationIsValid(Location(message.group_id, message.object_id),
                                message.object_status, end_of_message)) {
      session_->OnMalformedTrack(track);
      return;
    }
    UpstreamFetch::UpstreamFetchTask* task = fetch->task();
    if (task == nullptr) {
      // The application killed the FETCH.
      stream_->SendStopSending(kResetCodeCanceled);
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
  // It's a subscribe.
  SubscribeRemoteTrack* subscribe =
      static_cast<SubscribeRemoteTrack*>(track_.GetIfAvailable());
  if (subscribe == nullptr) {
    return;
  }
  subscribe->OnStreamClosed();
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
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
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
  bool knew_track_alias = parser_.track_alias().has_value();
  if (parser_.stream_type()->IsSubgroup()) {
    parser_.ReadAllData();
  } else if (!knew_track_alias) {
    parser_.ReadTrackAlias();
  }
  if (!parser_.track_alias().has_value()) {
    return;
  }
  if (parser_.stream_type()->IsSubgroup()) {
    if (knew_track_alias) {
      return;
    }
    // This is a new stream for a subscribe. Notify the subscription.
    auto it = session_->subscribe_by_alias_.find(*parser_.track_alias());
    if (it == session_->subscribe_by_alias_.end()) {
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received object for a track with no SUBSCRIBE";
      // This is a not a session error because there might be an UNSUBSCRIBE or
      // SUBSCRIBE_OK (containing the track alias) in flight.
      stream_->SendStopSending(kResetCodeCanceled);
      return;
    }
    it->second->OnStreamOpened();
    return;
  }
  auto it = session_->upstream_by_id_.find(*parser_.track_alias());
  if (it == session_->upstream_by_id_.end()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received object for a track with no FETCH";
    // This is a not a session error because there might be an UNSUBSCRIBE in
    // flight.
    stream_->SendStopSending(kResetCodeCanceled);
    return;
  }
  if (it->second == nullptr) {
    QUICHE_BUG(quiche_bug_moqt_fetch_pointer_is_null)
        << "Fetch pointer is null";
    return;
  }
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(it->second.get());
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
    const MoqtSubscribe& subscribe,
    MoqtPublishingMonitorInterface* monitoring_interface)
    : session_(session),
      track_publisher_(track_publisher),
      request_id_(subscribe.request_id),
      filter_type_(subscribe.filter_type),
      forward_(subscribe.forward),
      window_(SubscribeMessageToWindow(subscribe)),
      subscriber_priority_(subscribe.subscriber_priority),
      subscriber_delivery_order_(subscribe.group_order),
      monitoring_interface_(monitoring_interface) {
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckSupportKnown(
        subscribe.parameters.oack_window_size);
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << subscribe.full_track_name;
  session_->subscribed_track_names_.insert(subscribe.full_track_name);
}

MoqtSession::PublishedSubscription::~PublishedSubscription() {
  track_publisher_->RemoveObjectListener(this);
  session_->subscribed_track_names_.erase(track_publisher_->GetTrackName());
}

SendStreamMap& MoqtSession::PublishedSubscription::stream_map() {
  // The stream map is lazily initialized, since initializing it requires
  // knowing the forwarding preference in advance, and it might not be known
  // when the subscription is first created.
  if (!lazily_initialized_stream_map_.has_value()) {
    QUICHE_DCHECK(
        DoesTrackStatusImplyHavingData(*track_publisher_->GetTrackStatus()));
    lazily_initialized_stream_map_.emplace();
  }
  return *lazily_initialized_stream_map_;
}

void MoqtSession::PublishedSubscription::Update(
    Location start, std::optional<uint64_t> end_group,
    MoqtPriority subscriber_priority) {
  subscriber_priority_ = subscriber_priority;
  if (!window_.has_value()) {
    window_ = SubscribeWindow(start, end_group);
    return;
  }
  window_->TruncateStart(start);
  if (end_group.has_value()) {
    window_->TruncateEnd(*end_group);
  }
  // TODO: update priority of all data streams that are currently open.
  // TODO: update delivery timeout.
  // TODO: update forward and subscribe filter.

  // TODO: reset streams that are no longer in-window.
  // TODO: send SUBSCRIBE_DONE if required.
  // TODO: send an error for invalid updates now that it's a part of draft-05.
}

void MoqtSession::PublishedSubscription::set_subscriber_priority(
    MoqtPriority priority) {
  if (priority == subscriber_priority_) {
    return;
  }
  if (queued_outgoing_data_streams_.empty()) {
    subscriber_priority_ = priority;
    return;
  }
  webtransport::SendOrder old_send_order =
      FinalizeSendOrder(queued_outgoing_data_streams_.rbegin()->first);
  subscriber_priority_ = priority;
  session_->UpdateQueuedSendOrder(request_id_, old_send_order,
                                  FinalizeSendOrder(old_send_order));
};

void MoqtSession::PublishedSubscription::OnSubscribeAccepted() {
  std::optional<Location> largest_location;
  ControlStream* stream = session_->GetControlStream();
  if (PublisherHasData(*track_publisher_)) {
    largest_location = track_publisher_->GetLargestLocation();
    QUICHE_CHECK(largest_location.has_value());
    if (forward_) {
      switch (filter_type_) {
        case MoqtFilterType::kLatestObject:
          window_ = SubscribeWindow(largest_location->next());
          break;
        case MoqtFilterType::kNextGroupStart:
          window_ = SubscribeWindow(Location(largest_location->group + 1, 0));
          break;
        default:
          break;
      }
    }
  } else if (filter_type_ == MoqtFilterType::kLatestObject ||
             filter_type_ == MoqtFilterType::kNextGroupStart) {
    // No data yet. All objects will be in-window.
    window_ = SubscribeWindow(Location(0, 0));
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.request_id = request_id_;
  subscribe_ok.track_alias = session_->next_local_track_alias_++;
  subscribe_ok.group_order = track_publisher_->GetDeliveryOrder();
  subscribe_ok.largest_location = largest_location;
  track_alias_.emplace(subscribe_ok.track_alias);
  // TODO(martinduke): Support sending DELIVERY_TIMEOUT parameter as the
  // publisher.
  stream->SendOrBufferMessage(
      session_->framer_.SerializeSubscribeOk(subscribe_ok));
  if (!PublisherHasData(*track_publisher_)) {
    return;
  }
  // TODO(martinduke): If we buffer objects that arrived previously, the arrival
  // of the track alias disambiguates what subscription they belong to. Send
  // them.
}

void MoqtSession::PublishedSubscription::OnSubscribeRejected(
    MoqtSubscribeErrorReason reason, std::optional<uint64_t> track_alias) {
  session_->GetControlStream()->SendSubscribeError(
      request_id_, reason.error_code, reason.reason_phrase);
  session_->published_subscriptions_.erase(request_id_);
  // No class access below this line!
}

void MoqtSession::PublishedSubscription::OnNewObjectAvailable(
    Location sequence, uint64_t subgroup) {
  if (!InWindow(sequence)) {
    return;
  }
  DataStreamIndex index(sequence.group, subgroup);
  if (reset_subgroups_.contains(index)) {
    // This subgroup has already been reset, ignore.
    return;
  }
  if (session_->alternate_delivery_timeout_ &&
      !delivery_timeout_.IsInfinite() && largest_sent_.has_value() &&
      sequence.group >= largest_sent_->group) {
    // Start the delivery timeout timer on all previous groups.
    for (uint64_t group = first_active_group_; group < sequence.group;
         ++group) {
      for (webtransport::StreamId stream_id :
           stream_map().GetStreamsForGroup(group)) {
        webtransport::Stream* raw_stream =
            session_->session_->GetStreamById(stream_id);
        if (raw_stream == nullptr) {
          continue;
        }
        OutgoingDataStream* stream =
            static_cast<OutgoingDataStream*>(raw_stream->visitor());
        stream->CreateAndSetAlarm(session_->callbacks_.clock->ApproximateNow() +
                                  delivery_timeout_);
      }
    }
  }
  QUICHE_DCHECK_GE(sequence.group, first_active_group_);

  MoqtForwardingPreference forwarding_preference =
      track_publisher_->GetForwardingPreference();
  if (forwarding_preference == MoqtForwardingPreference::kDatagram) {
    SendDatagram(sequence);
    return;
  }

  std::optional<webtransport::StreamId> stream_id =
      stream_map().GetStreamFor(index);
  webtransport::Stream* raw_stream = nullptr;
  if (stream_id.has_value()) {
    raw_stream = session_->session_->GetStreamById(*stream_id);
  } else {
    raw_stream = session_->OpenOrQueueDataStream(
        request_id_,
        NewStreamParameters(sequence.group, subgroup, sequence.object));
  }
  if (raw_stream == nullptr) {
    return;
  }

  OutgoingDataStream* stream =
      static_cast<OutgoingDataStream*>(raw_stream->visitor());
  stream->SendObjects(*this);
}

void MoqtSession::PublishedSubscription::OnTrackPublisherGone() {
  session_->SubscribeIsDone(request_id_, SubscribeDoneCode::kGoingAway,
                            "Publisher is gone");
}

// TODO(martinduke): Revise to check if the last object has been delivered.
void MoqtSession::PublishedSubscription::OnNewFinAvailable(Location location,
                                                           uint64_t subgroup) {
  if (!GroupInWindow(location.group)) {
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
      static_cast<OutgoingDataStream*>(raw_stream->visitor());
  stream->Fin(location);
}

void MoqtSession::PublishedSubscription::OnSubgroupAbandoned(
    uint64_t group, uint64_t subgroup,
    webtransport::StreamErrorCode error_code) {
  if (!GroupInWindow(group)) {
    return;
  }
  DataStreamIndex index(group, subgroup);
  if (reset_subgroups_.contains(index)) {
    // This subgroup has already been reset, ignore.
    return;
  }
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
  if (!window_.has_value() || window_->end().group < group_id ||
      window_->start().group > group_id) {
    // The group is not in the window, ignore.
    return;
  }
  std::vector<webtransport::StreamId> streams =
      stream_map().GetStreamsForGroup(group_id);
  if (delivery_timeout_.IsInfinite() && largest_sent_.has_value() &&
      largest_sent_->group <= group_id) {
    session_->SubscribeIsDone(request_id_, SubscribeDoneCode::kTooFarBehind,
                              "");
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
    Location sequence, uint64_t subgroup) const {
  MoqtForwardingPreference forwarding_preference =
      track_publisher_->GetForwardingPreference();

  MoqtPriority publisher_priority = track_publisher_->GetPublisherPriority();
  MoqtDeliveryOrder delivery_order = subscriber_delivery_order().value_or(
      track_publisher_->GetDeliveryOrder());
  if (forwarding_preference == MoqtForwardingPreference::kDatagram) {
    return SendOrderForDatagram(subscriber_priority_, publisher_priority,
                                sequence.group, sequence.object,
                                delivery_order);
  }
  return SendOrderForStream(subscriber_priority_, publisher_priority,
                            sequence.group, subgroup, delivery_order);
}

// Returns the highest send order in the subscription.
void MoqtSession::PublishedSubscription::AddQueuedOutgoingDataStream(
    const NewStreamParameters& parameters) {
  std::optional<webtransport::SendOrder> start_send_order =
      queued_outgoing_data_streams_.empty()
          ? std::optional<webtransport::SendOrder>()
          : queued_outgoing_data_streams_.rbegin()->first;
  webtransport::SendOrder send_order =
      GetSendOrder(Location(parameters.index.group, parameters.first_object),
                   parameters.index.subgroup);
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
    return NewStreamParameters(0, 0, 0);
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
  // TODO: send SUBSCRIBE_DONE if the subscription is done.
}

MoqtSession::OutgoingDataStream::OutgoingDataStream(
    MoqtSession* session, webtransport::Stream* stream,
    PublishedSubscription& subscription, const NewStreamParameters& parameters)
    : session_(session),
      stream_(stream),
      subscription_id_(subscription.request_id()),
      index_(parameters.index),
      // Always include extension header length, because it's difficult to know
      // a priori if they're going to appear on a stream.
      stream_type_(MoqtDataStreamType::Subgroup(
          index_.subgroup, parameters.first_object, false)),
      next_object_(parameters.first_object),
      session_liveness_(session->liveness_token_) {
  UpdateSendOrder(subscription);
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
    stream_->ResetWithUserCode(kResetCodeCanceled);
    return nullptr;
  }

  PublishedSubscription* subscription = it->second.get();
  MoqtTrackPublisher& publisher = subscription->publisher();
  absl::StatusOr<MoqtTrackStatusCode> status = publisher.GetTrackStatus();
  if (!status.ok()) {
    // TODO: clean up the subscription.
    return nullptr;
  }
  if (!DoesTrackStatusImplyHavingData(*status)) {
    QUICHE_BUG(GetSubscriptionIfValid_InvalidTrackStatus)
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
  if (!subscription.track_alias().has_value()) {
    return;
  }
  while (stream_->CanWrite()) {
    std::optional<PublishedObject> object =
        subscription.publisher().GetCachedObject(index_.group, index_.subgroup,
                                                 next_object_);
    if (!object.has_value()) {
      break;
    }

    QUICHE_DCHECK_EQ(object->metadata.location.group, index_.group);
    QUICHE_DCHECK(object->metadata.subgroup == index_.subgroup);
    QUICHE_DCHECK(subscription.publisher().GetForwardingPreference() ==
                  MoqtForwardingPreference::kSubgroup);
    if (!subscription.InWindow(object->metadata.location)) {
      // It is possible that the next object became irrelevant due to a
      // SUBSCRIBE_UPDATE.  Close the stream if so.
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
    if (!session_->WriteObjectToStream(
            stream_, *subscription.track_alias(), object->metadata,
            std::move(object->payload), stream_type_, !stream_header_written_,
            object->fin_after_this)) {
      // WriteObjectToStream() closes the connection on error, meaning that
      // there is no need to process the stream any further.
      return;
    }
    ++next_object_;
    stream_header_written_ = true;
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

bool MoqtSession::WriteObjectToStream(webtransport::Stream* stream, uint64_t id,
                                      const PublishedObjectMetadata& metadata,
                                      quiche::QuicheMemSlice payload,
                                      MoqtDataStreamType type,
                                      bool is_first_on_stream, bool fin) {
  QUICHE_DCHECK(stream->CanWrite());
  MoqtObject header;
  header.track_alias = id;
  header.group_id = metadata.location.group;
  header.subgroup_id = metadata.subgroup;
  header.object_id = metadata.location.object;
  header.publisher_priority = metadata.publisher_priority;
  header.object_status = metadata.status;
  header.payload_length = payload.length();

  quiche::QuicheBuffer serialized_header =
      framer_.SerializeObjectHeader(header, type, is_first_on_stream);
  // TODO(vasilvv): add a version of WebTransport write API that accepts
  // memslices so that we can avoid a copy here.
  std::array write_vector = {
      quiche::QuicheMemSlice(std::move(serialized_header)), std::move(payload)};
  quiche::StreamWriteOptions options;
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
    static_cast<SubscribeRemoteTrack*>(track)->visitor()->OnMalformedTrack(
        track->full_track_name());
    Unsubscribe(track->full_track_name());
    return;
  }
  UpstreamFetch::UpstreamFetchTask* task =
      static_cast<UpstreamFetch*>(track)->task();
  if (task != nullptr) {
    task->OnStreamAndFetchClosed(kResetCodeMalformedTrack,
                                 "Malformed track received");
  }
  CancelFetch(track->request_id());
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
  std::optional<PublishedObject> object =
      track_publisher_->GetCachedObject(sequence.group, 0, sequence.object);
  if (!object.has_value()) {
    QUICHE_BUG(PublishedSubscription_SendDatagram_object_not_in_cache)
        << "Got notification about an object that is not in the cache";
    return;
  }
  if (!track_alias_.has_value()) {
    return;
  }
  MoqtObject header;
  header.track_alias = *track_alias_;
  header.group_id = object->metadata.location.group;
  header.object_id = object->metadata.location.object;
  header.publisher_priority = object->metadata.publisher_priority;
  header.object_status = object->metadata.status;
  header.subgroup_id = header.object_id;
  header.payload_length = object->payload.length();
  quiche::QuicheBuffer datagram = session_->framer_.SerializeObjectDatagram(
      header, object->payload.AsStringView());
  session_->session_->SendOrQueueDatagram(datagram.AsStringView());
  OnObjectSent(object->metadata.location);
}

void MoqtSession::OutgoingDataStream::UpdateSendOrder(
    PublishedSubscription& subscription) {
  stream_->SetPriority(webtransport::StreamPriority{
      /*send_group_id=*/kMoqtSendGroupId,
      subscription.GetSendOrder(Location(index_.group, next_object_),
                                index_.subgroup)});
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

}  // namespace moqt
