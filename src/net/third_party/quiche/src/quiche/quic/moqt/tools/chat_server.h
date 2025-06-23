// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_
#define QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_

#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_live_relay_queue.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_server.h"

namespace moqt {
namespace moq_chat {

class ChatServer {
 public:
  ChatServer(std::unique_ptr<quic::ProofSource> proof_source,
             absl::string_view output_file);
  ~ChatServer();

  class RemoteTrackVisitor : public SubscribeRemoteTrack::Visitor {
   public:
    explicit RemoteTrackVisitor(ChatServer* server);
    void OnReply(const moqt::FullTrackName& full_track_name,
                 std::optional<Location> largest_id,
                 std::optional<absl::string_view> reason_phrase) override;
    void OnCanAckObjects(MoqtObjectAckFunction) override {}
    void OnObjectFragment(const moqt::FullTrackName& full_track_name,
                          Location sequence,
                          moqt::MoqtPriority /*publisher_priority*/,
                          moqt::MoqtObjectStatus /*status*/,
                          absl::string_view object,
                          bool end_of_message) override;
    void OnSubscribeDone(FullTrackName /*full_track_name*/) override {}

   private:
    ChatServer* server_;
  };

  class ChatServerSessionHandler {
   public:
    ChatServerSessionHandler(MoqtSession* session, ChatServer* server);
    ~ChatServerSessionHandler();

    void set_iterator(
        const std::list<ChatServerSessionHandler>::const_iterator it) {
      it_ = it;
    }

    void AnnounceIfSubscribed(FullTrackName track_namespace) {
      for (const FullTrackName& subscribed_namespace : subscribed_namespaces_) {
        if (track_namespace.InNamespace(subscribed_namespace)) {
          session_->Announce(
              track_namespace,
              absl::bind_front(&ChatServer::ChatServerSessionHandler::
                                   OnOutgoingAnnounceReply,
                               this),
              VersionSpecificParameters());
          return;
        }
      }
    }

    void UnannounceIfSubscribed(FullTrackName track_namespace) {
      for (const FullTrackName& subscribed_namespace : subscribed_namespaces_) {
        if (track_namespace.InNamespace(subscribed_namespace)) {
          session_->Unannounce(track_namespace);
          return;
        }
      }
    }

   private:
    // Callback for incoming announces.
    std::optional<MoqtAnnounceErrorReason> OnIncomingAnnounce(
        const moqt::FullTrackName& track_namespace,
        std::optional<VersionSpecificParameters> parameters);
    void OnOutgoingAnnounceReply(
        FullTrackName track_namespace,
        std::optional<MoqtAnnounceErrorReason> error_message);

    MoqtSession* session_;  // Not owned.
    // This design assumes that each server has exactly one username, although
    // in theory there could be multiple users on one session.
    std::optional<FullTrackName> track_name_;
    ChatServer* server_;  // Not owned.
    absl::flat_hash_set<FullTrackName> subscribed_namespaces_;
    // The iterator of this entry in ChatServer::sessions_, so it can destroy
    // itself later.
    std::list<ChatServerSessionHandler>::const_iterator it_;
  };

  MoqtServer& moqt_server() { return server_; }

  RemoteTrackVisitor* remote_track_visitor() { return &remote_track_visitor_; }

  void AddUser(FullTrackName track_name);

  void DeleteUser(FullTrackName track_name);

  void DeleteSession(std::list<ChatServerSessionHandler>::const_iterator it) {
    sessions_.erase(it);
  }

  // Returns false if no output file is set.
  bool WriteToFile(absl::string_view username, absl::string_view message);

  MoqtPublisher* publisher() { return &publisher_; }

  int num_users() const { return user_queues_.size(); }

 private:
  absl::StatusOr<MoqtConfigureSessionCallback> IncomingSessionHandler(
      absl::string_view path);

  MoqtIncomingSessionCallback incoming_session_callback_ =
      [&](absl::string_view path) { return IncomingSessionHandler(path); };

  bool is_running_ = true;
  MoqtServer server_;
  std::list<ChatServerSessionHandler> sessions_;
  MoqtKnownTrackPublisher publisher_;
  RemoteTrackVisitor remote_track_visitor_;
  absl::flat_hash_map<FullTrackName, std::shared_ptr<MoqtLiveRelayQueue>>
      user_queues_;
  std::string output_filename_;
  std::ofstream output_file_;
};

}  // namespace moq_chat
}  // namespace moqt
#endif  // QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_
