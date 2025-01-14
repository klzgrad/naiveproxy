// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/chat_client.h"

#include <poll.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_split.h"
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
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt {

ChatClient::ChatClient(const quic::QuicServerId& server_id,
                       bool ignore_certificate,
                       std::unique_ptr<ChatUserInterface> interface,
                       quic::QuicEventLoop* event_loop)
    : event_loop_(event_loop), interface_(std::move(interface)) {
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
  session_callbacks_.session_terminated_callback =
      [this](absl::string_view error_message) {
        std::cerr << "Closed session, reason = " << error_message << "\n";
        session_is_open_ = false;
        connect_failed_ = true;
      };
  session_callbacks_.session_deleted_callback = [this]() {
    session_ = nullptr;
  };
  interface_->Initialize(
      [this](absl::string_view input_message) {
        OnTerminalLineInput(input_message);
      },
      event_loop_);
}

bool ChatClient::Connect(absl::string_view path, absl::string_view username,
                         absl::string_view chat_id) {
  username_ = username;
  chat_strings_.emplace(chat_id);
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
    session_is_open_ = false;
    return;
  }
  quiche::QuicheMemSlice message_slice(quiche::QuicheBuffer::Copy(
      quiche::SimpleBufferAllocator::Get(), input_message));
  queue_->AddObject(std::move(message_slice), /*key=*/true);
}

void ChatClient::RemoteTrackVisitor::OnReply(
    const FullTrackName& full_track_name,
    std::optional<absl::string_view> reason_phrase) {
  client_->subscribes_to_make_--;
  if (full_track_name == client_->chat_strings_->GetCatalogName()) {
    std::cout << "Subscription to catalog ";
  } else {
    std::cout << "Subscription to user " << full_track_name.ToString() << " ";
  }
  if (reason_phrase.has_value()) {
    std::cout << "REJECTED, reason = " << *reason_phrase << "\n";
  } else {
    std::cout << "ACCEPTED\n";
  }
}

void ChatClient::RemoteTrackVisitor::OnObjectFragment(
    const FullTrackName& full_track_name, FullSequence sequence,
    MoqtPriority /*publisher_priority*/, MoqtObjectStatus /*status*/,
    MoqtForwardingPreference /*forwarding_preference*/,
    absl::string_view object, bool end_of_message) {
  if (!end_of_message) {
    std::cerr << "Error: received partial message despite requesting "
                 "buffering\n";
  }
  if (full_track_name == client_->chat_strings_->GetCatalogName()) {
    if (sequence.group < client_->catalog_group_) {
      std::cout << "Ignoring old catalog";
      return;
    }
    client_->ProcessCatalog(object, this, sequence.group, sequence.object);
    return;
  }
  std::string username(
      client_->chat_strings_->GetUsernameFromFullTrackName(full_track_name));
  if (!client_->other_users_.contains(username)) {
    std::cout << "Username " << username << "doesn't exist\n";
    return;
  }
  if (object.empty()) {
    return;
  }
  client_->WriteToOutput(username, object);
}

bool ChatClient::AnnounceAndSubscribe() {
  session_ = client_->session();
  if (session_ == nullptr) {
    std::cout << "Failed to connect.\n";
    return false;
  }
  if (!username_.empty()) {
    // A server log might choose to not provide a username, thus getting all
    // the messages without adding itself to the catalog.
    FullTrackName my_track_name =
        chat_strings_->GetFullTrackNameFromUsername(username_);
    queue_ = std::make_shared<MoqtOutgoingQueue>(
        my_track_name, MoqtForwardingPreference::kSubgroup);
    publisher_.Add(queue_);
    session_->set_publisher(&publisher_);
    MoqtOutgoingAnnounceCallback announce_callback =
        [this](FullTrackName track_namespace,
               std::optional<MoqtAnnounceErrorReason> reason) {
          if (reason.has_value()) {
            std::cout << "ANNOUNCE rejected, " << reason->reason_phrase << "\n";
            session_->Error(MoqtError::kInternalError,
                            "Local ANNOUNCE rejected");
            return;
          }
          std::cout << "ANNOUNCE for " << track_namespace.ToString()
                    << " accepted\n";
          return;
        };
    FullTrackName my_track_namespace = my_track_name;
    my_track_namespace.NameToNamespace();
    std::cout << "Announcing " << my_track_namespace.ToString() << "\n";
    session_->Announce(my_track_namespace, std::move(announce_callback));
  }
  remote_track_visitor_ = std::make_unique<RemoteTrackVisitor>(this);
  FullTrackName catalog_name = chat_strings_->GetCatalogName();
  if (!session_->SubscribeCurrentGroup(
          catalog_name, remote_track_visitor_.get(),
          MoqtSubscribeParameters{username_, std::nullopt, std::nullopt,
                                  std::nullopt})) {
    std::cout << "Failed to get catalog\n";
    return false;
  }
  while (session_is_open_ && is_syncing()) {
    RunEventLoop();
  }
  return session_is_open_;
}

void ChatClient::ProcessCatalog(absl::string_view object,
                                RemoteTrack::Visitor* visitor,
                                uint64_t group_sequence,
                                uint64_t object_sequence) {
  std::string message(object);
  std::istringstream f(message);
  // std::string line;
  bool got_version = true;
  if (object_sequence == 0) {
    std::cout << "Received new Catalog. Users:\n";
    got_version = false;
  }
  std::vector<absl::string_view> lines =
      absl::StrSplit(object, '\n', absl::SkipEmpty());
  for (absl::string_view line : lines) {
    if (!got_version) {
      if (line != "version=1") {
        session_->Error(MoqtError::kProtocolViolation,
                        "Catalog does not begin with version");
        return;
      }
      got_version = true;
      continue;
    }
    std::string user;
    bool add = true;
    if (object_sequence > 0) {
      switch (line[0]) {
        case '-':
          add = false;
          break;
        case '+':
          break;
        default:
          std::cerr << "Catalog update with neither + nor -\n";
          return;
      }
      user = line.substr(1, line.size() - 1);
    } else {
      user = line;
    }
    if (username_ == user) {
      std::cout << user << "\n";
      continue;
    }
    if (!add) {
      // TODO: Unsubscribe from the user that's leaving
      std::cout << user << "left the chat\n";
      other_users_.erase(user);
      continue;
    }
    if (object_sequence == 0) {
      std::cout << user << "\n";
    } else {
      std::cout << user << " joined the chat\n";
    }
    auto it = other_users_.find(user);
    if (it == other_users_.end()) {
      FullTrackName to_subscribe =
          chat_strings_->GetFullTrackNameFromUsername(user);
      auto new_user = other_users_.emplace(
          std::make_pair(user, ChatUser(to_subscribe, group_sequence)));
      ChatUser& user_record = new_user.first->second;
      session_->SubscribeCurrentGroup(user_record.full_track_name, visitor);
      subscribes_to_make_++;
    } else {
      if (it->second.from_group == group_sequence) {
        session_->Error(MoqtError::kProtocolViolation,
                        "User listed twice in Catalog");
        return;
      }
      it->second.from_group = group_sequence;
    }
  }
  if (object_sequence == 0) {  // Eliminate users that are no longer present
    absl::erase_if(other_users_, [&](const auto& kv) {
      return kv.second.from_group != group_sequence;
    });
  }
  catalog_group_ = group_sequence;
}

}  // namespace moqt
