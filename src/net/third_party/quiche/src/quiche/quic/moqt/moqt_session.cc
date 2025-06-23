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
                    << "Received OBJECT message in datagram for subscribe_id "
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
    Error(MoqtError::kProtocolViolation,
          "Received DATAGRAM for non-datagram track");
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
    visitor->OnObjectFragment(track->full_track_name(),
                              Location{message.group_id, 0, message.object_id},
                              message.publisher_priority, message.object_status,
                              *payload, true);
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
    FullTrackName track_namespace,
    MoqtOutgoingSubscribeAnnouncesCallback callback,
    VersionSpecificParameters parameters) {
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Tried to send SUBSCRIBE_ANNOUNCES after GOAWAY";
    return false;
  }
  MoqtSubscribeAnnounces message;
  message.track_namespace = track_namespace;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeSubscribeAnnounces(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_ANNOUNCES message for "
                  << message.track_namespace;
  outgoing_subscribe_announces_[track_namespace] = std::move(callback);
  return true;
}

bool MoqtSession::UnsubscribeAnnounces(FullTrackName track_namespace) {
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

void MoqtSession::Announce(FullTrackName track_namespace,
                           MoqtOutgoingAnnounceCallback announce_callback,
                           VersionSpecificParameters parameters) {
  if (outgoing_announces_.contains(track_namespace)) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{
            RequestErrorCode::kInternalError,
            "ANNOUNCE message already outstanding for namespace"});
    return;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send ANNOUNCE after GOAWAY";
    return;
  }
  MoqtAnnounce message;
  message.track_namespace = track_namespace;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeAnnounce(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE message for "
                  << message.track_namespace;
  outgoing_announces_[track_namespace] = std::move(announce_callback);
}

