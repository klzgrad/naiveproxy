// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/chat_server.h"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_live_relay_queue.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/tools/moq_chat.h"
#include "quiche/quic/moqt/tools/moqt_server.h"

namespace moqt::moq_chat {

void ChatServer::ChatServerSessionHandler::OnIncomingPublishNamespace(
    const moqt::TrackNamespace& track_namespace,
    std::optional<VersionSpecificParameters> parameters,
    MoqtResponseCallback callback) {
  if (track_name_.has_value() &&
      GetUserNamespace(*track_name_) != track_namespace) {
    // ChatServer only supports one track per client session at a time. Return
    // PUBLISH_NAMESPACE_OK and exit.
    std::move(callback)(std::nullopt);
    return;
  }
  // Accept the PUBLISH_NAMESPACE regardless of the chat_id.
  track_name_ = ConstructTrackNameFromNamespace(track_namespace,
                                                GetChatId(track_namespace));
  if (!track_name_.has_value()) {
    std::cout << "Malformed PUBLISH_NAMESPACE namespace\n";
    std::move(callback)(MoqtPublishNamespaceErrorReason(
        RequestErrorCode::kTrackDoesNotExist,
        "Not a valid namespace for this chat."));
    return;
  }
  if (!parameters.has_value()) {
    std::cout << "Received PUBLISH_NAMESPACE_DONE for "
              << track_namespace.ToString() << "\n";
    server_->DeleteUser(*track_name_);
    track_name_.reset();
    return;
  }
  std::cout << "Received PUBLISH_NAMESPACE for " << track_namespace.ToString()
            << "\n";
  session_->SubscribeCurrentObject(*track_name_,
                                   server_->remote_track_visitor(),
                                   moqt::VersionSpecificParameters());
  server_->AddUser(*track_name_);
  std::move(callback)(std::nullopt);
}

void ChatServer::ChatServerSessionHandler::OnOutgoingPublishNamespaceReply(
    TrackNamespace track_namespace,
    std::optional<MoqtPublishNamespaceErrorReason> error_message) {
  // Log the result; the server doesn't really care.
  std::cout << "PUBLISH_NAMESPACE for " << track_namespace.ToString();
  if (error_message.has_value()) {
    std::cout << " failed with error: " << error_message->reason_phrase << "\n";
  } else {
    std::cout << " succeeded\n";
  }
}

ChatServer::ChatServerSessionHandler::ChatServerSessionHandler(
    MoqtSession* session, ChatServer* server)
    : session_(session), server_(server) {
  session_->callbacks().incoming_publish_namespace_callback = absl::bind_front(
      &ChatServer::ChatServerSessionHandler::OnIncomingPublishNamespace, this);
  session_->callbacks().session_terminated_callback =
      [this](absl::string_view error_message) {
        std::cout << "Session terminated, reason = " << error_message << "\n";
        session_ = nullptr;
        if (track_name_.has_value()) {
          server_->DeleteUser(*track_name_);
        }
      };
  session_->callbacks().incoming_subscribe_namespace_callback =
      [this](const moqt::TrackNamespace& chat_namespace,
             std::optional<VersionSpecificParameters> parameters,
             MoqtResponseCallback callback) {
        if (parameters.has_value()) {
          subscribed_namespaces_.insert(chat_namespace);
          std::cout << "Received SUBSCRIBE_NAMESPACE for ";
        } else {
          subscribed_namespaces_.erase(chat_namespace);
          std::cout << "Received UNSUBSCRIBE_NAMESPACE for ";
        }
        std::cout << chat_namespace.ToString() << "\n";
        if (!IsValidChatNamespace(chat_namespace)) {
          std::cout << "Not a valid moq-chat namespace.\n";
          std::move(callback)(
              MoqtRequestError{RequestErrorCode::kTrackDoesNotExist,
                               "Not a valid namespace for this chat."});
          return;
        }
        if (!parameters.has_value()) {  // UNSUBSCRIBE_NAMESPACE
          return;
        }
        // Send all PUBLISH_NAMESPACE.
        for (auto& [track_name, queue] : server_->user_queues_) {
          std::cout << "Sending PUBLISH_NAMESPACE for "
                    << GetUserNamespace(track_name).ToString() << "\n";
          if (track_name_.has_value() &&
              GetUsername(*track_name_) == GetUsername(track_name)) {
            // Don't PUBLISH_NAMESPACE a client to itself.
            continue;
          }
          session_->PublishNamespace(
              GetUserNamespace(track_name),
              absl::bind_front(&ChatServer::ChatServerSessionHandler::
                                   OnOutgoingPublishNamespaceReply,
                               this),
              moqt::VersionSpecificParameters());
        }
        std::move(callback)(std::nullopt);
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
    std::variant<SubscribeOkData, MoqtRequestError> response) {
  std::cout << "Subscription to " << full_track_name.ToString();
  if (std::holds_alternative<MoqtRequestError>(response)) {
    std::cout << " REJECTED, reason = "
              << std::get<MoqtRequestError>(response).reason_phrase << "\n";
    server_->DeleteUser(full_track_name);
  } else {
    std::cout << " ACCEPTED\n";
  }
}

void ChatServer::RemoteTrackVisitor::OnObjectFragment(
    const moqt::FullTrackName& full_track_name,
    const PublishedObjectMetadata& metadata, absl::string_view object,
    bool end_of_message) {
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
  if (metadata.status != MoqtObjectStatus::kNormal) {
    it->second->AddObject(metadata, "", /*fin=*/false);
    return;
  }
  if (!server_->WriteToFile(GetUsername(full_track_name), object)) {
    std::cout << GetUsername(full_track_name) << ": " << object << "\n\n";
  }
  it->second->AddObject(metadata, object, /*fin=*/false);
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
      track_name, MoqtForwardingPreference::kSubgroup,
      MoqtDeliveryOrder::kAscending, quic::QuicTime::Infinite());
  publisher_.Add(user_queues_[track_name]);
  for (auto& session : sessions_) {
    session.PublishNamespaceIfSubscribed(track_name.track_namespace());
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
  TrackNamespace track_namespace = GetUserNamespace(track_name);
  for (auto& session : sessions_) {
    session.PublishNamespaceDoneIfSubscribed(track_namespace);
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
