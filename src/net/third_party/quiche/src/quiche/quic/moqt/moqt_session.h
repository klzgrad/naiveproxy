// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
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
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/session_namespace_tree.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtSessionPeer;
}

inline constexpr MoqtPriority kDefaultSubscriberPriority = 0x80;
inline constexpr quic::QuicTimeDelta kDefaultGoAwayTimeout =
    quic::QuicTime::Delta::FromSeconds(10);

struct SubscriptionWithQueuedStream {
  webtransport::SendOrder send_order;
  uint64_t subscription_id;

  auto operator<=>(const SubscriptionWithQueuedStream& other) const = default;
};

// MoqtPublishingMonitorInterface allows a publisher monitor the delivery
// progress for a single individual subscriber.
class MoqtPublishingMonitorInterface {
 public:
  virtual ~MoqtPublishingMonitorInterface() = default;

  virtual void OnObjectAckSupportKnown(
      std::optional<quic::QuicTimeDelta> time_window) = 0;
  virtual void OnObjectAckReceived(uint64_t group_id, uint64_t object_id,
                                   quic::QuicTimeDelta delta_from_deadline) = 0;
};

class QUICHE_EXPORT MoqtSession : public MoqtSessionInterface,
                                  public webtransport::SessionVisitor {
 public:
  MoqtSession(webtransport::Session* session, MoqtSessionParameters parameters,
              std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
              MoqtSessionCallbacks callbacks = MoqtSessionCallbacks());
  ~MoqtSession() {
    is_closing_ = true;
    if (goaway_timeout_alarm_ != nullptr) {
      goaway_timeout_alarm_->PermanentCancel();
    }
    for (const TrackNamespace& track_namespace :
         incoming_subscribe_namespace_.GetSubscribedNamespaces()) {
      callbacks_.incoming_subscribe_namespace_callback(track_namespace,
                                                       std::nullopt, nullptr);
    }
    for (const TrackNamespace& track_namespace : incoming_publish_namespaces_) {
      callbacks_.incoming_publish_namespace_callback(track_namespace,
                                                     std::nullopt, nullptr);
    }
    std::move(callbacks_.session_deleted_callback)();
  }

  // webtransport::SessionVisitor implementation.
  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode,
                       const std::string&) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view datagram) override;
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override;

  quic::Perspective perspective() const { return parameters_.perspective; }

  // Returns true if message was sent.
  bool SubscribeNamespace(TrackNamespace track_namespace,
                          MoqtOutgoingSubscribeNamespaceCallback callback,
                          VersionSpecificParameters parameters);
  bool UnsubscribeNamespace(TrackNamespace track_namespace);

  // Allows the subscriber to declare it will not subscribe to |track_namespace|
  // anymore.
  void CancelPublishNamespace(TrackNamespace track_namespace,
                              RequestErrorCode code,
                              absl::string_view reason_phrase);

  // MoqtSessionInterface implementation.
  MoqtSessionCallbacks& callbacks() override { return callbacks_; }
  void Error(MoqtError code, absl::string_view error) override;
  bool SubscribeAbsolute(const FullTrackName& name, uint64_t start_group,
                         uint64_t start_object, SubscribeVisitor* visitor,
                         VersionSpecificParameters parameters) override;
  bool SubscribeAbsolute(const FullTrackName& name, uint64_t start_group,
                         uint64_t start_object, uint64_t end_group,
                         SubscribeVisitor* visitor,
                         VersionSpecificParameters parameters) override;
  bool SubscribeCurrentObject(const FullTrackName& name,
                              SubscribeVisitor* visitor,
                              VersionSpecificParameters parameters) override;
  bool SubscribeNextGroup(const FullTrackName& name, SubscribeVisitor* visitor,
                          VersionSpecificParameters parameters) override;
  bool SubscribeUpdate(const FullTrackName& name, std::optional<Location> start,
                       std::optional<uint64_t> end_group,
                       std::optional<MoqtPriority> subscriber_priority,
                       std::optional<bool> forward,
                       VersionSpecificParameters parameters) override;
  void Unsubscribe(const FullTrackName& name) override;
  bool Fetch(const FullTrackName& name, FetchResponseCallback callback,
             Location start, uint64_t end_group,
             std::optional<uint64_t> end_object, MoqtPriority priority,
             std::optional<MoqtDeliveryOrder> delivery_order,
             VersionSpecificParameters parameters) override;
  bool RelativeJoiningFetch(const FullTrackName& name,
                            SubscribeVisitor* visitor,
                            uint64_t num_previous_groups,
                            VersionSpecificParameters parameters) override;
  bool RelativeJoiningFetch(const FullTrackName& name,
                            SubscribeVisitor* visitor,
                            FetchResponseCallback callback,
                            uint64_t num_previous_groups, MoqtPriority priority,
                            std::optional<MoqtDeliveryOrder> delivery_order,
                            VersionSpecificParameters parameters) override;
  void PublishNamespace(TrackNamespace track_namespace,
                        MoqtOutgoingPublishNamespaceCallback callback,
                        VersionSpecificParameters parameters) override;
  bool PublishNamespaceDone(TrackNamespace track_namespace) override;
  quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() override {
    return weak_ptr_factory_.Create();
  }

  // Send a GOAWAY message to the peer. |new_session_uri| must be empty if
  // called by the client.
  void GoAway(absl::string_view new_session_uri);

  webtransport::Session* session() { return session_; }

  MoqtPublisher* publisher() { return publisher_; }
  void set_publisher(MoqtPublisher* publisher) { publisher_ = publisher; }
  bool support_object_acks() const { return parameters_.support_object_acks; }
  void set_support_object_acks(bool value) {
    QUICHE_DCHECK(!control_stream_.has_value())
        << "support_object_acks needs to be set before handshake";
    parameters_.support_object_acks = value;
  }

  // Assigns a monitoring interface for a specific track subscription that is
  // expected to happen in the future.  `interface` will be only used for a
  // single subscription, and it must outlive the session.
  void SetMonitoringInterfaceForTrack(
      FullTrackName track, MoqtPublishingMonitorInterface* interface) {
    monitoring_interfaces_for_published_tracks_.emplace(std::move(track),
                                                        interface);
  }

  void Close() { session_->CloseSession(0, "Application closed"); }

  // Tells the session that the highest send order for pending streams in a
  // subscription has changed. If |old_send_order| is nullopt, this is the
  // first pending stream. If |new_send_order| is nullopt, the subscription
  // has no pending streams anymore.
  void UpdateQueuedSendOrder(
      uint64_t subscribe_id,
      std::optional<webtransport::SendOrder> old_send_order,
      std::optional<webtransport::SendOrder> new_send_order);

  void GrantMoreRequests(uint64_t num_requests);

  void UseAlternateDeliveryTimeout() { alternate_delivery_timeout_ = true; }

  MoqtTraceRecorder& trace_recorder() { return trace_recorder_; }

 private:
  friend class test::MoqtSessionPeer;

  struct Empty {};

  struct NewStreamParameters {
    DataStreamIndex index;
    uint64_t first_object;
    MoqtPriority publisher_priority;

    NewStreamParameters(uint64_t group, uint64_t subgroup,
                        uint64_t first_object, MoqtPriority publisher_priority)
        : index(group, subgroup),
          first_object(first_object),
          publisher_priority(publisher_priority) {}
  };

  class QUICHE_EXPORT ControlStream : public webtransport::StreamVisitor,
                                      public MoqtControlParserVisitor {
   public:
    ControlStream(MoqtSession* session, webtransport::Stream* stream);

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override;
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override;
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override {}

    // MoqtControlParserVisitor implementation.
    void OnClientSetupMessage(const MoqtClientSetup& message) override;
    void OnServerSetupMessage(const MoqtServerSetup& message) override;
    void OnSubscribeMessage(const MoqtSubscribe& message) override;
    void OnSubscribeOkMessage(const MoqtSubscribeOk& message) override;
    void OnSubscribeErrorMessage(const MoqtSubscribeError& message) override;
    void OnUnsubscribeMessage(const MoqtUnsubscribe& message) override;
    // There is no state to update for SUBSCRIBE_DONE.
    void OnPublishDoneMessage(const MoqtPublishDone& /*message*/) override;
    void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) override;
    void OnPublishNamespaceMessage(
        const MoqtPublishNamespace& message) override;
    void OnPublishNamespaceOkMessage(
        const MoqtPublishNamespaceOk& message) override;
    void OnPublishNamespaceErrorMessage(
        const MoqtPublishNamespaceError& message) override;
    void OnPublishNamespaceDoneMessage(
        const MoqtPublishNamespaceDone& /*message*/) override;
    void OnPublishNamespaceCancelMessage(
        const MoqtPublishNamespaceCancel& message) override;
    void OnTrackStatusMessage(const MoqtTrackStatus& message) override;
    void OnTrackStatusOkMessage(const MoqtTrackStatusOk& /*message*/) override {
    }
    void OnTrackStatusErrorMessage(
        const MoqtTrackStatusError& /*message*/) override {}
    void OnGoAwayMessage(const MoqtGoAway& /*message*/) override;
    void OnSubscribeNamespaceMessage(
        const MoqtSubscribeNamespace& message) override;
    void OnSubscribeNamespaceOkMessage(
        const MoqtSubscribeNamespaceOk& message) override;
    void OnSubscribeNamespaceErrorMessage(
        const MoqtSubscribeNamespaceError& message) override;
    void OnUnsubscribeNamespaceMessage(
        const MoqtUnsubscribeNamespace& message) override;
    void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) override;
    void OnFetchMessage(const MoqtFetch& message) override;
    void OnFetchCancelMessage(const MoqtFetchCancel& /*message*/) override {}
    void OnFetchOkMessage(const MoqtFetchOk& message) override;
    void OnFetchErrorMessage(const MoqtFetchError& message) override;
    void OnRequestsBlockedMessage(const MoqtRequestsBlocked& message) override;
    void OnPublishMessage(const MoqtPublish& message) override;
    void OnPublishOkMessage(const MoqtPublishOk& /*message*/) override {}
    void OnPublishErrorMessage(const MoqtPublishError& /*message*/) override {}
    void OnObjectAckMessage(const MoqtObjectAck& message) override {
      auto subscription_it =
          session_->published_subscriptions_.find(message.subscribe_id);
      if (subscription_it == session_->published_subscriptions_.end()) {
        return;
      }
      subscription_it->second->ProcessObjectAck(message);
    }
    void OnParsingError(MoqtError error_code,
                        absl::string_view reason) override;

    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    webtransport::Stream* stream() const { return stream_; }

    // Sends a control message, or buffers it if there is insufficient flow
    // control credit.
    void SendOrBufferMessage(quiche::QuicheBuffer message, bool fin = false);

    void SendSubscribeError(uint64_t request_id, RequestErrorCode error_code,
                            absl::string_view reason_phrase);

   private:
    friend class test::MoqtSessionPeer;
    void SendFetchError(uint64_t request_id, RequestErrorCode error_code,
                        absl::string_view error_reason);

    MoqtSession* session_;
    webtransport::Stream* stream_;
    MoqtControlParser parser_;
  };
  class QUICHE_EXPORT IncomingDataStream : public webtransport::StreamVisitor,
                                           public MoqtDataParserVisitor {
   public:
    IncomingDataStream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session), stream_(stream), parser_(stream, this) {}
    ~IncomingDataStream();

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override;
    void OnCanWrite() override {}
    void OnResetStreamReceived(webtransport::StreamErrorCode) override {}
    void OnStopSendingReceived(
        webtransport::StreamErrorCode /*error*/) override {}
    void OnWriteSideInDataRecvdState() override {}

    // MoqtParserVisitor implementation.
    // TODO: Handle a stream FIN.
    void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                         bool end_of_message) override;
    void OnFin() override { fin_received_ = true; }
    void OnParsingError(MoqtError error_code,
                        absl::string_view reason) override;

    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    webtransport::Stream* stream() const { return stream_; }

    void MaybeReadOneObject();

   private:
    friend class test::MoqtSessionPeer;
    void OnControlMessageReceived();

    uint64_t next_object_id_ = 0;
    bool no_more_objects_ = false;  // EndOfGroup or EndOfTrack was received.
    std::optional<DataStreamIndex> index_;  // Only set for subscribe.
    bool fin_received_ = false;
    MoqtSession* session_;
    webtransport::Stream* stream_;
    // Once the subscribe ID is identified, set it here.
    quiche::QuicheWeakPtr<RemoteTrack> track_;
    MoqtDataParser parser_;
    std::string partial_object_;
  };
  // Represents a record for a single subscription to a local track that is
  // being sent to the peer.
  class PublishedSubscription : public MoqtObjectListener {
   public:
    PublishedSubscription(MoqtSession* session,
                          std::shared_ptr<MoqtTrackPublisher> track_publisher,
                          const MoqtSubscribe& subscribe,
                          MoqtPublishingMonitorInterface* monitoring_interface);
    // TODO(martinduke): Immediately reset all the streams.
    ~PublishedSubscription();

    PublishedSubscription(const PublishedSubscription&) = delete;
    PublishedSubscription(PublishedSubscription&&) = delete;
    PublishedSubscription& operator=(const PublishedSubscription&) = delete;
    PublishedSubscription& operator=(PublishedSubscription&&) = delete;

    uint64_t request_id() const { return request_id_; }
    MoqtTrackPublisher& publisher() { return *track_publisher_; }
    std::optional<uint64_t> track_alias() const { return track_alias_; }
    std::optional<Location> largest_sent() const { return largest_sent_; }
    MoqtPriority subscriber_priority() const { return subscriber_priority_; }
    std::optional<MoqtDeliveryOrder> subscriber_delivery_order() const {
      return subscriber_delivery_order_;
    }
    void set_subscriber_priority(MoqtPriority priority);

    // MoqtObjectListener implementation.
    void OnSubscribeAccepted() override;
    void OnSubscribeRejected(MoqtSubscribeErrorReason reason) override;
    // This is only called for objects that have just arrived.
    void OnNewObjectAvailable(Location location, uint64_t subgroup,
                              MoqtPriority publisher_priority) override;
    void OnTrackPublisherGone() override;
    void OnNewFinAvailable(Location location, uint64_t subgroup) override;
    void OnSubgroupAbandoned(uint64_t group, uint64_t subgroup,
                             webtransport::StreamErrorCode error_code) override;
    void OnGroupAbandoned(uint64_t group_id) override;
    void ProcessObjectAck(const MoqtObjectAck& message) {
      if (monitoring_interface_ == nullptr) {
        return;
      }
      monitoring_interface_->OnObjectAckReceived(
          message.group_id, message.object_id, message.delta_from_deadline);
    }

    // Updates the window and other properties of the subscription in question.
    void Update(Location start, std::optional<uint64_t> end,
                MoqtPriority subscriber_priority);
    // Checks if the specified sequence is within the window of this
    // subscription.
    bool InWindow(Location sequence) {
      return forward_ && window_.has_value() && window_->InWindow(sequence);
    }
    bool GroupInWindow(uint64_t group) {
      return forward_ && window_.has_value() && window_->GroupInWindow(group);
    }
    Location GetWindowStart() const {
      QUICHE_CHECK(window_.has_value());
      return window_->start();
    }
    MoqtFilterType filter_type() const { return filter_type_; }

    void OnDataStreamCreated(webtransport::StreamId id,
                             DataStreamIndex start_sequence);
    void OnDataStreamDestroyed(webtransport::StreamId id,
                               DataStreamIndex end_sequence);
    void OnObjectSent(Location sequence);

    std::vector<webtransport::StreamId> GetAllStreams() const;

    webtransport::SendOrder GetSendOrder(Location sequence, uint64_t subgroup,
                                         MoqtPriority publisher_priority) const;

    void AddQueuedOutgoingDataStream(const NewStreamParameters& parameters);
    // Pops the pending outgoing data stream, with the highest send order.
    // The session keeps track of which subscribes have pending streams. This
    // function will trigger a QUICHE_DCHECK if called when there are no pending
    // streams.
    NewStreamParameters NextQueuedOutgoingDataStream();

    quic::QuicTimeDelta delivery_timeout() const { return delivery_timeout_; }
    void set_delivery_timeout(quic::QuicTimeDelta timeout) {
      delivery_timeout_ = timeout;
    }

    void OnStreamTimeout(DataStreamIndex index) {
      reset_subgroups_.insert(index);
      if (session_->alternate_delivery_timeout_) {
        first_active_group_ = std::max(first_active_group_, index.group + 1);
      }
    }

    uint64_t first_active_group() const { return first_active_group_; }

    absl::flat_hash_set<DataStreamIndex>& reset_subgroups() {
      return reset_subgroups_;
    }

    uint64_t streams_opened() const { return streams_opened_; }

   private:
    friend class test::MoqtSessionPeer;
    SendStreamMap& stream_map();
    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    void SendDatagram(Location sequence);
    webtransport::SendOrder FinalizeSendOrder(
        webtransport::SendOrder send_order) {
      return UpdateSendOrderForSubscriberPriority(send_order,
                                                  subscriber_priority_);
    }

    MoqtSession* session_;
    std::shared_ptr<MoqtTrackPublisher> track_publisher_;
    uint64_t request_id_;
    std::optional<const uint64_t> track_alias_;
    MoqtFilterType filter_type_;
    bool forward_;
    // If window_ is nullopt, any arriving objects are ignored. This could be
    // because forward=0, or because the subscription is waiting for a
    // SUBSCRIBE_OK and doesn't know what the window should be yet.
    std::optional<SubscribeWindow> window_;
    MoqtPriority subscriber_priority_;
    uint64_t streams_opened_ = 0;

    // The subscription will ignore any groups with a lower ID, so it doesn't
    // need to track reset subgroups.
    uint64_t first_active_group_ = 0;
    // If a stream has been reset due to delivery timeout, do not open a new
    // stream if more object arrive for it.
    absl::flat_hash_set<DataStreamIndex> reset_subgroups_;
    // The min of DELIVERY_TIMEOUT from SUBSCRIBE and SUBSCRIBE_OK.
    quic::QuicTimeDelta delivery_timeout_ = quic::QuicTimeDelta::Infinite();

    std::optional<MoqtDeliveryOrder> subscriber_delivery_order_;
    MoqtPublishingMonitorInterface* monitoring_interface_;
    // Largest sequence number ever sent via this subscription.
    std::optional<Location> largest_sent_;
    // Should be almost always accessed via `stream_map()`.
    std::optional<SendStreamMap> lazily_initialized_stream_map_;
    // Store the send order of queued outgoing data streams. Use a
    // subscriber_priority_ of zero to avoid having to update it, and call
    // FinalizeSendOrder() whenever delivering it to the MoqtSession.
    absl::btree_multimap<webtransport::SendOrder, NewStreamParameters>
        queued_outgoing_data_streams_;
  };
  class QUICHE_EXPORT OutgoingDataStream : public webtransport::StreamVisitor {
   public:
    OutgoingDataStream(MoqtSession* session, webtransport::Stream* stream,
                       PublishedSubscription& subscription,
                       const NewStreamParameters& parameters);
    ~OutgoingDataStream();

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override {}
    void OnCanWrite() override;
    void OnResetStreamReceived(
        webtransport::StreamErrorCode /*error*/) override {}
    void OnStopSendingReceived(
        webtransport::StreamErrorCode /*error*/) override {}
    void OnWriteSideInDataRecvdState() override {}

    class DeliveryTimeoutDelegate
        : public quic::QuicAlarm::DelegateWithoutContext {
     public:
      explicit DeliveryTimeoutDelegate(OutgoingDataStream* stream)
          : stream_(stream) {}
      void OnAlarm() override;

     private:
      OutgoingDataStream* stream_;
    };

    webtransport::Stream* stream() const { return stream_; }

    // Sends objects on the stream, starting with `next_object_`, until the
    // stream becomes write-blocked or closed.
    void SendObjects(PublishedSubscription& subscription);

    // Sends a pure FIN on the stream, if the last object sent matches
    // |last_object|. Otherwise, does nothing.
    void Fin(Location last_object);

    // Recomputes the send order and updates it for the associated stream.
    void UpdateSendOrder(PublishedSubscription& subscription);

    // Creates and sets an alarm for the given deadline. Does nothing if the
    // alarm is already created.
    void CreateAndSetAlarm(quic::QuicTime deadline);

    DataStreamIndex index() const { return index_; }

   private:
    friend class test::MoqtSessionPeer;
    friend class DeliveryTimeoutDelegate;

    // Checks whether the associated subscription is still valid; if not, resets
    // the stream and returns nullptr.
    PublishedSubscription* GetSubscriptionIfValid();

    MoqtSession* session_;
    webtransport::Stream* stream_;
    uint64_t subscription_id_;
    DataStreamIndex index_;
    const MoqtPriority publisher_priority_;
    MoqtDataStreamType stream_type_;
    // Minimum object ID that should go out next. The session doesn't know the
    // exact ID of the next object in the stream because the next object could
    // be in a different subgroup or simply be skipped.
    uint64_t next_object_;
    // Used in subgroup streams to compute the object ID diff. If nullopt, the
    // stream header has not been written yet.
    std::optional<uint64_t> last_object_id_;
    // If this data stream is for SUBSCRIBE, reset it if an object has been
    // excessively delayed per Section 7.1.1.2.
    std::unique_ptr<quic::QuicAlarm> delivery_timeout_alarm_;
    // A weak pointer to an object owned by the session.  Used to make sure the
    // session does not get called after being destroyed.
    std::weak_ptr<void> session_liveness_;
  };

  class QUICHE_EXPORT PublishedFetch {
   public:
    PublishedFetch(uint64_t request_id, MoqtSession* session,
                   std::unique_ptr<MoqtFetchTask> fetch)
        : session_(session),
          fetch_(std::move(fetch)),
          request_id_(request_id) {}

    class FetchStreamVisitor : public webtransport::StreamVisitor {
     public:
      FetchStreamVisitor(std::shared_ptr<PublishedFetch> fetch,
                         webtransport::Stream* stream);
      ~FetchStreamVisitor() {
        std::shared_ptr<PublishedFetch> fetch = fetch_.lock();
        if (fetch != nullptr) {
          fetch->session()->incoming_fetches_.erase(fetch->request_id_);
        }
      }
      // webtransport::StreamVisitor implementation.
      void OnCanRead() override {}  // Write-only stream.
      void OnCanWrite() override;
      void OnResetStreamReceived(
          webtransport::StreamErrorCode /*error*/) override {
      }  // Write-only stream
      void OnStopSendingReceived(
          webtransport::StreamErrorCode /*error*/) override {}
      void OnWriteSideInDataRecvdState() override {}

     private:
      std::weak_ptr<PublishedFetch> fetch_;
      webtransport::Stream* stream_;
      bool stream_header_written_ = false;
    };

    MoqtFetchTask* fetch_task() { return fetch_.get(); }
    MoqtSession* session() { return session_; }
    uint64_t request_id() const { return request_id_; }
    void SetStreamId(webtransport::StreamId id) { stream_id_ = id; }

   private:
    MoqtSession* session_;
    std::unique_ptr<MoqtFetchTask> fetch_;
    uint64_t request_id_;
    // Store the stream ID in case a FETCH_CANCEL requires a reset.
    std::optional<webtransport::StreamId> stream_id_;
  };

  class QUICHE_EXPORT DownstreamTrackStatus : public MoqtObjectListener {
   public:
    DownstreamTrackStatus(uint64_t request_id,
                          MoqtSession* absl_nonnull session,
                          MoqtTrackPublisher* absl_nonnull publisher)
        : request_id_(request_id), session_(session), publisher_(publisher) {}
    ~DownstreamTrackStatus() {
      if (publisher_ != nullptr) {
        publisher_->RemoveObjectListener(this);
      }
    }
    DownstreamTrackStatus(const DownstreamTrackStatus&) = delete;
    DownstreamTrackStatus(DownstreamTrackStatus&&) = delete;

    void OnSubscribeAccepted() override {
      if (publisher_ == nullptr) {
        QUICHE_NOTREACHED();
        return;
      }
      MoqtTrackStatusOk track_status_ok;
      track_status_ok.request_id = request_id_;
      track_status_ok.track_alias = 0;
      QUICHE_BUG_IF(quic_bug_track_status_ok_no_expiration,
                    !publisher_->expiration().has_value())
          << "Request accepted without expiration";
      track_status_ok.expires =
          publisher_->expiration().value_or(quic::QuicTimeDelta::Zero());
      QUICHE_BUG_IF(quic_bug_track_status_ok_no_delivery_order,
                    !publisher_->delivery_order().has_value())
          << "Request accepted without delivery order";
      track_status_ok.group_order =
          publisher_->delivery_order().value_or(MoqtDeliveryOrder::kAscending);
      track_status_ok.largest_location = publisher_->largest_location();
      session_->SendControlMessage(
          session_->framer_.SerializeTrackStatusOk(track_status_ok));
      session_->incoming_track_status_.erase(request_id_);
      // No class access below this line!
    }

    void OnSubscribeRejected(MoqtSubscribeErrorReason error_reason) override {
      MoqtTrackStatusError track_status_error;
      track_status_error.request_id = request_id_;
      track_status_error.error_code = error_reason.error_code;
      track_status_error.reason_phrase = error_reason.reason_phrase;
      session_->SendControlMessage(
          session_->framer_.SerializeTrackStatusError(track_status_error));
      session_->incoming_track_status_.erase(request_id_);
      // No class access below this line!
    }

    void OnNewObjectAvailable(Location /*sequence*/, uint64_t /*subgroup*/,
                              MoqtPriority /*publisher_priority*/) override {}
    void OnNewFinAvailable(Location /*location*/,
                           uint64_t /*subgroup*/) override {}
    void OnSubgroupAbandoned(
        uint64_t /*group*/, uint64_t /*subgroup*/,
        webtransport::StreamErrorCode /*error_code*/) override {}
    void OnGroupAbandoned(uint64_t /*group_id*/) override {}
    void OnTrackPublisherGone() override {
      publisher_ = nullptr;
      OnSubscribeRejected(MoqtSubscribeErrorReason(
          RequestErrorCode::kTrackDoesNotExist, "Track publisher gone"));
    }

   private:
    uint64_t request_id_;
    MoqtSession* session_;
    MoqtTrackPublisher* publisher_;
  };

  class GoAwayTimeoutDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    explicit GoAwayTimeoutDelegate(MoqtSession* session) : session_(session) {}
    void OnAlarm() override;

   private:
    MoqtSession* session_;
  };

  class PublishDoneDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    PublishDoneDelegate(MoqtSession* session, SubscribeRemoteTrack* subscribe)
        : session_(session), subscribe_(subscribe) {}

    void OnAlarm() override { session_->DestroySubscription(subscribe_); }

   private:
    MoqtSession* session_;
    SubscribeRemoteTrack* subscribe_;
  };

  // Private members of MoqtSession.
  // Returns true if SUBSCRIBE_DONE was sent.
  bool PublishIsDone(uint64_t request_id, PublishDoneCode code,
                     absl::string_view error_reason);
  void MaybeDestroySubscription(SubscribeRemoteTrack* subscribe);
  void DestroySubscription(SubscribeRemoteTrack* subscribe);

  // Returns the pointer to the control stream, or nullptr if none is present.
  ControlStream* GetControlStream();
  // Sends a message on the control stream; QUICHE_DCHECKs if no control stream
  // is present.
  void SendControlMessage(quiche::QuicheBuffer message);

  // Returns false if the SUBSCRIBE isn't sent.
  bool Subscribe(MoqtSubscribe& message, SubscribeVisitor* visitor);

  // Opens a new data stream, or queues it if the session is flow control
  // blocked.
  webtransport::Stream* OpenOrQueueDataStream(
      uint64_t subscription_id, const NewStreamParameters& parameters);
  // Same as above, except the session is required to be not flow control
  // blocked.
  webtransport::Stream* OpenDataStream(PublishedSubscription& subscription,
                                       const NewStreamParameters& parameters);
  // Returns false if creation failed.
  [[nodiscard]] bool OpenDataStream(std::shared_ptr<PublishedFetch> fetch,
                                    webtransport::SendOrder send_order);

  SubscribeRemoteTrack* RemoteTrackByAlias(uint64_t track_alias);
  RemoteTrack* RemoteTrackById(uint64_t subscribe_id);
  SubscribeRemoteTrack* RemoteTrackByName(const FullTrackName& name);

  // Checks that a subscribe ID from a SUBSCRIBE or FETCH is valid, and throws
  // a session error if is not.
  bool ValidateRequestId(uint64_t request_id);

  // Actually sends an object on |stream| with track alias or fetch ID |id|
  // and metadata in |object|. Not for use with datagrams. Returns |true| if
  // the write was successful.
  bool WriteObjectToStream(webtransport::Stream* stream, uint64_t id,
                           const PublishedObjectMetadata& metadata,
                           quiche::QuicheMemSlice payload,
                           MoqtDataStreamType type,
                           std::optional<uint64_t> last_id, bool fin);

  void CancelFetch(uint64_t request_id);

  // Sends an OBJECT_ACK message for a specific subscribe ID.
  void SendObjectAck(uint64_t subscribe_id, uint64_t group_id,
                     uint64_t object_id,
                     quic::QuicTimeDelta delta_from_deadline) {
    if (!SupportsObjectAck()) {
      return;
    }
    MoqtObjectAck ack;
    ack.subscribe_id = subscribe_id;
    ack.group_id = group_id;
    ack.object_id = object_id;
    ack.delta_from_deadline = delta_from_deadline;
    SendControlMessage(framer_.SerializeObjectAck(ack));
  }

  // Indicates if OBJECT_ACK is supported by both sides.
  bool SupportsObjectAck() const {
    return parameters_.support_object_acks && peer_supports_object_ack_;
  }

  // Called when the incoming track is malformed per Section 2.5 of
  // draft-ietf-moqt-moq-transport-12. Unsubscribe and notify the application so
  // the error can be propagated downstream, if necessary.
  void OnMalformedTrack(RemoteTrack* track);

  bool is_closing_ = false;
  webtransport::Session* session_;
  MoqtSessionParameters parameters_;
  MoqtSessionCallbacks callbacks_;
  MoqtFramer framer_;

  std::optional<webtransport::StreamId> control_stream_;
  bool peer_supports_object_ack_ = false;
  std::string error_;

  bool sent_goaway_ = false;
  bool received_goaway_ = false;

  // Upstream SUBSCRIBE state.
  // Upstream SUBSCRIBEs and FETCHes, indexed by subscribe_id.
  absl::flat_hash_map<uint64_t, std::unique_ptr<RemoteTrack>> upstream_by_id_;
  // All SUBSCRIBEs, indexed by track_alias.
  absl::flat_hash_map<uint64_t, SubscribeRemoteTrack*> subscribe_by_alias_;
  // All SUBSCRIBEs, indexed by track name.
  absl::flat_hash_map<FullTrackName, SubscribeRemoteTrack*> subscribe_by_name_;

  // The next subscribe ID that the local endpoint can send.
  uint64_t next_request_id_ = 0;
  // The local endpoint can send subscribe IDs less than this value.
  uint64_t peer_max_request_id_ = 0;
  std::optional<uint64_t> last_requests_blocked_sent_;

  // All open incoming subscriptions, indexed by track name, used to check for
  // duplicates.
  absl::flat_hash_set<FullTrackName> subscribed_track_names_;
  // Application object representing the publisher for all of the tracks that
  // can be subscribed to via this connection.  Must outlive this object.
  MoqtPublisher* publisher_;
  // Subscriptions for local tracks by the remote peer, indexed by subscribe ID.
  absl::flat_hash_map<uint64_t, std::unique_ptr<PublishedSubscription>>
      published_subscriptions_;
  // Keeps track of all subscribe IDs that have queued outgoing data streams.
  absl::btree_set<SubscriptionWithQueuedStream>
      subscribes_with_queued_outgoing_data_streams_;
  // This is only used to check for track_alias collisions.
  absl::flat_hash_set<uint64_t> used_track_aliases_;
  uint64_t next_local_track_alias_ = 0;

  // Incoming FETCHes, indexed by fetch ID. There will be other pointers to
  // PublishedFetch, so storing a shared_ptr in the map provides pointer
  // stability for the value.
  absl::flat_hash_map<uint64_t, std::shared_ptr<PublishedFetch>>
      incoming_fetches_;

  absl::flat_hash_map<uint64_t, std::unique_ptr<DownstreamTrackStatus>>
      incoming_track_status_;

  // Monitoring interfaces for expected incoming subscriptions.
  absl::flat_hash_map<FullTrackName, MoqtPublishingMonitorInterface*>
      monitoring_interfaces_for_published_tracks_;

  // Outgoing PUBLISH_NAMESPACE for which no OK or ERROR has been received.
  absl::flat_hash_map<uint64_t, TrackNamespace>
      pending_outgoing_publish_namespaces_;
  // All outgoing PUBLISH_NAMESPACE.
  absl::flat_hash_map<TrackNamespace, MoqtOutgoingPublishNamespaceCallback>
      outgoing_publish_namespaces_;
  absl::flat_hash_set<TrackNamespace> incoming_publish_namespaces_;

  // The value is nullptr after OK or ERROR is received. The entry is deleted
  // when sending UNSUBSCRIBE_NAMESPACE, to make sure the application doesn't
  // unsubscribe from something that it isn't subscribed to. PUBLISH_NAMESPACEs
  // that result from this subscription use incoming_publish_namespace_callback.
  struct PendingSubscribeNamespaceData {
    TrackNamespace track_namespace;
    MoqtOutgoingSubscribeNamespaceCallback callback;
  };
  absl::flat_hash_map<uint64_t, PendingSubscribeNamespaceData>
      pending_outgoing_subscribe_namespaces_;
  absl::flat_hash_set<TrackNamespace> outgoing_subscribe_namespaces_;
  // It's an error if the namespaces overlap, so keep track of them.
  SessionNamespaceTree incoming_subscribe_namespace_;

  // The minimum request ID the peer can use that is monotonically increasing.
  uint64_t next_incoming_request_id_ = 0;
  // The maximum request ID sent to the peer. Peer-generated IDs must be less
  // than this value.
  uint64_t local_max_request_id_ = 0;

  std::unique_ptr<quic::QuicAlarmFactory> alarm_factory_;
  // Kill the session if the peer doesn't promptly close out the session after
  // a GOAWAY.
  std::unique_ptr<quic::QuicAlarm> goaway_timeout_alarm_;

  // If true, use a non-standard design where a timer starts for group n when
  // the first object of group n+1 arrives.
  bool alternate_delivery_timeout_ = false;

  MoqtTraceRecorder trace_recorder_;

  quiche::QuicheWeakPtrFactory<MoqtSessionInterface> weak_ptr_factory_;

  // Must be last.  Token used to make sure that the streams do not call into
  // the session when the session has already been destroyed.

  std::shared_ptr<Empty> liveness_token_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_