bool MoqtSession::Unannounce(FullTrackName track_namespace) {
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

void MoqtSession::CancelAnnounce(FullTrackName track_namespace,
                                 RequestErrorCode code,
                                 absl::string_view reason) {
  MoqtAnnounceCancel message{track_namespace, code, std::string(reason)};

  SendControlMessage(framer_.SerializeAnnounceCancel(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE_CANCEL message for "
                  << message.track_namespace << " with reason " << reason;
}

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    SubscribeRemoteTrack::Visitor* visitor,
                                    VersionSpecificParameters parameters) {
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
  auto it = subscribe_by_name_.find(name);
  if (it == subscribe_by_name_.end()) {
    return false;
  }
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
  SubscribeRemoteTrack* track = RemoteTrackByName(name);
  if (track == nullptr) {
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Sent UNSUBSCRIBE message for " << name;
  MoqtUnsubscribe message;
  message.subscribe_id = track->request_id();
  SendControlMessage(framer_.SerializeUnsubscribe(message));
  DestroySubscription(track);
}

bool MoqtSession::Fetch(const FullTrackName& name,
                        FetchResponseCallback callback, Location start,
                        uint64_t end_group, std::optional<uint64_t> end_object,
                        MoqtPriority priority,
                        std::optional<MoqtDeliveryOrder> delivery_order,
                        VersionSpecificParameters parameters) {
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
  message.full_track_name = name;
  message.fetch_id = next_request_id_;
  next_request_id_ += 2;
  message.start_object = start;
  message.end_group = end_group;
  message.end_object = end_object;
  message.subscriber_priority = priority;
  message.group_order = delivery_order;
  message.parameters = parameters;
  SendControlMessage(framer_.SerializeFetch(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent FETCH message for "
                  << message.full_track_name;
  auto fetch = std::make_unique<UpstreamFetch>(message, std::move(callback));
  upstream_by_id_.emplace(message.fetch_id, std::move(fetch));
  return true;
}

bool MoqtSession::JoiningFetch(const FullTrackName& name,
                               SubscribeRemoteTrack::Visitor* visitor,
                               uint64_t num_previous_groups,
                               VersionSpecificParameters parameters) {
  return JoiningFetch(
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

bool MoqtSession::JoiningFetch(const FullTrackName& name,
                               SubscribeRemoteTrack::Visitor* visitor,
                               FetchResponseCallback callback,
                               uint64_t num_previous_groups,
                               MoqtPriority priority,
                               std::optional<MoqtDeliveryOrder> delivery_order,
                               VersionSpecificParameters parameters) {
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
  if (!Subscribe(subscribe, visitor, std::nullopt)) {
    return false;
  }
  MoqtFetch fetch;
  fetch.fetch_id = next_request_id_;
  next_request_id_ += 2;
  fetch.subscriber_priority = priority;
  fetch.group_order = delivery_order;
  fetch.joining_fetch = {subscribe.request_id, num_previous_groups};
  fetch.parameters = parameters;
  SendControlMessage(framer_.SerializeFetch(fetch));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent Joining FETCH message for " << name;
  auto upstream_fetch =
      std::make_unique<UpstreamFetch>(fetch, std::move(callback));
  upstream_by_id_.emplace(fetch.fetch_id, std::move(upstream_fetch));
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
        if (object.status == MoqtObjectStatus::kObjectDoesNotExist) {
          QUIC_BUG(quic_bug_got_doesnotexist_in_fetch)
              << "Got ObjectDoesNotExist in FETCH";
          continue;
        }
        if (fetch->session_->WriteObjectToStream(
                stream_, fetch->fetch_id_, object,
                MoqtDataStreamType::kStreamHeaderFetch, !stream_header_written_,
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

bool MoqtSession::SubscribeIsDone(uint64_t subscribe_id, SubscribeDoneCode code,
                                  absl::string_view reason_phrase) {
  auto it = published_subscriptions_.find(subscribe_id);
  if (it == published_subscriptions_.end()) {
    return false;
  }

  PublishedSubscription& subscription = *it->second;
  std::vector<webtransport::StreamId> streams_to_reset =
      subscription.GetAllStreams();

  MoqtSubscribeDone subscribe_done;
  subscribe_done.subscribe_id = subscribe_id;
  subscribe_done.status_code = code;
  subscribe_done.stream_count = subscription.streams_opened();
  subscribe_done.reason_phrase = reason_phrase;
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
    stream->ResetWithUserCode(kResetCodeSubscriptionGone);
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
  subscribe_by_alias_.erase(subscribe->track_alias());
}

bool MoqtSession::Subscribe(MoqtSubscribe& message,
                            SubscribeRemoteTrack::Visitor* visitor,
                            std::optional<uint64_t> provided_track_alias) {
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
  if (provided_track_alias.has_value() &&
      subscribe_by_alias_.contains(*provided_track_alias)) {
    Error(MoqtError::kProtocolViolation, "Provided track alias already in use");
    return false;
  }
  if (received_goaway_ || sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE after GOAWAY";
    return false;
  }
  message.request_id = next_request_id_;
  next_request_id_ += 2;
  if (provided_track_alias.has_value()) {
    message.track_alias = *provided_track_alias;
    next_remote_track_alias_ =
        std::max(next_remote_track_alias_, *provided_track_alias) + 1;
  } else {
    message.track_alias = next_remote_track_alias_++;
  }
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
  subscribe_by_alias_.emplace(message.track_alias, track.get());
  upstream_by_id_.emplace(message.request_id, std::move(track));
  return true;
}

webtransport::Stream* MoqtSession::OpenOrQueueDataStream(
    uint64_t subscription_id, Location first_object) {
  auto it = published_subscriptions_.find(subscription_id);
  if (it == published_subscriptions_.end()) {
    // It is possible that the subscription has been discarded while the stream
    // was in the queue; discard those streams.
    return nullptr;
  }
  PublishedSubscription& subscription = *it->second;
  if (!session_->CanOpenNextOutgoingUnidirectionalStream()) {
    subscription.AddQueuedOutgoingDataStream(first_object);
    // The subscription will notify the session about how to update the
    // session's queue.
    // TODO: limit the number of streams in the queue.
    return nullptr;
  }
  return OpenDataStream(subscription, first_object);
}

webtransport::Stream* MoqtSession::OpenDataStream(
    PublishedSubscription& subscription, Location first_object) {
  webtransport::Stream* new_stream =
      session_->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    QUICHE_BUG(MoqtSession_OpenDataStream_blocked)
        << "OpenDataStream called when creation of new streams is blocked.";
    return nullptr;
  }
  new_stream->SetVisitor(std::make_unique<OutgoingDataStream>(
      this, new_stream, subscription, first_object));
  subscription.OnDataStreamCreated(new_stream->GetStreamId(), first_object);
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

RemoteTrack* MoqtSession::RemoteTrackById(uint64_t subscribe_id) {
  auto it = upstream_by_id_.find(subscribe_id);
  if (it == upstream_by_id_.end()) {
    return nullptr;
  }
  return it->second.get();
}

SubscribeRemoteTrack* MoqtSession::RemoteTrackByName(
    const FullTrackName& name) {
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
    Location next_queued_stream =
        subscription->second->NextQueuedOutgoingDataStream();
    // Check if Group is too old.
    if (next_queued_stream.group < subscription->second->first_active_group()) {
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
    uint64_t subscribe_id,
    std::optional<webtransport::SendOrder> old_send_order,
    std::optional<webtransport::SendOrder> new_send_order) {
  if (old_send_order == new_send_order) {
    return;
  }
  if (old_send_order.has_value()) {
    subscribes_with_queued_outgoing_data_streams_.erase(
        SubscriptionWithQueuedStream{*old_send_order, subscribe_id});
  }
  if (new_send_order.has_value()) {
    subscribes_with_queued_outgoing_data_streams_.emplace(*new_send_order,
                                                          subscribe_id);
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
    absl::string_view reason_phrase, uint64_t track_alias) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.request_id = request_id;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  subscribe_error.track_alias = track_alias;
  SendOrBufferMessage(
      session_->framer_.SerializeSubscribeError(subscribe_error));
}

void MoqtSession::ControlStream::SendFetchError(
    uint64_t subscribe_id, RequestErrorCode error_code,
    absl::string_view reason_phrase) {
  MoqtFetchError fetch_error;
  fetch_error.subscribe_id = subscribe_id;
  fetch_error.error_code = error_code;
  fetch_error.reason_phrase = reason_phrase;
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
                       "SUBSCRIBE after GOAWAY", message.track_alias);
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
                       track_publisher.status().message(), message.track_alias);
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
                    << "subscribe_id = " << message.request_id
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
                    << "subscribe_id = " << message.request_id << " "
                    << track->full_track_name()
                    << " largest_id = " << *message.largest_location;
  } else {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                    << "subscribe_id = " << message.request_id << " "
                    << track->full_track_name();
  }
  SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
  subscribe->OnObjectOrOk();
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
                    << "subscribe_id = " << message.request_id
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
                  << "subscribe_id = " << message.request_id << " ("
                  << track->full_track_name() << ")"
                  << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
  // Delete the by-name entry at this point prevents Subscribe() from throwing
  // an error due to a duplicate track name. The other entries for this
  // subscribe will be deleted after calling Subscribe().
  session_->subscribe_by_name_.erase(subscribe->full_track_name());
  if (message.error_code == RequestErrorCode::kRetryTrackAlias) {
    // Automatically resubscribe with new alias.
    MoqtSubscribe& subscribe_message = subscribe->GetSubscribe();
    session_->Subscribe(subscribe_message, subscribe->visitor(),
                        message.track_alias);
  } else if (subscribe->visitor() != nullptr) {
    subscribe->visitor()->OnReply(subscribe->full_track_name(), std::nullopt,
                                  message.reason_phrase);
  }
  session_->subscribe_by_alias_.erase(subscribe->track_alias());
  session_->upstream_by_id_.erase(subscribe->request_id());
}

void MoqtSession::ControlStream::OnUnsubscribeMessage(
    const MoqtUnsubscribe& message) {
  auto it = session_->published_subscriptions_.find(message.subscribe_id);
  if (it == session_->published_subscriptions_.end()) {
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received an UNSUBSCRIBE for "
                  << it->second->publisher().GetTrackName();
  session_->published_subscriptions_.erase(it);
}

void MoqtSession::ControlStream::OnSubscribeDoneMessage(
    const MoqtSubscribeDone& message) {
  auto it = session_->upstream_by_id_.find(message.subscribe_id);
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
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an ANNOUNCE after GOAWAY";
    MoqtAnnounceError error;
    error.track_namespace = message.track_namespace;
    error.error_code = RequestErrorCode::kUnauthorized;
    error.reason_phrase = "ANNOUNCE after GOAWAY";
    SendOrBufferMessage(session_->framer_.SerializeAnnounceError(error));
    return;
  }
  std::optional<MoqtAnnounceErrorReason> error =
      session_->callbacks_.incoming_announce_callback(message.track_namespace,
                                                      message.parameters);
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

// Do not enforce that there is only one of OK or ERROR per ANNOUNCE. Upon
// ERROR, we immediately destroy the state.
void MoqtSession::ControlStream::OnAnnounceOkMessage(
    const MoqtAnnounceOk& message) {
  auto it = session_->outgoing_announces_.find(message.track_namespace);
  if (it == session_->outgoing_announces_.end()) {
    return;  // State might have been destroyed due to UNANNOUNCE.
  }
  std::move(it->second)(message.track_namespace, std::nullopt);
}

void MoqtSession::ControlStream::OnAnnounceErrorMessage(
    const MoqtAnnounceError& message) {
  auto it = session_->outgoing_announces_.find(message.track_namespace);
  if (it == session_->outgoing_announces_.end()) {
    return;  // State might have been destroyed due to UNANNOUNCE.
  }
  std::move(it->second)(
      message.track_namespace,
      MoqtAnnounceErrorReason{message.error_code,
                              std::string(message.reason_phrase)});
  session_->outgoing_announces_.erase(it);
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
                              std::string(message.reason_phrase)});
  session_->outgoing_announces_.erase(it);
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
  // TODO(martinduke): Handle authentication.
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received a SUBSCRIBE_ANNOUNCES after GOAWAY";
    MoqtSubscribeAnnouncesError error;
    error.track_namespace = message.track_namespace;
    error.error_code = RequestErrorCode::kUnauthorized;
    error.reason_phrase = "SUBSCRIBE_ANNOUNCES after GOAWAY";
    SendOrBufferMessage(
        session_->framer_.SerializeSubscribeAnnouncesError(error));
    return;
  }
  std::optional<MoqtSubscribeErrorReason> result =
      session_->callbacks_.incoming_subscribe_announces_callback(
          message.track_namespace, message.parameters);
  if (result.has_value()) {
    MoqtSubscribeAnnouncesError error;
    error.track_namespace = message.track_namespace;
    error.error_code = result->error_code;
    error.reason_phrase = result->reason_phrase;
    SendOrBufferMessage(
        session_->framer_.SerializeSubscribeAnnouncesError(error));
    return;
  }
  MoqtSubscribeAnnouncesOk ok;
  ok.track_namespace = message.track_namespace;
  SendOrBufferMessage(session_->framer_.SerializeSubscribeAnnouncesOk(ok));
}

void MoqtSession::ControlStream::OnSubscribeAnnouncesOkMessage(
    const MoqtSubscribeAnnouncesOk& message) {
  auto it =
      session_->outgoing_subscribe_announces_.find(message.track_namespace);
  if (it == session_->outgoing_subscribe_announces_.end()) {
    return;  // UNSUBSCRIBE_ANNOUNCES may already have deleted the entry.
  }
  if (it->second == nullptr) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Two responses to SUBSCRIBE_ANNOUNCES");
    return;
  }
  std::move(it->second)(message.track_namespace, std::nullopt, "");
  it->second = nullptr;
}

void MoqtSession::ControlStream::OnSubscribeAnnouncesErrorMessage(
    const MoqtSubscribeAnnouncesError& message) {
  auto it =
      session_->outgoing_subscribe_announces_.find(message.track_namespace);
  if (it == session_->outgoing_subscribe_announces_.end()) {
    return;  // UNSUBSCRIBE_ANNOUNCES may already have deleted the entry.
  }
  if (it->second == nullptr) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Two responses to SUBSCRIBE_ANNOUNCES");
    return;
  }
  std::move(it->second)(message.track_namespace, message.error_code,
                        absl::string_view(message.reason_phrase));
  session_->outgoing_subscribe_announces_.erase(it);
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
  if (!session_->ValidateRequestId(message.fetch_id)) {
    return;
  }
  if (session_->sent_goaway_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received a FETCH after GOAWAY";
    SendFetchError(message.fetch_id, RequestErrorCode::kUnauthorized,
                   "FETCH after GOAWAY");
    return;
  }
  FullTrackName track_name;
  Location start_object;
  uint64_t end_group;
  std::optional<uint64_t> end_object;
  if (message.joining_fetch.has_value()) {
    uint64_t joining_subscribe_id = message.joining_fetch->joining_subscribe_id;
    auto it = session_->published_subscriptions_.find(joining_subscribe_id);
    if (it == session_->published_subscriptions_.end()) {
      QUIC_DLOG(INFO) << ENDPOINT << "Received a JOINING_FETCH for "
                      << "subscribe_id " << joining_subscribe_id
                      << " that does not exist";
      SendFetchError(message.fetch_id, RequestErrorCode::kTrackDoesNotExist,
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
    if (message.joining_fetch->preceding_group_offset > fetch_end.group) {
      start_object = Location(0, 0);
    } else {
      start_object = Location(
          fetch_end.group - message.joining_fetch->preceding_group_offset, 0,
          0);
    }
    end_group = fetch_end.group;
    end_object = fetch_end.object - 1;
  } else {
    track_name = message.full_track_name;
    start_object = message.start_object;
    end_group = message.end_group;
    end_object = message.end_object;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a FETCH for " << track_name;
  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> track_publisher =
      session_->publisher_->GetTrack(track_name);
  if (!track_publisher.ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " rejected by the application: "
                    << track_publisher.status();
    SendFetchError(message.fetch_id, RequestErrorCode::kTrackDoesNotExist,
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
    SendFetchError(message.fetch_id, RequestErrorCode::kInvalidRange,
                   fetch->GetStatus().message());
    return;
  }
  auto published_fetch = std::make_unique<PublishedFetch>(
      message.fetch_id, session_, std::move(fetch));
  auto result = session_->incoming_fetches_.emplace(message.fetch_id,
                                                    std::move(published_fetch));
  if (!result.second) {  // Emplace failed.
    QUIC_DLOG(INFO) << ENDPOINT << "FETCH for " << track_name
                    << " could not be added to the session";
    SendFetchError(message.fetch_id, RequestErrorCode::kInternalError,
                   "Could not initialize FETCH state");
  }
  MoqtFetchTask* fetch_task = result.first->second->fetch_task();
  fetch_task->SetFetchResponseCallback(
      [this, request_id = message.fetch_id, fetch_start = start_object,
       fetch_end = Location(end_group, end_object.value_or(UINT64_MAX))](
          std::variant<MoqtFetchOk, MoqtFetchError> message) {
        if (!session_->incoming_fetches_.contains(request_id)) {
          return;  // FETCH was cancelled.
        }
        if (std::holds_alternative<MoqtFetchOk>(message)) {
          MoqtFetchOk& fetch_ok = std::get<MoqtFetchOk>(message);
          fetch_ok.subscribe_id = request_id;
          if (fetch_ok.largest_id < fetch_start ||
              fetch_ok.largest_id > fetch_end) {
            // TODO(martinduke): Add end_of_track to fetch_ok and check it's
            // larger than largest_id.
            QUIC_BUG(quic_bug_fetch_ok_status_error)
                << "FETCH_OK end or end_of_track is invalid";
            session_->Error(MoqtError::kInternalError, "FETCH_OK status error");
            return;
          }
          SendOrBufferMessage(session_->framer_.SerializeFetchOk(fetch_ok));
          return;
        }
        MoqtFetchError& fetch_error = std::get<MoqtFetchError>(message);
        fetch_error.subscribe_id = request_id;
        SendOrBufferMessage(session_->framer_.SerializeFetchError(fetch_error));
      });
  // Set a temporary new-object callback that creates a data stream. When
  // created, the stream visitor will replace this callback.
  fetch_task->SetObjectAvailableCallback(
      [this, send_order = SendOrderForFetch(message.subscriber_priority),
       request_id = message.fetch_id]() {
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
  RemoteTrack* track = session_->RemoteTrackById(message.subscribe_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_OK for "
                    << "subscribe_id = " << message.subscribe_id
                    << " but no track exists";
    // Subscription state might have been destroyed for internal reasons.
    return;
  }
  if (!track->is_fetch()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received FETCH_OK for a SUBSCRIBE");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_OK for subscribe_id = "
                  << message.subscribe_id << " " << track->full_track_name();
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
  fetch->OnFetchResult(message.largest_id, absl::OkStatus(),
                       [=, session = session_]() {
                         session->CancelFetch(message.subscribe_id);
                       });
}

void MoqtSession::ControlStream::OnFetchErrorMessage(
    const MoqtFetchError& message) {
  RemoteTrack* track = session_->RemoteTrackById(message.subscribe_id);
  if (track == nullptr) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received the FETCH_ERROR for "
                    << "subscribe_id = " << message.subscribe_id
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
                  << "subscribe_id = " << message.subscribe_id << " ("
                  << track->full_track_name() << ")"
                  << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
  absl::Status status =
      RequestErrorCodeToStatus(message.error_code, message.reason_phrase);
  fetch->OnFetchResult(Location(0, 0), status, nullptr);
  session_->upstream_by_id_.erase(message.subscribe_id);
}

void MoqtSession::ControlStream::OnRequestsBlockedMessage(
    const MoqtRequestsBlocked& message) {
  // TODO(martinduke): Derive logic for granting more subscribes.
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
  std::array<absl::string_view, 1> write_vector = {message.AsStringView()};
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
  QUICHE_BUG_IF(quic_bug_object_with_no_stream_type,
                !parser_.stream_type().has_value())
      << "Object delivered without a stream type";
  // Get a pointer to the upstream state.
  RemoteTrack* track = track_.GetIfAvailable();
  if (track == nullptr) {
    track = (*parser_.stream_type() == MoqtDataStreamType::kStreamHeaderFetch)
                // message.track_alias is actually a fetch ID for fetches.
                ? session_->RemoteTrackById(message.track_alias)
                : session_->RemoteTrackByAlias(message.track_alias);
    if (track == nullptr) {
      stream_->SendStopSending(kResetCodeSubscriptionGone);
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
    SubscribeRemoteTrack* subscribe = static_cast<SubscribeRemoteTrack*>(track);
    subscribe->OnObject(/*is_datagram=*/false);
    if (subscribe->visitor() != nullptr) {
      // TODO(martinduke): Send extension headers.
      subscribe->visitor()->OnObjectFragment(
          track->full_track_name(),
          Location{message.group_id, message.subgroup_id.value_or(0),
                   message.object_id},
          message.publisher_priority, message.object_status, payload,
          end_of_message);
    }
  } else {  // FETCH
    track->OnObjectOrOk();
    UpstreamFetch* fetch = static_cast<UpstreamFetch*>(track);
    UpstreamFetch::UpstreamFetchTask* task = fetch->task();
    if (task == nullptr) {
      // The application killed the FETCH.
      stream_->SendStopSending(kResetCodeSubscriptionGone);
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
  if (parser_.stream_type() == MoqtDataStreamType::kStreamHeaderFetch) {
    session_->upstream_by_id_.erase(*parser_.track_alias());
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
      parser_.stream_type() != MoqtDataStreamType::kStreamHeaderFetch) {
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
  if (parser_.stream_type() == MoqtDataStreamType::kStreamHeaderSubgroup) {
    parser_.ReadAllData();
  } else if (!knew_track_alias) {
    parser_.ReadTrackAlias();
  }
  if (!parser_.track_alias().has_value()) {
    return;
  }
  if (parser_.stream_type() == MoqtDataStreamType::kStreamHeaderSubgroup) {
    if (knew_track_alias) {
      return;
    }
    // This is a new stream for a subscribe. Notify the subscription.
    auto it = session_->subscribe_by_alias_.find(*parser_.track_alias());
    if (it == session_->subscribe_by_alias_.end()) {
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received object for a track with no SUBSCRIBE";
      // This is a not a session error because there might be an UNSUBSCRIBE in
      // flight.
      stream_->SendStopSending(kResetCodeSubscriptionGone);
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
    stream_->SendStopSending(kResetCodeSubscriptionGone);
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
      track_alias_(subscribe.track_alias),
      filter_type_(subscribe.filter_type),
      forward_(subscribe.forward),
      window_(SubscribeMessageToWindow(subscribe)),
      subscriber_priority_(subscribe.subscriber_priority),
      subscriber_delivery_order_(subscribe.group_order),
      monitoring_interface_(monitoring_interface) {
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckSupportKnown(
        subscribe.parameters.oack_window_size.has_value());
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
    lazily_initialized_stream_map_.emplace(
        track_publisher_->GetForwardingPreference());
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
  subscribe_ok.group_order = track_publisher_->GetDeliveryOrder();
  subscribe_ok.largest_location = largest_location;
  // TODO(martinduke): Support sending DELIVERY_TIMEOUT parameter as the
  // publisher.
  stream->SendOrBufferMessage(
      session_->framer_.SerializeSubscribeOk(subscribe_ok));
}

void MoqtSession::PublishedSubscription::OnSubscribeRejected(
    MoqtSubscribeErrorReason reason, std::optional<uint64_t> track_alias) {
  session_->GetControlStream()->SendSubscribeError(
      request_id_, reason.error_code, reason.reason_phrase,
      track_alias.value_or(track_alias_));
  session_->published_subscriptions_.erase(request_id_);
  // No class access below this line!
}

void MoqtSession::PublishedSubscription::OnNewObjectAvailable(
    Location sequence) {
  if (!InWindow(sequence)) {
    return;
  }
  if (reset_subgroups_.contains(
          Location{sequence.group, sequence.subgroup, 0})) {
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
      stream_map().GetStreamForSequence(sequence);
  webtransport::Stream* raw_stream = nullptr;
  if (stream_id.has_value()) {
    raw_stream = session_->session_->GetStreamById(*stream_id);
  } else {
    raw_stream = session_->OpenOrQueueDataStream(request_id_, sequence);
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

void MoqtSession::PublishedSubscription::OnNewFinAvailable(Location sequence) {
  if (!InWindow(sequence)) {
    return;
  }
  if (reset_subgroups_.contains(
          Location{sequence.group, sequence.subgroup, 0})) {
    // This subgroup has already been reset, ignore.
    return;
  }
  QUICHE_DCHECK_GE(sequence.group, first_active_group_);
  std::optional<webtransport::StreamId> stream_id =
      stream_map().GetStreamForSequence(sequence);
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
  stream->Fin(sequence);
}

void MoqtSession::PublishedSubscription::OnSubgroupAbandoned(
    Location sequence, webtransport::StreamErrorCode error_code) {
  if (!InWindow(sequence)) {
    return;
  }
  if (reset_subgroups_.contains(
          Location{sequence.group, sequence.subgroup, 0})) {
    // This subgroup has already been reset, ignore.
    return;
  }
  QUICHE_DCHECK_GE(sequence.group, first_active_group_);
  std::optional<webtransport::StreamId> stream_id =
      stream_map().GetStreamForSequence(sequence);
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
  for (webtransport::StreamId stream_id : streams) {
    webtransport::Stream* raw_stream =
        session_->session_->GetStreamById(stream_id);
    if (raw_stream == nullptr) {
      continue;
    }
    raw_stream->ResetWithUserCode(kResetCodeTimedOut);
  }
  first_active_group_ = std::max(first_active_group_, group_id + 1);
  absl::erase_if(reset_subgroups_, [&](const Location& sequence) {
    return sequence.group < first_active_group_;
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
    Location sequence) const {
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
                            sequence.group, sequence.subgroup, delivery_order);
}

// Returns the highest send order in the subscription.
void MoqtSession::PublishedSubscription::AddQueuedOutgoingDataStream(
    Location first_object) {
  std::optional<webtransport::SendOrder> start_send_order =
      queued_outgoing_data_streams_.empty()
          ? std::optional<webtransport::SendOrder>()
          : queued_outgoing_data_streams_.rbegin()->first;
  webtransport::SendOrder send_order = GetSendOrder(first_object);
  // Zero out the subscriber priority bits, since these will be added when
  // updating the session.
  queued_outgoing_data_streams_.emplace(
      UpdateSendOrderForSubscriberPriority(send_order, 0), first_object);
  if (!start_send_order.has_value()) {
    session_->UpdateQueuedSendOrder(request_id_, std::nullopt, send_order);
  } else if (*start_send_order < send_order) {
    session_->UpdateQueuedSendOrder(
        request_id_, FinalizeSendOrder(*start_send_order), send_order);
  }
}

Location MoqtSession::PublishedSubscription::NextQueuedOutgoingDataStream() {
  QUICHE_DCHECK(!queued_outgoing_data_streams_.empty());
  if (queued_outgoing_data_streams_.empty()) {
    return Location();
  }
  auto it = queued_outgoing_data_streams_.rbegin();
  webtransport::SendOrder old_send_order = FinalizeSendOrder(it->first);
  Location first_object = it->second;
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
  return first_object;
}

void MoqtSession::PublishedSubscription::OnDataStreamCreated(
    webtransport::StreamId id, Location start_sequence) {
  ++streams_opened_;
  stream_map().AddStream(start_sequence, id);
}
void MoqtSession::PublishedSubscription::OnDataStreamDestroyed(
    webtransport::StreamId id, Location end_sequence) {
  stream_map().RemoveStream(end_sequence, id);
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
    PublishedSubscription& subscription, Location first_object)
    : session_(session),
      stream_(stream),
      subscription_id_(subscription.request_id()),
      next_object_(first_object),
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
    it->second->OnDataStreamDestroyed(stream_->GetStreamId(), next_object_);
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
    it->second->OnStreamTimeout(stream_->next_object_);
  }
  stream_->stream_->ResetWithUserCode(kResetCodeTimedOut);
}

MoqtSession::PublishedSubscription*
MoqtSession::OutgoingDataStream::GetSubscriptionIfValid() {
  auto it = session_->published_subscriptions_.find(subscription_id_);
  if (it == session_->published_subscriptions_.end()) {
    stream_->ResetWithUserCode(kResetCodeSubscriptionGone);
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
  while (stream_->CanWrite()) {
    std::optional<PublishedObject> object =
        subscription.publisher().GetCachedObject(next_object_);
    if (!object.has_value()) {
      break;
    }
    if (!subscription.InWindow(next_object_)) {
      // It is possible that the next object became irrelevant due to a
      // SUBSCRIBE_UPDATE.  Close the stream if so.
      bool success = stream_->SendFin();
      QUICHE_BUG_IF(OutgoingDataStream_fin_due_to_update, !success)
          << "Writing FIN failed despite CanWrite() being true.";
      return;
    }
    quic::QuicTimeDelta delivery_timeout = subscription.delivery_timeout();
    if (!session_->alternate_delivery_timeout_ &&
        session_->callbacks_.clock->ApproximateNow() - object->arrival_time >
            delivery_timeout) {
      subscription.OnStreamTimeout(next_object_);
      stream_->ResetWithUserCode(kResetCodeTimedOut);
      return;
    }
    QUICHE_DCHECK(next_object_ <= object->sequence);
    MoqtTrackPublisher& publisher = subscription.publisher();
    QUICHE_DCHECK(DoesTrackStatusImplyHavingData(*publisher.GetTrackStatus()));
    MoqtForwardingPreference forwarding_preference =
        publisher.GetForwardingPreference();
    UpdateSendOrder(subscription);
    if (forwarding_preference == MoqtForwardingPreference::kDatagram) {
      QUICHE_BUG(quic_bug_SendObjects_for_Datagram)
          << "Datagram Track requesting SendObjects";
      return;
    }
    next_object_ = object->sequence.next();
    if (session_->WriteObjectToStream(
            stream_, subscription.track_alias(), *object,
            MoqtDataStreamType::kStreamHeaderSubgroup, !stream_header_written_,
            object->fin_after_this)) {
      stream_header_written_ = true;
      subscription.OnObjectSent(object->sequence);
    }
    if (object->fin_after_this && !delivery_timeout.IsInfinite() &&
        !session_->alternate_delivery_timeout_) {
      CreateAndSetAlarm(object->arrival_time + delivery_timeout);
    }
  }
}

void MoqtSession::OutgoingDataStream::Fin(Location last_object) {
  if (next_object_ <= last_object) {
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
                                      const PublishedObject& object,
                                      MoqtDataStreamType type,
                                      bool is_first_on_stream, bool fin) {
  QUICHE_DCHECK(stream->CanWrite());
  MoqtObject header;
  header.track_alias = id;
  header.group_id = object.sequence.group;
  header.subgroup_id = object.sequence.subgroup;
  header.object_id = object.sequence.object;
  header.publisher_priority = object.publisher_priority;
  header.object_status = object.status;
  header.payload_length = object.payload.length();

  quiche::QuicheBuffer serialized_header =
      framer_.SerializeObjectHeader(header, type, is_first_on_stream);
  // TODO(vasilvv): add a version of WebTransport write API that accepts
  // memslices so that we can avoid a copy here.
  std::array<absl::string_view, 2> write_vector = {
      serialized_header.AsStringView(), object.payload.AsStringView()};
  quiche::StreamWriteOptions options;
  options.set_send_fin(fin);
  absl::Status write_status = stream->Writev(write_vector, options);
  if (!write_status.ok()) {
    QUICHE_BUG(MoqtSession_WriteObjectToStream_write_failed)
        << "Writing into MoQT stream failed despite CanWrite() being true "
           "before; status: "
        << write_status;
    Error(MoqtError::kInternalError, "Data stream write error");
    return false;
  }

  QUIC_DVLOG(1) << "Stream " << stream->GetStreamId() << " successfully wrote "
                << object.sequence << ", fin = " << fin;
  return true;
}

void MoqtSession::CancelFetch(uint64_t subscribe_id) {
  if (is_closing_) {
    return;
  }
  // This is only called from the callback where UpstreamFetchTask has been
  // destroyed, so there is no need to notify the application.
  upstream_by_id_.erase(subscribe_id);
  ControlStream* stream = GetControlStream();
  if (stream == nullptr) {
    return;
  }
  MoqtFetchCancel message;
  message.subscribe_id = subscribe_id;
  stream->SendOrBufferMessage(framer_.SerializeFetchCancel(message));
  // The FETCH_CANCEL will cause a RESET_STREAM to return, which would be the
  // same as a STOP_SENDING. However, a FETCH_CANCEL works even if the stream
  // hasn't opened yet.
}

void MoqtSession::PublishedSubscription::SendDatagram(Location sequence) {
  std::optional<PublishedObject> object =
      track_publisher_->GetCachedObject(sequence);
  if (!object.has_value()) {
    QUICHE_BUG(PublishedSubscription_SendDatagram_object_not_in_cache)
        << "Got notification about an object that is not in the cache";
    return;
  }

  MoqtObject header;
  header.track_alias = track_alias();
  header.group_id = object->sequence.group;
  header.object_id = object->sequence.object;
  header.publisher_priority = object->publisher_priority;
  header.object_status = object->status;
  header.subgroup_id = std::nullopt;
  header.payload_length = object->payload.length();
  quiche::QuicheBuffer datagram = session_->framer_.SerializeObjectDatagram(
      header, object->payload.AsStringView());
  session_->session_->SendOrQueueDatagram(datagram.AsStringView());
  OnObjectSent(object->sequence);
}

void MoqtSession::OutgoingDataStream::UpdateSendOrder(
    PublishedSubscription& subscription) {
  stream_->SetPriority(
      webtransport::StreamPriority{/*send_group_id=*/kMoqtSendGroupId,
                                   subscription.GetSendOrder(next_object_)});
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
