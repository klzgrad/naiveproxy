// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/simple_buffer_allocator.h"
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
using MoqtAnnounceCallback = quiche::SingleUseCallback<void(
    absl::string_view track_namespace,
    std::optional<absl::string_view> error_message)>;

// Callbacks for session-level events.
struct MoqtSessionCallbacks {
  MoqtSessionEstablishedCallback session_established_callback = +[] {};
  MoqtSessionTerminatedCallback session_terminated_callback =
      +[](absl::string_view) {};
  MoqtSessionDeletedCallback session_deleted_callback = +[] {};
};

class QUICHE_EXPORT MoqtSession : public webtransport::SessionVisitor {
 public:
  MoqtSession(webtransport::Session* session, MoqtSessionParameters parameters,
              MoqtSessionCallbacks callbacks)
      : session_(session),
        parameters_(parameters),
        session_established_callback_(
            std::move(callbacks.session_established_callback)),
        session_terminated_callback_(
            std::move(callbacks.session_terminated_callback)),
        session_deleted_callback_(
            std::move(callbacks.session_deleted_callback)),
        framer_(quiche::SimpleBufferAllocator::Get(),
                parameters.using_webtrans) {}
  ~MoqtSession() { std::move(session_deleted_callback_)(); }

  // webtransport::SessionVisitor implementation.
  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode,
                       const std::string&) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view /*datagram*/) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

  void Error(MoqtError code, absl::string_view error);

  quic::Perspective perspective() const { return parameters_.perspective; }

  // Add to the list of tracks that can be subscribed to. Call this before
  // Announce() so that subscriptions can be processed correctly. If |visitor|
  // is nullptr, then incoming SUBSCRIBE for objects in the path will receive
  // SUBSCRIBE_OK, but never actually get the objects.
  void AddLocalTrack(const FullTrackName& full_track_name,
                     LocalTrack::Visitor* visitor);
  // Send an ANNOUNCE message for |track_namespace|, and call
  // |announce_callback| when the response arrives. Will fail immediately if
  // there is already an unresolved ANNOUNCE for that namespace.
  void Announce(absl::string_view track_namespace,
                MoqtAnnounceCallback announce_callback);
  bool HasSubscribers(const FullTrackName& full_track_name) const;

  // Returns true if SUBSCRIBE was sent. If there is already a subscription to
  // the track, the message will still be sent. However, the visitor will be
  // ignored.
  bool SubscribeAbsolute(absl::string_view track_namespace,
                         absl::string_view name, uint64_t start_group,
                         uint64_t start_object, RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  bool SubscribeAbsolute(absl::string_view track_namespace,
                         absl::string_view name, uint64_t start_group,
                         uint64_t start_object, uint64_t end_group,
                         uint64_t end_object, RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  bool SubscribeRelative(absl::string_view track_namespace,
                         absl::string_view name, int64_t start_group,
                         int64_t start_object, RemoteTrack::Visitor* visitor,
                         absl::string_view auth_info = "");
  bool SubscribeCurrentGroup(absl::string_view track_namespace,
                             absl::string_view name,
                             RemoteTrack::Visitor* visitor,
                             absl::string_view auth_info = "");

  // Returns false if it could not open a stream when necessary, or if the
  // track does not exist (there was no call to AddLocalTrack). Will still
  // return false is some streams succeed.
  // Also returns false if |payload_length| exists but is shorter than
  // |payload|.
  // |payload.length() >= |payload_length|, because the application can deliver
  // partial objects.
  bool PublishObject(FullTrackName& full_track_name, uint64_t group_id,
                     uint64_t object_id, uint64_t object_send_order,
                     MoqtForwardingPreference forwarding_preference,
                     absl::string_view payload,
                     std::optional<uint64_t> payload_length,
                     bool end_of_stream);
  // TODO: Add an API to FIN the stream for a particular track/group/object.

 private:
  friend class test::MoqtSessionPeer;
  class QUICHE_EXPORT Stream : public webtransport::StreamVisitor,
                               public MoqtParserVisitor {
   public:
    Stream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.using_webtrans, *this) {}
    Stream(MoqtSession* session, webtransport::Stream* stream,
           bool is_control_stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.using_webtrans, *this),
          is_control_stream_(is_control_stream) {}

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
    void OnSubscribeFinMessage(const MoqtSubscribeFin& /*message*/) override {}
    void OnSubscribeRstMessage(const MoqtSubscribeRst& /*message*/) override {}
    void OnAnnounceMessage(const MoqtAnnounce& message) override;
    void OnAnnounceOkMessage(const MoqtAnnounceOk& message) override;
    void OnAnnounceErrorMessage(const MoqtAnnounceError& message) override;
    void OnUnannounceMessage(const MoqtUnannounce& /*message*/) override {}
    void OnGoAwayMessage(const MoqtGoAway& /*message*/) override {}
    void OnParsingError(MoqtError error_code,
                        absl::string_view reason) override;

    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

    webtransport::Stream* stream() const { return stream_; }

   private:
    friend class test::MoqtSessionPeer;
    void SendSubscribeError(const MoqtSubscribe& message,
                            SubscribeErrorCode error_code,
                            absl::string_view reason_phrase,
                            uint64_t track_alias);
    bool CheckIfIsControlStream();

    MoqtSession* session_;
    webtransport::Stream* stream_;
    MoqtParser parser_;
    // nullopt means "incoming stream, and we don't know if it's the control
    // stream or a data stream yet".
    std::optional<bool> is_control_stream_;
    std::string partial_object_;
  };

  // Returns false if the SUBSCRIBE isn't sent.
  bool Subscribe(MoqtSubscribe& message, RemoteTrack::Visitor* visitor);
  // converts two MoqtLocations into absolute sequences.
  std::optional<FullSequence> LocationToAbsoluteNumber(
      const LocalTrack& track,
      const std::optional<MoqtSubscribeLocation>& group,
      const std::optional<MoqtSubscribeLocation>& object);
  // Returns the stream ID if successful, nullopt if not.
  // TODO: Add a callback if stream creation is delayed.
  std::optional<webtransport::StreamId> OpenUnidirectionalStream();

  webtransport::Session* session_;
  MoqtSessionParameters parameters_;
  MoqtSessionEstablishedCallback session_established_callback_;
  MoqtSessionTerminatedCallback session_terminated_callback_;
  MoqtSessionDeletedCallback session_deleted_callback_;
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

  // All the tracks the peer can subscribe to.
  absl::flat_hash_map<FullTrackName, LocalTrack> local_tracks_;
  // This is only used to check for track_alias collisions.
  absl::flat_hash_set<uint64_t> used_track_aliases_;
  uint64_t next_local_track_alias_ = 0;

  // Indexed by subscribe_id.
  struct ActiveSubscribe {
    MoqtSubscribe message;
    RemoteTrack::Visitor* visitor;
  };
  // Outgoing SUBSCRIBEs that have not received SUBSCRIBE_OK or SUBSCRIBE_ERROR.
  absl::flat_hash_map<uint64_t, ActiveSubscribe> active_subscribes_;
  uint64_t next_subscribe_id_ = 0;

  // Indexed by track namespace.
  absl::flat_hash_map<std::string, MoqtAnnounceCallback>
      pending_outgoing_announces_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_
