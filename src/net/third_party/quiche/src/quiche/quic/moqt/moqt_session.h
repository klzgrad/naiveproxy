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
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtSessionPeer;
}

using MoqtSessionEstablishedCallback = quiche::SingleUseCallback<void()>;
using MoqtSessionTerminatedCallback =
    quiche::SingleUseCallback<void(absl::string_view error_message)>;
using MoqtSessionDeletedCallback = quiche::SingleUseCallback<void()>;
// If |error_message| is nullopt, the ANNOUNCE was successful.
using MoqtOutgoingAnnounceCallback = quiche::SingleUseCallback<void(
    FullTrackName track_namespace,
    std::optional<MoqtAnnounceErrorReason> error)>;
using MoqtIncomingAnnounceCallback =
    quiche::MultiUseCallback<std::optional<MoqtAnnounceErrorReason>(
        FullTrackName track_namespace)>;

inline std::optional<MoqtAnnounceErrorReason> DefaultIncomingAnnounceCallback(
    FullTrackName /*track_namespace*/) {
  return std::optional(MoqtAnnounceErrorReason{
      MoqtAnnounceErrorCode::kAnnounceNotSupported,
      "This endpoint does not accept incoming ANNOUNCE messages"});
};

// Callbacks for session-level events.
struct MoqtSessionCallbacks {
  MoqtSessionEstablishedCallback session_established_callback = +[] {};
  MoqtSessionTerminatedCallback session_terminated_callback =
      +[](absl::string_view) {};
  MoqtSessionDeletedCallback session_deleted_callback = +[] {};

  MoqtIncomingAnnounceCallback incoming_announce_callback =
      DefaultIncomingAnnounceCallback;
};

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

class QUICHE_EXPORT MoqtSession : public webtransport::SessionVisitor {
 public:
  MoqtSession(webtransport::Session* session, MoqtSessionParameters parameters,
              MoqtSessionCallbacks callbacks = MoqtSessionCallbacks());
  ~MoqtSession() { std::move(callbacks_.session_deleted_callback)(); }

