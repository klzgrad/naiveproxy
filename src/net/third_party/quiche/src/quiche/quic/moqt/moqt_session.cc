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
#include <vector>


#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

namespace {

using ::quic::Perspective;

constexpr MoqtPriority kDefaultSubscriberPriority = 0x80;

// WebTransport lets applications split a session into multiple send groups
// that have equal weight for scheduling. We don't have a use for that, so the
// send group is always the same.
constexpr webtransport::SendGroupId kMoqtSendGroupId = 0;

bool PublisherHasData(const MoqtTrackPublisher& publisher) {
  absl::StatusOr<MoqtTrackStatusCode> status = publisher.GetTrackStatus();
  return status.ok() && DoesTrackStatusImplyHavingData(*status);
}

SubscribeWindow SubscribeMessageToWindow(const MoqtSubscribe& subscribe,
                                         MoqtTrackPublisher& publisher) {
  const FullSequence sequence = PublisherHasData(publisher)
                                    ? publisher.GetLargestSequence()
                                    : FullSequence{0, 0};
  switch (GetFilterType(subscribe)) {
    case MoqtFilterType::kLatestGroup:
      return SubscribeWindow(sequence.group, 0);
    case MoqtFilterType::kLatestObject:
      return SubscribeWindow(sequence.group, sequence.object);
    case MoqtFilterType::kAbsoluteStart:
      return SubscribeWindow(*subscribe.start_group, *subscribe.start_object);
    case MoqtFilterType::kAbsoluteRange:
      return SubscribeWindow(*subscribe.start_group, *subscribe.start_object,
                             *subscribe.end_group, *subscribe.end_object);
    case MoqtFilterType::kNone:
      QUICHE_BUG(MoqtSession_Subscription_invalid_filter_passed);
      return SubscribeWindow(0, 0);
  }
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
                         MoqtSessionCallbacks callbacks)
    : session_(session),
      parameters_(parameters),
      callbacks_(std::move(callbacks)),
      framer_(quiche::SimpleBufferAllocator::Get(), parameters.using_webtrans),
      publisher_(DefaultPublisher::GetInstance()),
      local_max_subscribe_id_(parameters.max_subscribe_id),
      liveness_token_(std::make_shared<Empty>()) {}

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
      .role = MoqtRole::kPubSub,
      .max_subscribe_id = parameters_.max_subscribe_id,
      .supports_object_ack = parameters_.support_object_acks,
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
  absl::string_view payload = ParseDatagram(datagram, message);
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "Received OBJECT message in datagram for subscribe_id "
                    << " for track alias " << message.track_alias
                    << " with sequence " << message.group_id << ":"
                    << message.object_id << " priority "
                    << message.publisher_priority << " length "
                    << payload.size();
  auto [full_track_name, visitor] = TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(
        full_track_name, FullSequence{message.group_id, 0, message.object_id},
        message.publisher_priority, message.object_status,
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

// TODO: Create state that allows ANNOUNCE_OK/ERROR on spurious namespaces to
// trigger session errors.
void MoqtSession::Announce(FullTrackName track_namespace,
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

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    RemoteTrack::Visitor* visitor,
                                    MoqtSubscribeParameters parameters) {
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  message.parameters = std::move(parameters);
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group,
                                    RemoteTrack::Visitor* visitor,
                                    MoqtSubscribeParameters parameters) {
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = end_group;
  message.end_object = std::nullopt;
  message.parameters = std::move(parameters);
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(const FullTrackName& name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group, uint64_t end_object,
                                    RemoteTrack::Visitor* visitor,
                                    MoqtSubscribeParameters parameters) {
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  if (end_group == start_group && end_object < start_object) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = end_group;
  message.end_object = end_object;
  message.parameters = std::move(parameters);
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentObject(const FullTrackName& name,
                                         RemoteTrack::Visitor* visitor,
                                         MoqtSubscribeParameters parameters) {
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  message.start_group = std::nullopt;
  message.start_object = std::nullopt;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  message.parameters = std::move(parameters);
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentGroup(const FullTrackName& name,
                                        RemoteTrack::Visitor* visitor,
                                        MoqtSubscribeParameters parameters) {
  MoqtSubscribe message;
  message.full_track_name = name;
  message.subscriber_priority = kDefaultSubscriberPriority;
  message.group_order = std::nullopt;
  // First object of current group.
  message.start_group = std::nullopt;
  message.start_object = 0;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  message.parameters = std::move(parameters);
  return Subscribe(message, visitor);
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
  subscribe_done.reason_phrase = reason_phrase;
  subscribe_done.final_id = subscription.largest_sent();
  SendControlMessage(framer_.SerializeSubscribeDone(subscribe_done));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_DONE message for "
                  << subscribe_id;
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

bool MoqtSession::Subscribe(MoqtSubscribe& message,
                            RemoteTrack::Visitor* visitor) {
  if (peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE to subscriber peer";
    return false;
  }
  // TODO(martinduke): support authorization info
  if (next_subscribe_id_ > peer_max_subscribe_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE with ID "
                    << next_subscribe_id_
                    << " which is greater than the maximum ID "
                    << peer_max_subscribe_id_;
    return false;
  }
  message.subscribe_id = next_subscribe_id_++;
  auto it = remote_track_aliases_.find(message.full_track_name);
  if (it != remote_track_aliases_.end()) {
    message.track_alias = it->second;
    if (message.track_alias >= next_remote_track_alias_) {
      next_remote_track_alias_ = message.track_alias + 1;
    }
  } else {
    message.track_alias = next_remote_track_alias_++;
  }
  if (SupportsObjectAck() && visitor != nullptr) {
    // Since we do not expose subscribe IDs directly in the API, instead wrap
    // the session and subscribe ID in a callback.
    visitor->OnCanAckObjects(absl::bind_front(&MoqtSession::SendObjectAck, this,
                                              message.subscribe_id));
  } else {
    QUICHE_DLOG_IF(WARNING, message.parameters.object_ack_window.has_value())
        << "Attempting to set object_ack_window on a connection that does not "
           "support it.";
    message.parameters.object_ack_window = std::nullopt;
  }
  SendControlMessage(framer_.SerializeSubscribe(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE message for "
                  << message.full_track_name;
  active_subscribes_.try_emplace(message.subscribe_id, message, visitor);
  return true;
}

webtransport::Stream* MoqtSession::OpenOrQueueDataStream(
    uint64_t subscription_id, FullSequence first_object) {
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
    PublishedSubscription& subscription, FullSequence first_object) {
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

void MoqtSession::OnCanCreateNewOutgoingUnidirectionalStream() {
  while (!subscribes_with_queued_outgoing_data_streams_.empty() &&
         session_->CanOpenNextOutgoingUnidirectionalStream()) {
    auto next = subscribes_with_queued_outgoing_data_streams_.rbegin();
    auto subscription = published_subscriptions_.find(next->subscription_id);
    if (subscription == published_subscriptions_.end()) {
      // Subscription no longer exists; delete the entry.
      subscribes_with_queued_outgoing_data_streams_.erase((++next).base());
      continue;
    }
    // Open the stream. The second argument pops the item from the
    // subscription's queue, which might update
    // subscribes_with_queued_outgoing_data_streams_.
    webtransport::Stream* stream =
        OpenDataStream(*subscription->second,
                       subscription->second->NextQueuedOutgoingDataStream());
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

void MoqtSession::GrantMoreSubscribes(uint64_t num_subscribes) {
  local_max_subscribe_id_ += num_subscribes;
  MoqtMaxSubscribeId message;
  message.max_subscribe_id = local_max_subscribe_id_;
  SendControlMessage(framer_.SerializeMaxSubscribeId(message));
}

std::pair<FullTrackName, RemoteTrack::Visitor*>
MoqtSession::TrackPropertiesFromAlias(const MoqtObject& message) {
  auto it = remote_tracks_.find(message.track_alias);
  if (it == remote_tracks_.end()) {
    ActiveSubscribe* subscribe = nullptr;
    // SUBSCRIBE_OK has not arrived yet, but deliver the object. Indexing
    // active_subscribes_ by track alias would make this faster if the
    // subscriber has tons of incomplete subscribes.
    for (auto& open_subscribe : active_subscribes_) {
      if (open_subscribe.second.message.track_alias == message.track_alias) {
        subscribe = &open_subscribe.second;
        break;
      }
    }
    if (subscribe == nullptr) {
      return std::pair<FullTrackName, RemoteTrack::Visitor*>(
          {FullTrackName{}, nullptr});
    }
    subscribe->received_object = true;
    if (subscribe->forwarding_preference.has_value()) {
      if (message.forwarding_preference != *subscribe->forwarding_preference) {
        Error(MoqtError::kProtocolViolation,
              "Forwarding preference changes mid-track");
        return std::pair<FullTrackName, RemoteTrack::Visitor*>(
            {FullTrackName{}, nullptr});
      }
    } else {
      subscribe->forwarding_preference = message.forwarding_preference;
    }
    return std::make_pair(subscribe->message.full_track_name,
                          subscribe->visitor);
  }
  RemoteTrack& track = it->second;
  if (!track.CheckForwardingPreference(message.forwarding_preference)) {
    // Incorrect forwarding preference.
    Error(MoqtError::kProtocolViolation,
          "Forwarding preference changes mid-track");
    return std::pair<FullTrackName, RemoteTrack::Visitor*>(
        {FullTrackName{}, nullptr});
  }
  return std::make_pair(track.full_track_name(), track.visitor());
}

template <class Parser>
static void ForwardStreamDataToParser(webtransport::Stream& stream,
                                      Parser& parser) {
  bool fin =
      quiche::ProcessAllReadableRegions(stream, [&](absl::string_view chunk) {
        parser.ProcessData(chunk, /*end_of_stream=*/false);
      });
  if (fin) {
    parser.ProcessData("", /*end_of_stream=*/true);
  }
}

MoqtSession::ControlStream::ControlStream(MoqtSession* session,
                                          webtransport::Stream* stream)
    : session_(session),
      stream_(stream),
      parser_(session->parameters_.using_webtrans, *this) {
  stream_->SetPriority(
      webtransport::StreamPriority{/*send_group_id=*/kMoqtSendGroupId,
                                   /*send_order=*/kMoqtControlStreamSendOrder});
}

void MoqtSession::ControlStream::OnCanRead() {
  ForwardStreamDataToParser(*stream_, parser_);
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
    session_->Error(MoqtError::kProtocolViolation,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  session_->peer_supports_object_ack_ = message.supports_object_ack;
  QUICHE_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  if (session_->parameters_.perspective == Perspective::IS_SERVER) {
    MoqtServerSetup response;
    response.selected_version = session_->parameters_.version;
    response.role = MoqtRole::kPubSub;
    response.max_subscribe_id = session_->parameters_.max_subscribe_id;
    response.supports_object_ack = session_->parameters_.support_object_acks;
    SendOrBufferMessage(session_->framer_.SerializeServerSetup(response));
    QUIC_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle role and path.
  if (message.max_subscribe_id.has_value()) {
    session_->peer_max_subscribe_id_ = *message.max_subscribe_id;
  }
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
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
  session_->peer_supports_object_ack_ = message.supports_object_ack;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle role and path.
  if (message.max_subscribe_id.has_value()) {
    session_->peer_max_subscribe_id_ = *message.max_subscribe_id;
  }
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
}

void MoqtSession::ControlStream::SendSubscribeError(
    const MoqtSubscribe& message, SubscribeErrorCode error_code,
    absl::string_view reason_phrase, uint64_t track_alias) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.subscribe_id = message.subscribe_id;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  subscribe_error.track_alias = track_alias;
  SendOrBufferMessage(
      session_->framer_.SerializeSubscribeError(subscribe_error));
}

void MoqtSession::ControlStream::OnSubscribeMessage(
    const MoqtSubscribe& message) {
  if (session_->peer_role_ == MoqtRole::kPublisher) {
    QUIC_DLOG(INFO) << ENDPOINT << "Publisher peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE from publisher");
    return;
  }
  if (message.subscribe_id > session_->local_max_subscribe_id_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received SUBSCRIBE with too large ID";
    session_->Error(MoqtError::kTooManySubscribes,
                    "Received SUBSCRIBE with too large ID");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE for "
                  << message.full_track_name;

  const FullTrackName& track_name = message.full_track_name;
  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> track_publisher =
      session_->publisher_->GetTrack(track_name);
  if (!track_publisher.ok()) {
    QUIC_DLOG(INFO) << ENDPOINT << "SUBSCRIBE for " << track_name
                    << " rejected by the application: "
                    << track_publisher.status();
    SendSubscribeError(message, SubscribeErrorCode::kTrackDoesNotExist,
                       track_publisher.status().message(), message.track_alias);
    return;
  }
  std::optional<FullSequence> largest_id;
  if (PublisherHasData(**track_publisher)) {
    largest_id = (*track_publisher)->GetLargestSequence();
  }
  if (message.start_group.has_value() && largest_id.has_value() &&
      *message.start_group < largest_id->group) {
    SendSubscribeError(message, SubscribeErrorCode::kInvalidRange,
                       "SUBSCRIBE starts in previous group",
                       message.track_alias);
    return;
  }
  MoqtDeliveryOrder delivery_order = (*track_publisher)->GetDeliveryOrder();

  MoqtPublishingMonitorInterface* monitoring = nullptr;
  auto monitoring_it =
      session_->monitoring_interfaces_for_published_tracks_.find(track_name);
  if (monitoring_it !=
      session_->monitoring_interfaces_for_published_tracks_.end()) {
    monitoring = monitoring_it->second;
    session_->monitoring_interfaces_for_published_tracks_.erase(monitoring_it);
  }

  if (session_->subscribed_track_names_.contains(track_name)) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Duplicate subscribe for track");
    return;
  }
  auto subscription = std::make_unique<MoqtSession::PublishedSubscription>(
      session_, *std::move(track_publisher), message, monitoring);
  auto [it, success] = session_->published_subscriptions_.emplace(
      message.subscribe_id, std::move(subscription));
  if (!success) {
    SendSubscribeError(message, SubscribeErrorCode::kInternalError,
                       "Duplicate subscribe ID", message.track_alias);
    return;
  }

  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.subscribe_id = message.subscribe_id;
  subscribe_ok.group_order = delivery_order;
  subscribe_ok.largest_id = largest_id;
  SendOrBufferMessage(session_->framer_.SerializeSubscribeOk(subscribe_ok));

  if (largest_id.has_value()) {
    it->second->Backfill();
  }
}

void MoqtSession::ControlStream::OnSubscribeOkMessage(
    const MoqtSubscribeOk& message) {
  auto it = session_->active_subscribes_.find(message.subscribe_id);
  if (it == session_->active_subscribes_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_OK for nonexistent subscribe");
    return;
  }
  MoqtSubscribe& subscribe = it->second.message;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                  << "subscribe_id = " << message.subscribe_id << " "
                  << subscribe.full_track_name;
  // Copy the Remote Track from session_->active_subscribes_ to
  // session_->remote_tracks_.
  RemoteTrack::Visitor* visitor = it->second.visitor;
  auto [track_iter, new_entry] = session_->remote_tracks_.try_emplace(
      subscribe.track_alias, subscribe.full_track_name, subscribe.track_alias,
      visitor);
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
    visitor->OnReply(subscribe.full_track_name, std::nullopt);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::ControlStream::OnSubscribeErrorMessage(
    const MoqtSubscribeError& message) {
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
                  << subscribe.full_track_name << ")"
                  << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  RemoteTrack::Visitor* visitor = it->second.visitor;
  if (message.error_code == SubscribeErrorCode::kRetryTrackAlias) {
    // Automatically resubscribe with new alias.
    session_->remote_track_aliases_[subscribe.full_track_name] =
        message.track_alias;
    session_->Subscribe(subscribe, visitor);
  } else if (visitor != nullptr) {
    visitor->OnReply(subscribe.full_track_name, message.reason_phrase);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::ControlStream::OnUnsubscribeMessage(
    const MoqtUnsubscribe& message) {
  session_->SubscribeIsDone(message.subscribe_id,
                            SubscribeDoneCode::kUnsubscribed, "");
}

void MoqtSession::ControlStream::OnSubscribeUpdateMessage(
    const MoqtSubscribeUpdate& message) {
  auto it = session_->published_subscriptions_.find(message.subscribe_id);
  if (it == session_->published_subscriptions_.end()) {
    return;
  }
  FullSequence start(message.start_group, message.start_object);
  std::optional<FullSequence> end;
  if (message.end_group.has_value()) {
    end = FullSequence(*message.end_group, message.end_object.has_value()
                                               ? *message.end_object
                                               : UINT64_MAX);
  }
  it->second->Update(start, end, message.subscriber_priority);
}

void MoqtSession::ControlStream::OnAnnounceMessage(
    const MoqtAnnounce& message) {
  if (session_->peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Subscriber peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE from Subscriber");
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

void MoqtSession::ControlStream::OnAnnounceOkMessage(
    const MoqtAnnounceOk& message) {
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_OK for nonexistent announce");
    return;
  }
  std::move(it->second)(message.track_namespace, std::nullopt);
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::ControlStream::OnAnnounceErrorMessage(
    const MoqtAnnounceError& message) {
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

void MoqtSession::ControlStream::OnAnnounceCancelMessage(
    const MoqtAnnounceCancel& message) {
  // TODO: notify the application about this.
}

void MoqtSession::ControlStream::OnMaxSubscribeIdMessage(
    const MoqtMaxSubscribeId& message) {
  if (session_->peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Subscriber peer sent MAX_SUBSCRIBE_ID";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received MAX_SUBSCRIBE_ID from Subscriber");
    return;
  }
  if (message.max_subscribe_id < session_->peer_max_subscribe_id_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Peer sent MAX_SUBSCRIBE_ID message with "
                       "lower value than previous";
    session_->Error(MoqtError::kProtocolViolation,
                    "MAX_SUBSCRIBE_ID message has lower value than previous");
    return;
  }
  session_->peer_max_subscribe_id_ = message.max_subscribe_id;
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
                  << " priority " << message.publisher_priority
                  << " forwarding_preference "
                  << MoqtForwardingPreferenceToString(
                         message.forwarding_preference)
                  << " length " << payload.size() << " length "
                  << message.payload_length << (end_of_message ? "F" : "");
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
  auto [full_track_name, visitor] = session_->TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(
        full_track_name,
        FullSequence{message.group_id, message.subgroup_id.value_or(0),
                     message.object_id},
        message.publisher_priority, message.object_status,
        message.forwarding_preference, payload, end_of_message);
  }
  partial_object_.clear();
}

void MoqtSession::IncomingDataStream::OnCanRead() {
  ForwardStreamDataToParser(*stream_, parser_);
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
    : subscription_id_(subscribe.subscribe_id),
      session_(session),
      track_publisher_(track_publisher),
      track_alias_(subscribe.track_alias),
      window_(SubscribeMessageToWindow(subscribe, *track_publisher)),
      subscriber_priority_(subscribe.subscriber_priority),
      subscriber_delivery_order_(subscribe.group_order),
      monitoring_interface_(monitoring_interface) {
  track_publisher->AddObjectListener(this);
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckSupportKnown(
        subscribe.parameters.object_ack_window.has_value());
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
    FullSequence start, std::optional<FullSequence> end,
    MoqtPriority subscriber_priority) {
  window_.UpdateStartEnd(start, end);
  subscriber_priority_ = subscriber_priority;
  // TODO: update priority of all data streams that are currently open.

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
  session_->UpdateQueuedSendOrder(subscription_id_, old_send_order,
                                  FinalizeSendOrder(old_send_order));
};

void MoqtSession::PublishedSubscription::OnNewObjectAvailable(
    FullSequence sequence) {
  if (!window_.InWindow(sequence)) {
    return;
  }

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
    raw_stream = session_->OpenOrQueueDataStream(subscription_id_, sequence);
  }
  if (raw_stream == nullptr) {
    return;
  }

  OutgoingDataStream* stream =
      static_cast<OutgoingDataStream*>(raw_stream->visitor());
  stream->SendObjects(*this);
}

void MoqtSession::PublishedSubscription::OnTrackPublisherGone() {
  session_->SubscribeIsDone(subscription_id_, SubscribeDoneCode::kGoingAway,
                            "Publisher is gone");
}

void MoqtSession::PublishedSubscription::Backfill() {
  const FullSequence start = window_.start();
  const FullSequence end = track_publisher_->GetLargestSequence();
  const MoqtForwardingPreference preference =
      track_publisher_->GetForwardingPreference();

  absl::flat_hash_set<ReducedSequenceIndex> already_opened;
  std::vector<FullSequence> objects =
      track_publisher_->GetCachedObjectsInRange(start, end);
  QUICHE_DCHECK(absl::c_is_sorted(objects));
  for (FullSequence sequence : objects) {
    auto [it, was_missing] =
        already_opened.insert(ReducedSequenceIndex(sequence, preference));
    if (!was_missing) {
      // For every stream mapping unit present, we only need to notify of the
      // earliest object on it, since the stream itself will pull the rest.
      continue;
    }
    OnNewObjectAvailable(sequence);
  }
}

std::vector<webtransport::StreamId>
MoqtSession::PublishedSubscription::GetAllStreams() const {
  if (!lazily_initialized_stream_map_.has_value()) {
    return {};
  }
  return lazily_initialized_stream_map_->GetAllStreams();
}

webtransport::SendOrder MoqtSession::PublishedSubscription::GetSendOrder(
    FullSequence sequence) const {
  MoqtForwardingPreference forwarding_preference =
      track_publisher_->GetForwardingPreference();

  MoqtPriority publisher_priority = track_publisher_->GetPublisherPriority();
  MoqtDeliveryOrder delivery_order = subscriber_delivery_order().value_or(
      track_publisher_->GetDeliveryOrder());
  switch (forwarding_preference) {
    case MoqtForwardingPreference::kTrack:
      return SendOrderForStream(subscriber_priority_, publisher_priority,
                                /*group_id=*/0, delivery_order);
      break;
    case MoqtForwardingPreference::kSubgroup:
      return SendOrderForStream(subscriber_priority_, publisher_priority,
                                sequence.group, sequence.subgroup,
                                delivery_order);
      break;
    case MoqtForwardingPreference::kDatagram:
      QUICHE_NOTREACHED();
      return 0;
  }
}

// Returns the highest send order in the subscription.
void MoqtSession::PublishedSubscription::AddQueuedOutgoingDataStream(
    FullSequence first_object) {
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
    session_->UpdateQueuedSendOrder(subscription_id_, std::nullopt, send_order);
  } else if (*start_send_order < send_order) {
    session_->UpdateQueuedSendOrder(
        subscription_id_, FinalizeSendOrder(*start_send_order), send_order);
  }
}

FullSequence
MoqtSession::PublishedSubscription::NextQueuedOutgoingDataStream() {
  QUICHE_DCHECK(!queued_outgoing_data_streams_.empty());
  if (queued_outgoing_data_streams_.empty()) {
    return FullSequence();
  }
  auto it = queued_outgoing_data_streams_.rbegin();
  webtransport::SendOrder old_send_order = FinalizeSendOrder(it->first);
  FullSequence first_object = it->second;
  // converting a reverse iterator to an iterator involves incrementing it and
  // then taking base().
  queued_outgoing_data_streams_.erase((++it).base());
  if (queued_outgoing_data_streams_.empty()) {
    session_->UpdateQueuedSendOrder(subscription_id_, old_send_order,
                                    std::nullopt);
  } else {
    webtransport::SendOrder new_send_order =
        FinalizeSendOrder(queued_outgoing_data_streams_.rbegin()->first);
    if (old_send_order != new_send_order) {
      session_->UpdateQueuedSendOrder(subscription_id_, old_send_order,
                                      new_send_order);
    }
  }
  return first_object;
}

void MoqtSession::PublishedSubscription::OnDataStreamCreated(
    webtransport::StreamId id, FullSequence start_sequence) {
  stream_map().AddStream(start_sequence, id);
}
void MoqtSession::PublishedSubscription::OnDataStreamDestroyed(
    webtransport::StreamId id, FullSequence end_sequence) {
  stream_map().RemoveStream(end_sequence, id);
}

void MoqtSession::PublishedSubscription::OnObjectSent(FullSequence sequence) {
  if (largest_sent_.has_value()) {
    largest_sent_ = std::max(*largest_sent_, sequence);
  } else {
    largest_sent_ = sequence;
  }
  // TODO: send SUBSCRIBE_DONE if the subscription is done.
}

MoqtSession::OutgoingDataStream::OutgoingDataStream(
    MoqtSession* session, webtransport::Stream* stream,
    PublishedSubscription& subscription, FullSequence first_object)
    : session_(session),
      stream_(stream),
      subscription_id_(subscription.subscription_id()),
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
    SendNextObject(subscription, *std::move(object));
  }
}

void MoqtSession::OutgoingDataStream::SendNextObject(
    PublishedSubscription& subscription, PublishedObject object) {
  QUICHE_DCHECK(next_object_ <= object.sequence);
  QUICHE_DCHECK(stream_->CanWrite());

  MoqtTrackPublisher& publisher = subscription.publisher();
  QUICHE_DCHECK(DoesTrackStatusImplyHavingData(*publisher.GetTrackStatus()));
  MoqtForwardingPreference forwarding_preference =
      publisher.GetForwardingPreference();

  UpdateSendOrder(subscription);

  MoqtObject header;
  header.track_alias = subscription.track_alias();
  header.group_id = object.sequence.group;
  header.object_id = object.sequence.object;
  header.publisher_priority = publisher.GetPublisherPriority();
  header.object_status = object.status;
  header.forwarding_preference = forwarding_preference;
  // TODO(martinduke): send values other than 0.
  header.subgroup_id =
      (forwarding_preference == MoqtForwardingPreference::kSubgroup)
          ? 0
          : std::optional<uint64_t>();
  header.payload_length = object.payload.length();

  quiche::QuicheBuffer serialized_header =
      session_->framer_.SerializeObjectHeader(
          header, GetMessageTypeForForwardingPreference(forwarding_preference),
          !stream_header_written_);
  bool fin = false;
  switch (forwarding_preference) {
    case MoqtForwardingPreference::kTrack:
      if (object.status == MoqtObjectStatus::kEndOfGroup ||
          object.status == MoqtObjectStatus::kGroupDoesNotExist) {
        ++next_object_.group;
        next_object_.object = 0;
      } else {
        next_object_.object = header.object_id + 1;
      }
      fin = object.status == MoqtObjectStatus::kEndOfTrack ||
            !subscription.InWindow(next_object_);
      break;

    case MoqtForwardingPreference::kSubgroup:
      // TODO(martinduke): EndOfGroup and EndOfTrack implies the ability to
      // close other streams/subgroups. PublishedObject should contain a boolean
      // if the stream is safe to close.
      next_object_.object = header.object_id + 1;
      fin = object.status == MoqtObjectStatus::kEndOfTrack ||
            object.status == MoqtObjectStatus::kEndOfGroup ||
            object.status == MoqtObjectStatus::kEndOfSubgroup ||
            object.status == MoqtObjectStatus::kGroupDoesNotExist ||
            !subscription.InWindow(next_object_);
      break;

    case MoqtForwardingPreference::kDatagram:
      QUICHE_NOTREACHED();
      break;
  }

  // TODO(vasilvv): add a version of WebTransport write API that accepts
  // memslices so that we can avoid a copy here.
  std::array<absl::string_view, 2> write_vector = {
      serialized_header.AsStringView(), object.payload.AsStringView()};
  quiche::StreamWriteOptions options;
  options.set_send_fin(fin);
  absl::Status write_status = stream_->Writev(write_vector, options);
  if (!write_status.ok()) {
    QUICHE_BUG(MoqtSession_SendNextObject_write_failed)
        << "Writing into MoQT stream failed despite CanWrite() being true "
           "before; status: "
        << write_status;
    session_->Error(MoqtError::kInternalError, "Data stream write error");
    return;
  }

  QUIC_DVLOG(1) << "Stream " << stream_->GetStreamId() << " successfully wrote "
                << object.sequence << ", fin = " << fin
                << ", next: " << next_object_;

  stream_header_written_ = true;
  subscription.OnObjectSent(object.sequence);
}

void MoqtSession::PublishedSubscription::SendDatagram(FullSequence sequence) {
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
  header.publisher_priority = track_publisher_->GetPublisherPriority();
  header.object_status = object->status;
  header.forwarding_preference = MoqtForwardingPreference::kDatagram;
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

}  // namespace moqt
