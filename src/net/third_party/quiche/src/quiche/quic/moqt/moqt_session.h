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

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
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
#include "quiche/common/quiche_circular_deque.h"
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
    absl::string_view track_namespace,
    std::optional<MoqtAnnounceErrorReason> error)>;
using MoqtIncomingAnnounceCallback =
    quiche::MultiUseCallback<std::optional<MoqtAnnounceErrorReason>(
        absl::string_view track_namespace)>;

inline std::optional<MoqtAnnounceErrorReason> DefaultIncomingAnnounceCallback(
    absl::string_view /*track_namespace*/) {
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
  void Announce(absl::string_view track_namespace,
                MoqtOutgoingAnnounceCallback announce_callback);

  // Returns true if SUBSCRIBE was sent. If there is already a subscription to
  // the track, the message will still be sent. However, the visitor will be
  // ignored.
  // Subscribe from (start_group, start_object) to the end of the track.
  bool SubscribeAbsolute(absl::string_view track_namespace,
                         absl::string_view name, uint64_t start_group,
                         uint64_t start_object, RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  // Subscribe from (start_group, start_object) to the end of end_group.
  bool SubscribeAbsolute(absl::string_view track_namespace,
                         absl::string_view name, uint64_t start_group,
                         uint64_t start_object, uint64_t end_group,
                         RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  // Subscribe from (start_group, start_object) to (end_group, end_object).
  bool SubscribeAbsolute(absl::string_view track_namespace,
                         absl::string_view name, uint64_t start_group,
                         uint64_t start_object, uint64_t end_group,
                         uint64_t end_object, RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  bool SubscribeCurrentObject(absl::string_view track_namespace,
                              absl::string_view name,
                              RemoteTrack::Visitor* visitor,
                              absl::string_view auth_info = "");
  bool SubscribeCurrentGroup(absl::string_view track_namespace,
                             absl::string_view name,
                             RemoteTrack::Visitor* visitor,
                             absl::string_view auth_info = "");

  MoqtSessionCallbacks& callbacks() { return callbacks_; }
  MoqtPublisher* publisher() { return publisher_; }
  void set_publisher(MoqtPublisher* publisher) { publisher_ = publisher; }

 private:
  friend class test::MoqtSessionPeer;

  class QUICHE_EXPORT ControlStream : public webtransport::StreamVisitor,
                                      public MoqtParserVisitor {
   public:
    ControlStream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.using_webtrans, *this) {}

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override;
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override;
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override {}

    // MoqtParserVisitor implementation.
    // TODO: Handle a stream FIN.
    void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                         bool end_of_message) override;
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
    MoqtParser parser_;
  };
  class QUICHE_EXPORT IncomingDataStream : public webtransport::StreamVisitor,
                                           public MoqtParserVisitor {
   public:
    IncomingDataStream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.using_webtrans, *this) {}

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
    void OnClientSetupMessage(const MoqtClientSetup&) override {
      OnControlMessageReceived();
    }
    void OnServerSetupMessage(const MoqtServerSetup&) override {
      OnControlMessageReceived();
    }
    void OnSubscribeMessage(const MoqtSubscribe&) override {
      OnControlMessageReceived();
    }
    void OnSubscribeOkMessage(const MoqtSubscribeOk&) override {
      OnControlMessageReceived();
    }
    void OnSubscribeErrorMessage(const MoqtSubscribeError&) override {
      OnControlMessageReceived();
    }
    void OnUnsubscribeMessage(const MoqtUnsubscribe&) override {
      OnControlMessageReceived();
    }
    void OnSubscribeDoneMessage(const MoqtSubscribeDone&) override {
      OnControlMessageReceived();
    }
    void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate&) override {
      OnControlMessageReceived();
    }
    void OnAnnounceMessage(const MoqtAnnounce&) override {
      OnControlMessageReceived();
    }
    void OnAnnounceOkMessage(const MoqtAnnounceOk&) override {
      OnControlMessageReceived();
    }
    void OnAnnounceErrorMessage(const MoqtAnnounceError&) override {
      OnControlMessageReceived();
    }
    void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) override {
      OnControlMessageReceived();
    }
    void OnTrackStatusRequestMessage(
        const MoqtTrackStatusRequest& message) override {
      OnControlMessageReceived();
    }
    void OnUnannounceMessage(const MoqtUnannounce&) override {
      OnControlMessageReceived();
    }
    void OnTrackStatusMessage(const MoqtTrackStatus&) override {
      OnControlMessageReceived();
    }
    void OnGoAwayMessage(const MoqtGoAway&) override {
      OnControlMessageReceived();
    }
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
    MoqtParser parser_;
    std::string partial_object_;
  };
  // Represents a record for a single subscription to a local track that is
  // being sent to the peer.
  class PublishedSubscription : public MoqtObjectListener {
   public:
    explicit PublishedSubscription(
        MoqtSession* session,
        std::shared_ptr<MoqtTrackPublisher> track_publisher,
        const MoqtSubscribe& subscribe);
    ~PublishedSubscription();

    PublishedSubscription(const PublishedSubscription&) = delete;
    PublishedSubscription(PublishedSubscription&&) = delete;
    PublishedSubscription& operator=(const PublishedSubscription&) = delete;
    PublishedSubscription& operator=(PublishedSubscription&&) = delete;

    MoqtTrackPublisher& publisher() { return *track_publisher_; }
    uint64_t track_alias() const { return track_alias_; }
    std::optional<FullSequence> largest_sent() const { return largest_sent_; }

    void OnNewObjectAvailable(FullSequence sequence) override;

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

   private:
    SendStreamMap& stream_map();
    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    void SendDatagram(FullSequence sequence);

    uint64_t subscription_id_;
    MoqtSession* session_;
    std::shared_ptr<MoqtTrackPublisher> track_publisher_;
    uint64_t track_alias_;
    SubscribeWindow window_;
    MoqtPriority subscriber_priority_;
    std::optional<MoqtDeliveryOrder> subscriber_delivery_order_;
    // Largest sequence number ever sent via this subscription.
    std::optional<FullSequence> largest_sent_;
    // Should be almost always accessed via `stream_map()`.
    std::optional<SendStreamMap> lazily_initialized_stream_map_;
  };
  class QUICHE_EXPORT OutgoingDataStream : public webtransport::StreamVisitor {
   public:
    OutgoingDataStream(MoqtSession* session, webtransport::Stream* stream,
                       uint64_t subscription_id, FullSequence first_object);
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
    FullSequence next_object_;
    bool stream_header_written_ = false;
    // A weak pointer to an object owned by the session.  Used to make sure the
    // session does not get called after being destroyed.
    std::weak_ptr<void> session_liveness_;
  };
  // QueuedOutgoingDataStream records an information necessary to create a
  // stream that was attempted to be created before but was blocked due to flow
  // control.
  struct QueuedOutgoingDataStream {
    uint64_t subscription_id;
    FullSequence first_object;
  };

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
  webtransport::Stream* OpenDataStream(uint64_t subscription_id,
                                       FullSequence first_object);

  // Get FullTrackName and visitor for a subscribe_id and track_alias. Returns
  // nullptr if not present.
  std::pair<FullTrackName, RemoteTrack::Visitor*> TrackPropertiesFromAlias(
      const MoqtObject& message);

  webtransport::Session* session_;
  MoqtSessionParameters parameters_;
  MoqtSessionCallbacks callbacks_;
  MoqtFramer framer_;

  std::optional<webtransport::StreamId> control_stream_;
  std::string error_;

  // All the tracks the session is subscribed to, indexed by track_alias.
  // Multiple subscribes to the same track are recorded in a single
  // subscription.
  absl::flat_hash_map<uint64_t, RemoteTrack> remote_tracks_;
  // Look up aliases for remote tracks by name
  absl::flat_hash_map<FullTrackName, uint64_t> remote_track_aliases_;
  uint64_t next_remote_track_alias_ = 0;

  // Application object representing the publisher for all of the tracks that
  // can be subscribed to via this connection.  Must outlive this object.
  MoqtPublisher* publisher_;
  // Subscriptions for local tracks by the remote peer, indexed by subscribe ID.
  absl::flat_hash_map<uint64_t, std::unique_ptr<PublishedSubscription>>
      published_subscriptions_;
  // Keeps track of all the data streams that were supposed to be open, but were
  // blocked by the flow control.
  quiche::QuicheCircularDeque<QueuedOutgoingDataStream>
      queued_outgoing_data_streams_;
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

  // Indexed by track namespace.
  absl::flat_hash_map<std::string, MoqtOutgoingAnnounceCallback>
      pending_outgoing_announces_;

  // The role the peer advertised in its SETUP message. Initialize it to avoid
  // an uninitialized value if no SETUP arrives or it arrives with no Role
  // parameter, and other checks have changed/been disabled.
  MoqtRole peer_role_ = MoqtRole::kPubSub;

  // Must be last.  Token used to make sure that the streams do not call into
  // the session when the session has already been destroyed.
  struct Empty {};
  std::shared_ptr<Empty> liveness_token_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_
