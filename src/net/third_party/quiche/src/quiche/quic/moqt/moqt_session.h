// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
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
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
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

  virtual void OnObjectAckSupportKnown(bool supported) = 0;
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

  void Error(MoqtError code, absl::string_view error) override;

  quic::Perspective perspective() const { return parameters_.perspective; }

  // Returns true if message was sent.
  bool SubscribeAnnounces(FullTrackName track_namespace,
                          MoqtOutgoingSubscribeAnnouncesCallback callback,
                          VersionSpecificParameters parameters);
  bool UnsubscribeAnnounces(FullTrackName track_namespace);

  // Send an ANNOUNCE message for |track_namespace|, and call
  // |announce_callback| when the response arrives. Will fail immediately if
  // there is already an unresolved ANNOUNCE for that namespace.
  void Announce(FullTrackName track_namespace,
                MoqtOutgoingAnnounceCallback announce_callback,
                VersionSpecificParameters parameters);
  // Returns true if message was sent, false if there is no ANNOUNCE to cancel.
  bool Unannounce(FullTrackName track_namespace);
  // Allows the subscriber to declare it will not subscribe to |track_namespace|
  // anymore.
  void CancelAnnounce(FullTrackName track_namespace, RequestErrorCode code,
                      absl::string_view reason_phrase);

  // Returns true if SUBSCRIBE was sent. If there is already a subscription to
  // the track, the message will still be sent. However, the visitor will be
  // ignored. If |visitor| is nullptr, forward will be set to false.
  // Subscribe from (start_group, start_object) to the end of the track.
  bool SubscribeAbsolute(const FullTrackName& name, uint64_t start_group,
                         uint64_t start_object,
                         SubscribeRemoteTrack::Visitor* visitor,
                         VersionSpecificParameters parameters) override;
  // Subscribe from (start_group, start_object) to the end of end_group.
  bool SubscribeAbsolute(const FullTrackName& name, uint64_t start_group,
                         uint64_t start_object, uint64_t end_group,
                         SubscribeRemoteTrack::Visitor* visitor,
                         VersionSpecificParameters parameters) override;
  bool SubscribeCurrentObject(const FullTrackName& name,
                              SubscribeRemoteTrack::Visitor* visitor,
                              VersionSpecificParameters parameters) override;
  bool SubscribeNextGroup(const FullTrackName& name,
                          SubscribeRemoteTrack::Visitor* visitor,
                          VersionSpecificParameters parameters) override;
  bool SubscribeUpdate(const FullTrackName& name, std::optional<Location> start,
                       std::optional<uint64_t> end_group,
                       std::optional<MoqtPriority> subscriber_priority,
                       std::optional<bool> forward,
                       VersionSpecificParameters parameters) override;
  // Returns false if the subscription is not found. The session immediately
  // destroys all subscription state.
  void Unsubscribe(const FullTrackName& name);
  // |callback| will be called when FETCH_OK or FETCH_ERROR is received, and
  // delivers a pointer to MoqtFetchTask for application use. The callback
  // transfers ownership of MoqtFetchTask to the application.
  // To cancel a FETCH, simply destroy the FetchTask.
  bool Fetch(const FullTrackName& name, FetchResponseCallback callback,
             Location start, uint64_t end_group,
             std::optional<uint64_t> end_object, MoqtPriority priority,
             std::optional<MoqtDeliveryOrder> delivery_order,
             VersionSpecificParameters parameters) override;
  // Sends both a SUBSCRIBE and a joining FETCH, beginning |num_previous_groups|
  // groups before the current group. The Fetch will not be flow controlled,
  // instead using |visitor| to deliver fetched objects when they arrive. Gaps
  // in the FETCH will not be filled by with ObjectDoesNotExist. If the FETCH
  // fails for any reason, the application will not receive a notification; it
  // will just appear to be missing objects.
  bool JoiningFetch(const FullTrackName& name,
                    SubscribeRemoteTrack::Visitor* visitor,
                    uint64_t num_previous_groups,
                    VersionSpecificParameters parameters) override;
  // Sends both a SUBSCRIBE and a joining FETCH, beginning |num_previous_groups|
  // groups before the current group. The application provides |callback| to
  // fully control acceptance of Fetched objects.
  bool JoiningFetch(const FullTrackName& name,
                    SubscribeRemoteTrack::Visitor* visitor,
                    FetchResponseCallback callback,
                    uint64_t num_previous_groups, MoqtPriority priority,
                    std::optional<MoqtDeliveryOrder> delivery_order,
                    VersionSpecificParameters parameters) override;

  // Send a GOAWAY message to the peer. |new_session_uri| must be empty if
  // called by the client.
  void GoAway(absl::string_view new_session_uri);

  webtransport::Session* session() { return session_; }
  MoqtSessionCallbacks& callbacks() override { return callbacks_; }
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

 private:
  friend class test::MoqtSessionPeer;

  struct Empty {};

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
    void OnSubscribeDoneMessage(const MoqtSubscribeDone& /*message*/) override;
    void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) override;
    void OnAnnounceMessage(const MoqtAnnounce& message) override;
    void OnAnnounceOkMessage(const MoqtAnnounceOk& message) override;
    void OnAnnounceErrorMessage(const MoqtAnnounceError& message) override;
    void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) override;
    void OnTrackStatusRequestMessage(
        const MoqtTrackStatusRequest& message) override {};
    void OnUnannounceMessage(const MoqtUnannounce& /*message*/) override;
    void OnTrackStatusMessage(const MoqtTrackStatus& message) override {}
    void OnGoAwayMessage(const MoqtGoAway& /*message*/) override;
    void OnSubscribeAnnouncesMessage(
        const MoqtSubscribeAnnounces& message) override;
    void OnSubscribeAnnouncesOkMessage(
        const MoqtSubscribeAnnouncesOk& message) override;
    void OnSubscribeAnnouncesErrorMessage(
        const MoqtSubscribeAnnouncesError& message) override;
    void OnUnsubscribeAnnouncesMessage(
        const MoqtUnsubscribeAnnounces& message) override;
    void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) override;
    void OnFetchMessage(const MoqtFetch& message) override;
    void OnFetchCancelMessage(const MoqtFetchCancel& message) override {}
    void OnFetchOkMessage(const MoqtFetchOk& message) override;
    void OnFetchErrorMessage(const MoqtFetchError& message) override;
    void OnRequestsBlockedMessage(const MoqtRequestsBlocked& message) override;
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
                            absl::string_view reason_phrase,
                            uint64_t track_alias);

   private:
    friend class test::MoqtSessionPeer;
    void SendFetchError(uint64_t subscribe_id, RequestErrorCode error_code,
                        absl::string_view reason_phrase);

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
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override {}
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override {}
    void OnWriteSideInDataRecvdState() override {}

    // MoqtParserVisitor implementation.
    // TODO: Handle a stream FIN.
    void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                         bool end_of_message) override;
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
    uint64_t track_alias() const { return track_alias_; }
    std::optional<Location> largest_sent() const { return largest_sent_; }
    MoqtPriority subscriber_priority() const { return subscriber_priority_; }
    std::optional<MoqtDeliveryOrder> subscriber_delivery_order() const {
      return subscriber_delivery_order_;
    }
    void set_subscriber_priority(MoqtPriority priority);

    // MoqtObjectListener implementation.
    void OnSubscribeAccepted() override;
    void OnSubscribeRejected(
        MoqtSubscribeErrorReason reason,
        std::optional<uint64_t> track_alias = std::nullopt) override;
    // This is only called for objects that have just arrived.
    void OnNewObjectAvailable(Location sequence) override;
    void OnTrackPublisherGone() override;
    void OnNewFinAvailable(Location sequence) override;
    void OnSubgroupAbandoned(Location sequence,
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
    Location GetWindowStart() const {
      QUICHE_CHECK(window_.has_value());
      return window_->start();
    }
    MoqtFilterType filter_type() const { return filter_type_; };

    void OnDataStreamCreated(webtransport::StreamId id,
                             Location start_sequence);
    void OnDataStreamDestroyed(webtransport::StreamId id,
                               Location end_sequence);
    void OnObjectSent(Location sequence);

    std::vector<webtransport::StreamId> GetAllStreams() const;

    webtransport::SendOrder GetSendOrder(Location sequence) const;

    void AddQueuedOutgoingDataStream(Location first_object);
    // Pops the pending outgoing data stream, with the highest send order.
    // The session keeps track of which subscribes have pending streams. This
    // function will trigger a QUICHE_DCHECK if called when there are no pending
    // streams.
    Location NextQueuedOutgoingDataStream();

    quic::QuicTimeDelta delivery_timeout() const { return delivery_timeout_; }
    void set_delivery_timeout(quic::QuicTimeDelta timeout) {
      delivery_timeout_ = timeout;
    }

    void OnStreamTimeout(Location sequence) {
      sequence.object = 0;
      reset_subgroups_.insert(sequence);
      if (session_->alternate_delivery_timeout_) {
        first_active_group_ = std::max(first_active_group_, sequence.group + 1);
      }
    }

    uint64_t first_active_group() const { return first_active_group_; }

    absl::flat_hash_set<Location>& reset_subgroups() {
      return reset_subgroups_;
    }

    uint64_t streams_opened() const { return streams_opened_; }

   private:
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
    uint64_t track_alias_;
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
    absl::flat_hash_set<Location> reset_subgroups_;
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
    absl::btree_multimap<webtransport::SendOrder, Location>
        queued_outgoing_data_streams_;
  };
  class QUICHE_EXPORT OutgoingDataStream : public webtransport::StreamVisitor {
   public:
    OutgoingDataStream(MoqtSession* session, webtransport::Stream* stream,
                       PublishedSubscription& subscription,
                       Location first_object);
    ~OutgoingDataStream();

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override {}
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override {}
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override {}
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

   private:
    friend class test::MoqtSessionPeer;
    friend class DeliveryTimeoutDelegate;

    // Checks whether the associated subscription is still valid; if not, resets
    // the stream and returns nullptr.
    PublishedSubscription* GetSubscriptionIfValid();

    MoqtSession* session_;
    webtransport::Stream* stream_;
    uint64_t subscription_id_;
    // A Location with the minimum object ID that should go out next. The
    // session doesn't know what the next object ID in the stream is because
    // the next object could be in a different subgroup or simply be skipped.
    Location next_object_;
    bool stream_header_written_ = false;
    // If this data stream is for SUBSCRIBE, reset it if an object has been
    // excessively delayed per Section 7.1.1.2.
    std::unique_ptr<quic::QuicAlarm> delivery_timeout_alarm_;
    // A weak pointer to an object owned by the session.  Used to make sure the
    // session does not get called after being destroyed.
    std::weak_ptr<void> session_liveness_;
  };

  class QUICHE_EXPORT PublishedFetch {
   public:
    PublishedFetch(uint64_t fetch_id, MoqtSession* session,
                   std::unique_ptr<MoqtFetchTask> fetch)
        : session_(session), fetch_(std::move(fetch)), fetch_id_(fetch_id) {}

    class FetchStreamVisitor : public webtransport::StreamVisitor {
     public:
      FetchStreamVisitor(std::shared_ptr<PublishedFetch> fetch,
                         webtransport::Stream* stream)
          : fetch_(fetch), stream_(stream) {
        fetch->fetch_task()->SetObjectAvailableCallback(
            [this]() { this->OnCanWrite(); });
      }
      ~FetchStreamVisitor() {
        std::shared_ptr<PublishedFetch> fetch = fetch_.lock();
        if (fetch != nullptr) {
          fetch->session()->incoming_fetches_.erase(fetch->fetch_id_);
        }
      }
      // webtransport::StreamVisitor implementation.
      void OnCanRead() override {}  // Write-only stream.
      void OnCanWrite() override;
      void OnResetStreamReceived(webtransport::StreamErrorCode error) override {
      }  // Write-only stream
      void OnStopSendingReceived(webtransport::StreamErrorCode error) override {
      }
      void OnWriteSideInDataRecvdState() override {}

     private:
      std::weak_ptr<PublishedFetch> fetch_;
      webtransport::Stream* stream_;
      bool stream_header_written_ = false;
    };

    MoqtFetchTask* fetch_task() { return fetch_.get(); }
    MoqtSession* session() { return session_; }
    uint64_t fetch_id() const { return fetch_id_; }
    void SetStreamId(webtransport::StreamId id) { stream_id_ = id; }

   private:
    MoqtSession* session_;
    std::unique_ptr<MoqtFetchTask> fetch_;
    uint64_t fetch_id_;
    // Store the stream ID in case a FETCH_CANCEL requires a reset.
    std::optional<webtransport::StreamId> stream_id_;
  };

  class GoAwayTimeoutDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    explicit GoAwayTimeoutDelegate(MoqtSession* session) : session_(session) {}
    void OnAlarm() override;

   private:
    MoqtSession* session_;
  };

  class SubscribeDoneDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    SubscribeDoneDelegate(MoqtSession* session, SubscribeRemoteTrack* subscribe)
        : session_(session), subscribe_(subscribe) {}

    void OnAlarm() override { session_->DestroySubscription(subscribe_); }

   private:
    MoqtSession* session_;
    SubscribeRemoteTrack* subscribe_;
  };

  // Private members of MoqtSession.
  // Returns true if SUBSCRIBE_DONE was sent.
  bool SubscribeIsDone(uint64_t subscribe_id, SubscribeDoneCode code,
                       absl::string_view reason_phrase);
  void MaybeDestroySubscription(SubscribeRemoteTrack* subscribe);
  void DestroySubscription(SubscribeRemoteTrack* subscribe);

  // Returns the pointer to the control stream, or nullptr if none is present.
  ControlStream* GetControlStream();
  // Sends a message on the control stream; QUICHE_DCHECKs if no control stream
  // is present.
  void SendControlMessage(quiche::QuicheBuffer message);

  // Returns false if the SUBSCRIBE isn't sent. |provided_track_alias| has a
  // value only if this call is due to a SUBSCRIBE_ERROR.
  bool Subscribe(MoqtSubscribe& message, SubscribeRemoteTrack::Visitor* visitor,
                 std::optional<uint64_t> provided_track_alias = std::nullopt);

  // Opens a new data stream, or queues it if the session is flow control
  // blocked.
  webtransport::Stream* OpenOrQueueDataStream(uint64_t subscription_id,
                                              Location first_object);
  // Same as above, except the session is required to be not flow control
  // blocked.
  webtransport::Stream* OpenDataStream(PublishedSubscription& subscription,
                                       Location first_object);
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
                           const PublishedObject& object,
                           MoqtDataStreamType type, bool is_first_on_stream,
                           bool fin);

  void CancelFetch(uint64_t subscribe_id);

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
  // The next track alias to guess on a SUBSCRIBE.
  uint64_t next_remote_track_alias_ = 0;
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

  // Monitoring interfaces for expected incoming subscriptions.
  absl::flat_hash_map<FullTrackName, MoqtPublishingMonitorInterface*>
      monitoring_interfaces_for_published_tracks_;

  // Indexed by track namespace. If the value is not nullptr, no OK or ERROR
  // has been received. The entry is deleted after sending UNANNOUNCE or
  // receiving ANNOUNCE_CANCEL.
  absl::flat_hash_map<FullTrackName, MoqtOutgoingAnnounceCallback>
      outgoing_announces_;
  // The value is nullptr after OK or ERROR is received. The entry is deleted
  // when sending UNSUBSCRIBE_ANNOUNCES, to make sure the application doesn't
  // unsubscribe from something that it isn't subscribed to. ANNOUNCEs that
  // result from this subscription use incoming_announce_callback.
  absl::flat_hash_map<FullTrackName, MoqtOutgoingSubscribeAnnouncesCallback>
      outgoing_subscribe_announces_;

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

  // Must be last.  Token used to make sure that the streams do not call into
  // the session when the session has already been destroyed.

  std::shared_ptr<Empty> liveness_token_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_
