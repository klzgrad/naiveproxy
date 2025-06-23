// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/chat_client.h"

#include <poll.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/tools/moq_chat.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt::moq_chat {

std::optional<MoqtAnnounceErrorReason> ChatClient::OnIncomingAnnounce(
    const moqt::FullTrackName& track_namespace,
    std::optional<VersionSpecificParameters> parameters) {
  if (track_namespace == GetUserNamespace(my_track_name_)) {
    // Ignore ANNOUNCE for my own track.
    return std::optional<MoqtAnnounceErrorReason>();
  }
  std::optional<FullTrackName> track_name = ConstructTrackNameFromNamespace(
      track_namespace, GetChatId(my_track_name_));
  if (!parameters.has_value()) {
    std::cout << "UNANNOUNCE for " << track_namespace.ToString() << "\n";
    if (track_name.has_value() && other_users_.contains(*track_name)) {
      session_->Unsubscribe(*track_name);
      other_users_.erase(*track_name);
    }
    return std::nullopt;
  }
  std::cout << "ANNOUNCE for " << track_namespace.ToString() << "\n";
  if (!track_name.has_value()) {
    std::cout << "ANNOUNCE rejected, invalid namespace\n";
    return std::make_optional<MoqtAnnounceErrorReason>(
        RequestErrorCode::kTrackDoesNotExist, "Not a subscribed namespace");
  }
  if (other_users_.contains(*track_name)) {
    std::cout << "Duplicate ANNOUNCE, send OK and ignore\n";
    return std::nullopt;
  }
  if (GetUsername(my_track_name_) == GetUsername(*track_name)) {
    std::cout << "ANNOUNCE for a previous instance of my track, "
                 "do not subscribe\n";
    return std::nullopt;
  }
  VersionSpecificParameters subscribe_parameters(
      AuthTokenType::kOutOfBand, std::string(GetUsername(my_track_name_)));
  if (session_->SubscribeCurrentObject(*track_name, &remote_track_visitor_,
                                       subscribe_parameters)) {
    ++subscribes_to_make_;
    other_users_.emplace(*track_name);
  }
  return std::nullopt;  // Send ANNOUNCE_OK.
}

ChatClient::ChatClient(const quic::QuicServerId& server_id,
                       bool ignore_certificate,
                       std::unique_ptr<ChatUserInterface> interface,
                       absl::string_view chat_id, absl::string_view username,
                       absl::string_view localhost,
                       quic::QuicEventLoop* event_loop)
    : my_track_name_(ConstructTrackName(chat_id, username, localhost)),
      event_loop_(event_loop),
      remote_track_visitor_(this),
      interface_(std::move(interface)) {
  if (event_loop_ == nullptr) {
    quic::QuicDefaultClock* clock = quic::QuicDefaultClock::Get();
    local_event_loop_ = quic::GetDefaultEventLoop()->Create(clock);
    event_loop_ = local_event_loop_.get();
  }

  quic::QuicSocketAddress peer_address =
      quic::tools::LookupAddress(AF_UNSPEC, server_id);
  std::unique_ptr<quic::ProofVerifier> verifier;
  if (ignore_certificate) {
    verifier = std::make_unique<quic::FakeProofVerifier>();
  } else {
    verifier = quic::CreateDefaultProofVerifier(server_id.host());
  }

  client_ = std::make_unique<MoqtClient>(peer_address, server_id,
                                         std::move(verifier), event_loop_);
  session_callbacks_.session_established_callback = [this]() {
    std::cout << "Session established\n";
    session_is_open_ = true;
  };
  session_callbacks_.goaway_received_callback =
      [](absl::string_view new_session_uri) {
        std::cout << "GoAway received, new session uri = " << new_session_uri
                  << "\n";
        // TODO (martinduke): Connect to the new session uri.
      };
  session_callbacks_.session_terminated_callback =
      [this](absl::string_view error_message) {
        std::cerr << "Closed session, reason = " << error_message << "\n";
        session_is_open_ = false;
        connect_failed_ = true;
        session_ = nullptr;
      };
  session_callbacks_.session_deleted_callback = [this]() {
    session_ = nullptr;
  };
  session_callbacks_.incoming_announce_callback =
      absl::bind_front(&ChatClient::OnIncomingAnnounce, this);
  interface_->Initialize(
      [this](absl::string_view input_message) {
        OnTerminalLineInput(input_message);
      },
      event_loop_);
}

bool ChatClient::Connect(absl::string_view path) {
  client_->Connect(std::string(path), std::move(session_callbacks_));
  while (!session_is_open_ && !connect_failed_) {
    RunEventLoop();
  }
  return (!connect_failed_);
}

