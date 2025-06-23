// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/chat_server.h"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/moqt/moqt_live_relay_queue.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/tools/moq_chat.h"
#include "quiche/quic/moqt/tools/moqt_server.h"

namespace moqt::moq_chat {

std::optional<MoqtAnnounceErrorReason>
ChatServer::ChatServerSessionHandler::OnIncomingAnnounce(
    const moqt::FullTrackName& track_namespace,
    std::optional<VersionSpecificParameters> parameters) {
  if (track_name_.has_value() &&
      GetUserNamespace(*track_name_) != track_namespace) {
    // ChatServer only supports one track per client session at a time. Return
    // ANNOUNCE_OK and exit.
    return std::nullopt;
  }
  // Accept the ANNOUNCE regardless of the chat_id.
  track_name_ = ConstructTrackNameFromNamespace(track_namespace,
                                                GetChatId(track_namespace));
  if (!track_name_.has_value()) {
    std::cout << "Malformed ANNOUNCE namespace\n";
    return MoqtAnnounceErrorReason(RequestErrorCode::kTrackDoesNotExist,
                                   "Not a valid namespace for this chat.");
  }
  if (!parameters.has_value()) {
    std::cout << "Received UNANNOUNCE for " << track_namespace.ToString()
              << "\n";
    server_->DeleteUser(*track_name_);
    track_name_.reset();
    return std::nullopt;
  }
  std::cout << "Received ANNOUNCE for " << track_namespace.ToString() << "\n";
  session_->SubscribeCurrentObject(*track_name_,
                                   server_->remote_track_visitor(),
                                   moqt::VersionSpecificParameters());
  server_->AddUser(*track_name_);
  return std::nullopt;
}

void ChatServer::ChatServerSessionHandler::OnOutgoingAnnounceReply(
    FullTrackName track_namespace,
    std::optional<MoqtAnnounceErrorReason> error_message) {
  // Log the result; the server doesn't really care.
  std::cout << "ANNOUNCE for " << track_namespace.ToString();
  if (error_message.has_value()) {
    std::cout << " failed with error: " << error_message->reason_phrase << "\n";
  } else {
    std::cout << " succeeded\n";
  }
}

ChatServer::ChatServerSessionHandler::ChatServerSessionHandler(
    MoqtSession* session, ChatServer* server)
    : session_(session), server_(server) {
  session_->callbacks().incoming_announce_callback = absl::bind_front(
      &ChatServer::ChatServerSessionHandler::OnIncomingAnnounce, this);
  session_->callbacks().session_terminated_callback =
      [this](absl::string_view error_message) {
        std::cout << "Session terminated, reason = " << error_message << "\n";
        session_ = nullptr;
        if (track_name_.has_value()) {
          server_->DeleteUser(*track_name_);
        }
      };
  session_->callbacks().incoming_subscribe_announces_callback =
      [this](const moqt::FullTrackName& chat_namespace,
             std::optional<VersionSpecificParameters> parameters) {
        if (parameters.has_value()) {
          subscribed_namespaces_.insert(chat_namespace);
          std::cout << "Received SUBSCRIBE_ANNOUNCES for ";
        } else {
          subscribed_namespaces_.erase(chat_namespace);
          std::cout << "Received UNSUBSCRIBE_ANNOUNCES for ";
        }
        std::cout << chat_namespace.ToString() << "\n";
        if (!IsValidChatNamespace(chat_namespace)) {
          std::cout << "Not a valid moq-chat namespace.\n";
          return std::make_optional(
              MoqtSubscribeErrorReason{RequestErrorCode::kTrackDoesNotExist,
                                       "Not a valid namespace for this chat."});
        }
        if (!parameters.has_value()) {
          return std::optional<MoqtSubscribeErrorReason>();
        }
        // Send all ANNOUNCE.
        for (auto& [track_name, queue] : server_->user_queues_) {
          std::cout << "Sending ANNOUNCE for "
                    << GetUserNamespace(track_name).ToString() << "\n";
          if (track_name_.has_value() &&
              GetUsername(*track_name_) == GetUsername(track_name)) {
            // Don't ANNOUNCE a client to itself.
            continue;
          }
          session_->Announce(
              GetUserNamespace(track_name),
              absl::bind_front(&ChatServer::ChatServerSessionHandler::
                                   OnOutgoingAnnounceReply,
                               this),
              moqt::VersionSpecificParameters());
        }
        return std::optional<MoqtSubscribeErrorReason>();
      };
  session_->set_publisher(server_->publisher());
}

ChatServer::ChatServerSessionHandler::~ChatServerSessionHandler() {
  if (!server_->is_running_) {
    return;
  }
  if (track_name_.has_value()) {
    server_->DeleteUser(*track_name_);
  }
}

ChatServer::RemoteTrackVisitor::RemoteTrackVisitor(ChatServer* server)
    : server_(server) {}

void ChatServer::RemoteTrackVisitor::OnReply(
    const moqt::FullTrackName& full_track_name,
    std::optional<Location> /*largest_id*/,
    std::optional<absl::string_view> reason_phrase) {
  std::cout << "Subscription to " << full_track_name.ToString();
  if (reason_phrase.has_value()) {
    std::cout << " REJECTED, reason = " << *reason_phrase << "\n";
    server_->DeleteUser(full_track_name);
  } else {
    std::cout << " ACCEPTED\n";
  }
}

void ChatServer::RemoteTrackVisitor::OnObjectFragment(
    const moqt::FullTrackName& full_track_name, moqt::Location sequence,
    moqt::MoqtPriority /*publisher_priority*/, moqt::MoqtObjectStatus status,
    absl::string_view object, bool end_of_message) {
  if (!end_of_message) {
    std::cerr << "Error: received partial message despite requesting "
                 "buffering\n";
  }
  auto it = server_->user_queues_.find(full_track_name);
  if (it == server_->user_queues_.end()) {
    std::cerr << "Error: received message for unknown track "
              << full_track_name.ToString() << "\n";
    return;
  }
  if (status != MoqtObjectStatus::kNormal) {
    it->second->AddObject(sequence, status);
    return;
  }
  if (!server_->WriteToFile(GetUsername(full_track_name), object)) {
    std::cout << GetUsername(full_track_name) << ": " << object << "\n\n";
  }
  it->second->AddObject(sequence, object);
}

ChatServer::ChatServer(std::unique_ptr<quic::ProofSource> proof_source,
                       absl::string_view output_file)
    : server_(std::move(proof_source), std::move(incoming_session_callback_)),
      remote_track_visitor_(this) {
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

void ChatServer::AddUser(FullTrackName track_name) {
  // Add a local track.
  user_queues_[track_name] = std::make_shared<MoqtLiveRelayQueue>(
      track_name, MoqtForwardingPreference::kSubgroup);
  publisher_.Add(user_queues_[track_name]);
  FullTrackName track_namespace = track_name;
  track_namespace.NameToNamespace();
  for (auto& session : sessions_) {
    session.AnnounceIfSubscribed(track_namespace);
  }
}

void ChatServer::DeleteUser(FullTrackName track_name) {
  if (!is_running_) {
    return;
  }
  // RemoveAllSubscriptions() sends a SUBSCRIBE_DONE for each.
  user_queues_[track_name]->RemoveAllSubscriptions();
  user_queues_.erase(track_name);
  publisher_.Delete(track_name);
  FullTrackName track_namespace = GetUserNamespace(track_name);
  for (auto& session : sessions_) {
    session.UnannounceIfSubscribed(track_namespace);
  }
  if (user_queues_.empty()) {
    std::cout << "No more users!\n";
  }
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
  if (!IsValidPath(path)) {
    return absl::NotFoundError("Unknown endpoint; try \"/moq-chat\".");
  }
  return [this](MoqtSession* session) {
    sessions_.emplace_front(session, this);
    // Add a self-reference so it can delete itself from ChatServer::sessions_.
    sessions_.front().set_iterator(sessions_.cbegin());
  };
}

}  // namespace moqt::moq_chat
