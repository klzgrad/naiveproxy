// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_
#define QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_

#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_live_relay_queue.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moq_chat.h"
#include "quiche/quic/moqt/tools/moqt_server.h"

namespace moqt {

class ChatServer {
 public:
  ChatServer(std::unique_ptr<quic::ProofSource> proof_source,
             absl::string_view chat_id, absl::string_view output_file);
  ~ChatServer();

  class RemoteTrackVisitor : public RemoteTrack::Visitor {
   public:
    explicit RemoteTrackVisitor(ChatServer* server);
    void OnReply(const moqt::FullTrackName& full_track_name,
                 std::optional<absl::string_view> reason_phrase) override;
    void OnCanAckObjects(MoqtObjectAckFunction) override {}
    void OnObjectFragment(
        const moqt::FullTrackName& full_track_name, uint64_t group_sequence,
        uint64_t object_sequence, moqt::MoqtPriority /*publisher_priority*/,
        moqt::MoqtObjectStatus /*status*/,
        moqt::MoqtForwardingPreference /*forwarding_preference*/,
        absl::string_view object, bool end_of_message) override;

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

   private:
    MoqtSession* session_;  // Not owned.
    // This design assumes that each server has exactly one username, although
    // in theory there could be multiple users on one session.
    std::optional<std::string> username_;
    ChatServer* server_;  // Not owned.
    // The iterator of this entry in ChatServer::sessions_, so it can destroy
    // itself later.
    std::list<ChatServerSessionHandler>::const_iterator it_;
  };

  MoqtServer& moqt_server() { return server_; }

  RemoteTrackVisitor* remote_track_visitor() { return &remote_track_visitor_; }

  MoqtOutgoingQueue* catalog() { return catalog_.get(); }

  void AddUser(absl::string_view username);

  void DeleteUser(absl::string_view username);

  void DeleteSession(std::list<ChatServerSessionHandler>::const_iterator it) {
    sessions_.erase(it);
  }

  // Returns false if no output file is set.
  bool WriteToFile(absl::string_view username, absl::string_view message);

  MoqtPublisher* publisher() { return &publisher_; }

  MoqChatStrings& strings() { return strings_; }

  int num_users() const { return user_queues_.size(); }

 private:
  absl::StatusOr<MoqtConfigureSessionCallback> IncomingSessionHandler(
      absl::string_view path);

  MoqtIncomingSessionCallback incoming_session_callback_ =
      [&](absl::string_view path) { return IncomingSessionHandler(path); };

  bool is_running_ = true;
  MoqtServer server_;
  std::list<ChatServerSessionHandler> sessions_;
  MoqChatStrings strings_;
  MoqtKnownTrackPublisher publisher_;
  std::shared_ptr<MoqtOutgoingQueue> catalog_;
  RemoteTrackVisitor remote_track_visitor_;
  // indexed by username
  absl::flat_hash_map<std::string, std::shared_ptr<MoqtLiveRelayQueue>>
      user_queues_;
  std::string output_filename_;
  std::ofstream output_file_;
};

}  // namespace moqt
#endif  // QUICHE_QUIC_MOQT_TOOLS_CHAT_SERVER_H_