void ChatClient::OnTerminalLineInput(absl::string_view input_message) {
  if (input_message.empty()) {
    return;
  }
  if (input_message == "/exit") {
    // Clean teardown of SUBSCRIBE_ANNOUNCES, ANNOUNCE, SUBSCRIBE.
    session_->UnsubscribeAnnounces(GetChatNamespace(my_track_name_));
    session_->Unannounce(GetUserNamespace(my_track_name_));
    for (const auto& track_name : other_users_) {
      session_->Unsubscribe(track_name);
    }
    other_users_.clear();
    session_is_open_ = false;
    return;
  }
  quiche::QuicheMemSlice message_slice(quiche::QuicheBuffer::Copy(
      quiche::SimpleBufferAllocator::Get(), input_message));
  queue_->AddObject(std::move(message_slice), /*key=*/true);
}

void ChatClient::RemoteTrackVisitor::OnReply(
    const FullTrackName& full_track_name,
    std::optional<Location> /*largest_id*/,
    std::optional<absl::string_view> reason_phrase) {
  auto it = client_->other_users_.find(full_track_name);
  if (it == client_->other_users_.end()) {
    std::cout << "Error: received reply for unknown user "
              << full_track_name.ToString() << "\n";
    return;
  }
  --client_->subscribes_to_make_;
  std::cout << "Subscription to user " << GetUsername(*it) << " ";
  if (reason_phrase.has_value()) {
    std::cout << "REJECTED, reason = " << *reason_phrase << "\n";
    client_->other_users_.erase(it);
  } else {
    std::cout << "ACCEPTED\n";
  }
}

void ChatClient::RemoteTrackVisitor::OnObjectFragment(
    const FullTrackName& full_track_name, Location /*sequence*/,
    MoqtPriority /*publisher_priority*/, MoqtObjectStatus /*status*/,
    absl::string_view object, bool end_of_message) {
  if (!end_of_message) {
    std::cerr << "Error: received partial message despite requesting "
                 "buffering\n";
  }
  auto it = client_->other_users_.find(full_track_name);
  if (it == client_->other_users_.end()) {
    std::cout << "Error: received message for unknown user "
              << full_track_name.ToString() << "\n";
    return;
  }
  if (object.empty()) {
    return;
  }
  client_->WriteToOutput(GetUsername(*it), object);
}

bool ChatClient::AnnounceAndSubscribeAnnounces() {
  session_ = client_->session();
  if (session_ == nullptr) {
    std::cout << "Failed to connect.\n";
    return false;
  }
  // TODO: A server log might choose to not provide a username, thus getting all
  // the messages without adding itself to the catalog.
  queue_ = std::make_shared<MoqtOutgoingQueue>(
      my_track_name_, MoqtForwardingPreference::kSubgroup);
  publisher_.Add(queue_);
  session_->set_publisher(&publisher_);
  MoqtOutgoingAnnounceCallback announce_callback =
      [this](FullTrackName track_namespace,
             std::optional<MoqtAnnounceErrorReason> reason) {
        if (reason.has_value()) {
          std::cout << "ANNOUNCE rejected, " << reason->reason_phrase << "\n";
          session_->Error(MoqtError::kInternalError, "Local ANNOUNCE rejected");
          return;
        }
        std::cout << "ANNOUNCE for " << track_namespace.ToString()
                  << " accepted\n";
        return;
      };
  std::cout << "Announcing " << GetUserNamespace(my_track_name_).ToString()
            << "\n";
  session_->Announce(GetUserNamespace(my_track_name_),
                     std::move(announce_callback), VersionSpecificParameters());

  // Send SUBSCRIBE_ANNOUNCE. Pop 3 levels of namespace to get to {moq-chat,
  // chat-id}
  MoqtOutgoingSubscribeAnnouncesCallback subscribe_announces_callback =
      [this](FullTrackName track_namespace,
             std::optional<RequestErrorCode> error, absl::string_view reason) {
        if (error.has_value()) {
          std::cout << "SUBSCRIBE_ANNOUNCES rejected, " << reason << "\n";
          session_->Error(MoqtError::kInternalError,
                          "Local SUBSCRIBE_ANNOUNCES rejected");
          return;
        }
        std::cout << "SUBSCRIBE_ANNOUNCES for " << track_namespace.ToString()
                  << " accepted\n";
        return;
      };
  VersionSpecificParameters parameters(
      AuthTokenType::kOutOfBand, std::string(GetUsername(my_track_name_)));
  session_->SubscribeAnnounces(GetChatNamespace(my_track_name_),
                               std::move(subscribe_announces_callback),
                               parameters);

  while (session_is_open_ && is_syncing()) {
    RunEventLoop();
  }
  return session_is_open_;
}

}  // namespace moqt::moq_chat