  // webtransport::SessionVisitor implementation.
  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode,
                       const std::string&) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view datagram) override;
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override;

  void Error(MoqtError code, absl::string_view error);

  quic::Perspective perspective() const { return parameters_.perspective; }

  // Send an ANNOUNCE message for |track_namespace|, and call
  // |announce_callback| when the response arrives. Will fail immediately if
  // there is already an unresolved ANNOUNCE for that namespace.
  void Announce(FullTrackName track_namespace,
                MoqtOutgoingAnnounceCallback announce_callback);

  // Returns true if SUBSCRIBE was sent. If there is already a subscription to
  // the track, the message will still be sent. However, the visitor will be
  // ignored.
  // Subscribe from (start_group, start_object) to the end of the track.
  bool SubscribeAbsolute(
      const FullTrackName& name, uint64_t start_group, uint64_t start_object,
      RemoteTrack::Visitor* visitor,
      MoqtSubscribeParameters parameters = MoqtSubscribeParameters());
  // Subscribe from (start_group, start_object) to the end of end_group.
  bool SubscribeAbsolute(
      const FullTrackName& name, uint64_t start_group, uint64_t start_object,
      uint64_t end_group, RemoteTrack::Visitor* visitor,
      MoqtSubscribeParameters parameters = MoqtSubscribeParameters());
  // Subscribe from (start_group, start_object) to (end_group, end_object).
  bool SubscribeAbsolute(
      const FullTrackName& name, uint64_t start_group, uint64_t start_object,
      uint64_t end_group, uint64_t end_object, RemoteTrack::Visitor* visitor,
      MoqtSubscribeParameters parameters = MoqtSubscribeParameters());
  bool SubscribeCurrentObject(
      const FullTrackName& name, RemoteTrack::Visitor* visitor,
      MoqtSubscribeParameters parameters = MoqtSubscribeParameters());
  bool SubscribeCurrentGroup(
      const FullTrackName& name, RemoteTrack::Visitor* visitor,
      MoqtSubscribeParameters parameters = MoqtSubscribeParameters());

  webtransport::Session* session() { return session_; }
  MoqtSessionCallbacks& callbacks() { return callbacks_; }
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

  void GrantMoreSubscribes(uint64_t num_subscribes);

 private:
  friend class test::MoqtSessionPeer;

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
    void OnSubscribeDoneMessage(const MoqtSubscribeDone& /*message*/) override {
    }
    void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) override;
    void OnAnnounceMessage(const MoqtAnnounce& message) override;
    void OnAnnounceOkMessage(const MoqtAnnounceOk& message) override;
    void OnAnnounceErrorMessage(const MoqtAnnounceError& message) override;
    void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) override;
    void OnTrackStatusRequestMessage(
        const MoqtTrackStatusRequest& message) override {};
    void OnUnannounceMessage(const MoqtUnannounce& /*message*/) override {}
    void OnTrackStatusMessage(const MoqtTrackStatus& message) override {}
    void OnGoAwayMessage(const MoqtGoAway& /*message*/) override {}
    void OnSubscribeAnnouncesMessage(
        const MoqtSubscribeAnnounces& message) override {}
    void OnSubscribeAnnouncesOkMessage(
        const MoqtSubscribeAnnouncesOk& message) override {}
    void OnSubscribeAnnouncesErrorMessage(
        const MoqtSubscribeAnnouncesError& message) override {}
    void OnUnsubscribeAnnouncesMessage(
        const MoqtUnsubscribeAnnounces& message) override {}
    void OnMaxSubscribeIdMessage(const MoqtMaxSubscribeId& message) override;
    void OnFetchMessage(const MoqtFetch& message) override {}
    void OnFetchCancelMessage(const MoqtFetchCancel& message) override {}
    void OnFetchOkMessage(const MoqtFetchOk& message) override {}
    void OnFetchErrorMessage(const MoqtFetchError& message) override {}
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

   private:
    friend class test::MoqtSessionPeer;
    void SendSubscribeError(const MoqtSubscribe& message,
                            SubscribeErrorCode error_code,
                            absl::string_view reason_phrase,
                            uint64_t track_alias);

    MoqtSession* session_;
    webtransport::Stream* stream_;
    MoqtControlParser parser_;
  };
  class QUICHE_EXPORT IncomingDataStream : public webtransport::StreamVisitor,
                                           public MoqtDataParserVisitor {
   public:
    IncomingDataStream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session), stream_(stream), parser_(this) {}

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

   private:
    friend class test::MoqtSessionPeer;
    void OnControlMessageReceived();

    MoqtSession* session_;
    webtransport::Stream* stream_;
    MoqtDataParser parser_;
    std::string partial_object_;
  };
  // Represents a record for a single subscription to a local track that is
  // being sent to the peer.
  class PublishedSubscription : public MoqtObjectListener {
   public:
    explicit PublishedSubscription(
        MoqtSession* session,
        std::shared_ptr<MoqtTrackPublisher> track_publisher,
        const MoqtSubscribe& subscribe,
        MoqtPublishingMonitorInterface* monitoring_interface);
    ~PublishedSubscription();

    PublishedSubscription(const PublishedSubscription&) = delete;
    PublishedSubscription(PublishedSubscription&&) = delete;
    PublishedSubscription& operator=(const PublishedSubscription&) = delete;
    PublishedSubscription& operator=(PublishedSubscription&&) = delete;

    uint64_t subscription_id() const { return subscription_id_; }
    MoqtTrackPublisher& publisher() { return *track_publisher_; }
    uint64_t track_alias() const { return track_alias_; }
    std::optional<FullSequence> largest_sent() const { return largest_sent_; }
    MoqtPriority subscriber_priority() const { return subscriber_priority_; }
    std::optional<MoqtDeliveryOrder> subscriber_delivery_order() const {
      return subscriber_delivery_order_;
    }
    void set_subscriber_priority(MoqtPriority priority);

    // This is only called for objects that have just arrived.
    void OnNewObjectAvailable(FullSequence sequence) override;
    void OnTrackPublisherGone() override;
    void ProcessObjectAck(const MoqtObjectAck& message) {
      if (monitoring_interface_ == nullptr) {
        return;
      }
      monitoring_interface_->OnObjectAckReceived(
          message.group_id, message.object_id, message.delta_from_deadline);
    }

    // Creates streams for all objects that are currently in the track's object
    // cache and match the subscription window.  This is in some sense similar
    // to a fetch (since all of the objects are in the past), but is
    // conceptually simpler, as backpressure is less of a concern.
    void Backfill();

    // Updates the window and other properties of the subscription in question.
    void Update(FullSequence start, std::optional<FullSequence> end,
                MoqtPriority subscriber_priority);
    // Checks if the specified sequence is within the window of this
    // subscription.
    bool InWindow(FullSequence sequence) { return window_.InWindow(sequence); }

    void OnDataStreamCreated(webtransport::StreamId id,
                             FullSequence start_sequence);
    void OnDataStreamDestroyed(webtransport::StreamId id,
                               FullSequence end_sequence);
    void OnObjectSent(FullSequence sequence);

    std::vector<webtransport::StreamId> GetAllStreams() const;

    webtransport::SendOrder GetSendOrder(FullSequence sequence) const;

    void AddQueuedOutgoingDataStream(FullSequence first_object);
    // Pops the pending outgoing data stream, with the highest send order.
    // The session keeps track of which subscribes have pending streams. This
    // function will trigger a QUICHE_DCHECK if called when there are no pending
    // streams.
    FullSequence NextQueuedOutgoingDataStream();

   private:
    SendStreamMap& stream_map();
    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    void SendDatagram(FullSequence sequence);
    webtransport::SendOrder FinalizeSendOrder(
        webtransport::SendOrder send_order) {
      return UpdateSendOrderForSubscriberPriority(send_order,
                                                  subscriber_priority_);
    }

    uint64_t subscription_id_;
    MoqtSession* session_;
    std::shared_ptr<MoqtTrackPublisher> track_publisher_;
    uint64_t track_alias_;
    SubscribeWindow window_;
    MoqtPriority subscriber_priority_;
    std::optional<MoqtDeliveryOrder> subscriber_delivery_order_;
    MoqtPublishingMonitorInterface* monitoring_interface_;
    // Largest sequence number ever sent via this subscription.
    std::optional<FullSequence> largest_sent_;
    // Should be almost always accessed via `stream_map()`.
    std::optional<SendStreamMap> lazily_initialized_stream_map_;
    // Store the send order of queued outgoing data streams. Use a
    // subscriber_priority_ of zero to avoid having to update it, and call
    // FinalizeSendOrder() whenever delivering it to the MoqtSession.d
    absl::btree_multimap<webtransport::SendOrder, FullSequence>
        queued_outgoing_data_streams_;
  };
  class QUICHE_EXPORT OutgoingDataStream : public webtransport::StreamVisitor {
   public:
    OutgoingDataStream(MoqtSession* session, webtransport::Stream* stream,
                       PublishedSubscription& subscription,
                       FullSequence first_object);
    ~OutgoingDataStream();

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override {}
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override {}
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override {}
    void OnWriteSideInDataRecvdState() override {}

    webtransport::Stream* stream() const { return stream_; }

    // Sends objects on the stream, starting with `next_object_`, until the
    // stream becomes write-blocked or closed.
    void SendObjects(PublishedSubscription& subscription);

    // Recomputes the send order and updates it for the associated stream.
    void UpdateSendOrder(PublishedSubscription& subscription);

   private:
    friend class test::MoqtSessionPeer;

    // Checks whether the associated subscription is still valid; if not, resets
    // the stream and returns nullptr.
    PublishedSubscription* GetSubscriptionIfValid();

    // Actually sends an object on the stream; the object MUST be
    // `next_object_`.
    void SendNextObject(PublishedSubscription& subscription,
                        PublishedObject object);

    MoqtSession* session_;
    webtransport::Stream* stream_;
    uint64_t subscription_id_;
    // A FullSequence with the minimum object ID that should go out next. The
    // session doesn't know what the next object ID in the stream is because
    // the next object could be in a different subgroup or simply be skipped.
    FullSequence next_object_;
    bool stream_header_written_ = false;
    // A weak pointer to an object owned by the session.  Used to make sure the
    // session does not get called after being destroyed.
    std::weak_ptr<void> session_liveness_;
  };

  // Private members of MoqtSession.

  // Returns true if SUBSCRIBE_DONE was sent.
  bool SubscribeIsDone(uint64_t subscribe_id, SubscribeDoneCode code,
                       absl::string_view reason_phrase);
  // Returns the pointer to the control stream, or nullptr if none is present.
  ControlStream* GetControlStream();
  // Sends a message on the control stream; QUICHE_DCHECKs if no control stream
  // is present.
  void SendControlMessage(quiche::QuicheBuffer message);

  // Returns false if the SUBSCRIBE isn't sent.
  bool Subscribe(MoqtSubscribe& message, RemoteTrack::Visitor* visitor);

  // Opens a new data stream, or queues it if the session is flow control
  // blocked.
  webtransport::Stream* OpenOrQueueDataStream(uint64_t subscription_id,
                                              FullSequence first_object);
  // Same as above, except the session is required to be not flow control
  // blocked.
  webtransport::Stream* OpenDataStream(PublishedSubscription& subscription,
                                       FullSequence first_object);

  // Get FullTrackName and visitor for a subscribe_id and track_alias. Returns
  // an empty FullTrackName tuple and nullptr if not present.
  std::pair<FullTrackName, RemoteTrack::Visitor*> TrackPropertiesFromAlias(
      const MoqtObject& message);

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

  webtransport::Session* session_;
  MoqtSessionParameters parameters_;
  MoqtSessionCallbacks callbacks_;
  MoqtFramer framer_;

  std::optional<webtransport::StreamId> control_stream_;
  bool peer_supports_object_ack_ = false;
  std::string error_;

  // All the tracks the session is subscribed to, indexed by track_alias.
  absl::flat_hash_map<uint64_t, RemoteTrack> remote_tracks_;
  // Look up aliases for remote tracks by name
  absl::flat_hash_map<FullTrackName, uint64_t> remote_track_aliases_;
  uint64_t next_remote_track_alias_ = 0;

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

  // Indexed by subscribe_id.
  struct ActiveSubscribe {
    MoqtSubscribe message;
    RemoteTrack::Visitor* visitor;
    // The forwarding preference of the first received object, which all
    // subsequent objects must match.
    std::optional<MoqtForwardingPreference> forwarding_preference;
    // If true, an object has arrived for the subscription before SUBSCRIBE_OK
    // arrived.
    bool received_object = false;
  };
  // Outgoing SUBSCRIBEs that have not received SUBSCRIBE_OK or SUBSCRIBE_ERROR.
  absl::flat_hash_map<uint64_t, ActiveSubscribe> active_subscribes_;
  uint64_t next_subscribe_id_ = 0;

  // Monitoring interfaces for expected incoming subscriptions.
  absl::flat_hash_map<FullTrackName, MoqtPublishingMonitorInterface*>
      monitoring_interfaces_for_published_tracks_;

  // Indexed by track namespace.
  absl::flat_hash_map<FullTrackName, MoqtOutgoingAnnounceCallback>
      pending_outgoing_announces_;

  // The role the peer advertised in its SETUP message. Initialize it to avoid
  // an uninitialized value if no SETUP arrives or it arrives with no Role
  // parameter, and other checks have changed/been disabled.
  MoqtRole peer_role_ = MoqtRole::kPubSub;

  // The maximum subscribe ID that the local endpoint can send.
  uint64_t peer_max_subscribe_id_ = 0;
  // The maximum subscribe ID sent to the peer.
  uint64_t local_max_subscribe_id_ = 0;

  // Must be last.  Token used to make sure that the streams do not call into
  // the session when the session has already been destroyed.
  struct Empty {};
  std::shared_ptr<Empty> liveness_token_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_
