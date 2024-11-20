// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/chat_server.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/moqt/moqt_live_relay_queue.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/tools/moq_chat.h"
#include "quiche/quic/moqt/tools/moqt_server.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt {

ChatServer::ChatServerSessionHandler::ChatServerSessionHandler(
    MoqtSession* session, ChatServer* server)
    : session_(session), server_(server) {
  session_->callbacks().incoming_announce_callback =
      [&](FullTrackName track_namespace) {
        FullTrackName track_name = track_namespace;
        track_name.AddElement("");
        std::cout << "Received ANNOUNCE for " << track_namespace.ToString()
                  << "\n";
        username_ = server_->strings().GetUsernameFromFullTrackName(track_name);
        if (username_->empty()) {
          std::cout << "Malformed ANNOUNCE namespace\n";
          return std::nullopt;
        }
        session_->SubscribeCurrentGroup(track_name,
                                        server_->remote_track_visitor());
        server_->AddUser(*username_);
        return std::nullopt;
      };
  // TODO(martinduke): Add a callback for UNANNOUNCE that deletes the user and
  // clears username_, but keeps the handler.
  session_->callbacks().session_terminated_callback =
      [&](absl::string_view error_message) {
        std::cout << "Session terminated, reason = " << error_message << "\n";
        session_ = nullptr;
        server_->DeleteSession(it_);
      };
  session_->set_publisher(server_->publisher());
}

ChatServer::ChatServerSessionHandler::~ChatServerSessionHandler() {
  if (!server_->is_running_) {
    return;
  }
  if (username_.has_value()) {
    server_->DeleteUser(*username_);
  }
}

ChatServer::RemoteTrackVisitor::RemoteTrackVisitor(ChatServer* server)
    : server_(server) {}

void ChatServer::RemoteTrackVisitor::OnReply(
    const moqt::FullTrackName& full_track_name,
    std::optional<absl::string_view> reason_phrase) {
  std::cout << "Subscription to user "
            << server_->strings().GetUsernameFromFullTrackName(full_track_name)
            << " ";
  if (reason_phrase.has_value()) {
    std::cout << "REJECTED, reason = " << *reason_phrase << "\n";
    std::string username =
        server_->strings().GetUsernameFromFullTrackName(full_track_name);
    if (!username.empty()) {
      std::cout << "Rejection was for malformed namespace\n";
      return;
    }
    server_->DeleteUser(username);
  } else {
    std::cout << "ACCEPTED\n";
  }
}

void ChatServer::RemoteTrackVisitor::OnObjectFragment(
    const moqt::FullTrackName& full_track_name, uint64_t group_sequence,
    uint64_t object_sequence, moqt::MoqtPriority /*publisher_priority*/,
    moqt::MoqtObjectStatus status,
    moqt::MoqtForwardingPreference /*forwarding_preference*/,
    absl::string_view object, bool end_of_message) {
  if (!end_of_message) {
    std::cerr << "Error: received partial message despite requesting "
                 "buffering\n";
  }
  std::string username =
      server_->strings().GetUsernameFromFullTrackName(full_track_name);
  if (username.empty()) {
    std::cout << "Received user message with malformed namespace\n";
    return;
  }
  auto it = server_->user_queues_.find(username);
  if (it == server_->user_queues_.end()) {
    std::cerr << "Error: received message for unknown user " << username
              << "\n";
    return;
  }
  if (status != MoqtObjectStatus::kNormal) {
    it->second->AddObject(group_sequence, object_sequence, status, "");
    return;
  }
  if (!server_->WriteToFile(username, object)) {
    std::cout << username << ": " << object << "\n\n";
  }
  it->second->AddObject(group_sequence, object_sequence, status, object);
}

ChatServer::ChatServer(std::unique_ptr<quic::ProofSource> proof_source,
                       absl::string_view chat_id, absl::string_view output_file)
    : server_(std::move(proof_source), std::move(incoming_session_callback_)),
      strings_(chat_id),
      catalog_(std::make_shared<MoqtOutgoingQueue>(
          strings_.GetCatalogName(), MoqtForwardingPreference::kSubgroup)),
      remote_track_visitor_(this) {
  catalog_->AddObject(quiche::QuicheMemSlice(quiche::QuicheBuffer::Copy(
                          quiche::SimpleBufferAllocator::Get(),
                          MoqChatStrings::kCatalogHeader)),
                      /*key=*/true);
  publisher_.Add(catalog_);
  if (!output_file.empty()) {
    output_filename_ = output_file;
  }
  if (!output_filename_.empty()) {
    output_file_.open(output_filename_);
    output_file_ << "Chat transcript:\n";
    output_file_.flush();
  }
}

ChatServer::~ChatServer() {
  // Kill all sessions so that the callback doesn't fire when the server is
  // destroyed.
  is_running_ = false;
  server_.quic_server().Shutdown();
}

void ChatServer::AddUser(absl::string_view username) {
  std::string catalog_data = absl::StrCat("+", username);
  catalog_->AddObject(quiche::QuicheMemSlice(quiche::QuicheBuffer::Copy(
                          quiche::SimpleBufferAllocator::Get(), catalog_data)),
                      /*key=*/false);
  // Add a local track.
  user_queues_[username] = std::make_shared<MoqtLiveRelayQueue>(
      strings_.GetFullTrackNameFromUsername(username),
      MoqtForwardingPreference::kSubgroup);
  publisher_.Add(user_queues_[username]);
}

void ChatServer::DeleteUser(absl::string_view username) {
  // Delete from Catalog.
  std::string catalog_data = absl::StrCat("-", username);
  catalog_->AddObject(quiche::QuicheMemSlice(quiche::QuicheBuffer::Copy(
                          quiche::SimpleBufferAllocator::Get(), catalog_data)),
                      /*key=*/false);
  user_queues_.erase(username);
  publisher_.Delete(strings_.GetFullTrackNameFromUsername(username));
}

bool ChatServer::WriteToFile(absl::string_view username,
                             absl::string_view message) {
  if (!output_filename_.empty()) {
    output_file_ << username << ": " << message << "\n\n";
    output_file_.flush();
    return true;
  }
  return false;
}

absl::StatusOr<MoqtConfigureSessionCallback> ChatServer::IncomingSessionHandler(
    absl::string_view path) {
  if (!strings_.IsValidPath(path)) {
    return absl::NotFoundError("Unknown endpoint; try \"/moq-chat\".");
  }
  return [this](MoqtSession* session) {
    sessions_.emplace_front(session, this);
    // Add a self-reference so it can delete itself from ChatServer::sessions_.
    sessions_.front().set_iterator(sessions_.cbegin());
  };
}

}  // namespace moqt
