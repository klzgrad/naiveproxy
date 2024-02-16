// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/platform/api/quic_thread.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_circular_deque.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

class ChatClient {
 public:
  ChatClient(quic::QuicServerId& server_id, std::string path,
             std::string username, std::string chat_id)
      : chat_id_(chat_id),
        username_(username),
        my_track_name_(UsernameToTrackName(username)),
        catalog_name_("moq-chat/" + chat_id, "/catalog") {
    quic::QuicDefaultClock* clock = quic::QuicDefaultClock::Get();
    std::cout << "Connecting to host " << server_id.host() << " port "
              << server_id.port() << " path " << path << "\n";
    event_loop_ = quic::GetDefaultEventLoop()->Create(clock);
    quic::QuicSocketAddress peer_address =
        quic::tools::LookupAddress(AF_UNSPEC, server_id);
    std::unique_ptr<quic::ProofVerifier> verifier;
    const bool ignore_certificate = quiche::GetQuicheCommandLineFlag(
        FLAGS_disable_certificate_verification);
    if (ignore_certificate) {
      verifier = std::make_unique<quic::FakeProofVerifier>();
    } else {
      verifier = quic::CreateDefaultProofVerifier(server_id.host());
    }
    client_ = std::make_unique<moqt::MoqtClient>(
        peer_address, server_id, std::move(verifier), event_loop_.get());
    session_callbacks_.session_established_callback = [this]() {
      std::cout << "Session established\n";
      session_is_open_ = true;
    };
    session_callbacks_.session_terminated_callback =
        [this](absl::string_view error_message) {
          std::cerr << "Closed session, reason = " << error_message << "\n";
          session_is_open_ = false;
        };
    session_callbacks_.session_deleted_callback = [this]() {
      session_ = nullptr;
    };
    client_->Connect(path, std::move(session_callbacks_));
  }

  bool session_is_open() const { return session_is_open_; }
  bool is_syncing() const {
    return catalog_group_.has_value() || subscribes_to_make_ > 0 ||
           !session_->HasSubscribers(my_track_name_);
  }

  void RunEventLoop() {
    event_loop_->RunEventLoopOnce(quic::QuicTime::Delta::FromSeconds(5));
  }

  class QUICHE_EXPORT RemoteTrackVisitor : public moqt::RemoteTrack::Visitor {
   public:
    RemoteTrackVisitor(ChatClient* client) : client_(client) {}

    void OnReply(const moqt::FullTrackName& full_track_name,
                 std::optional<absl::string_view> reason_phrase) override {
      client_->subscribes_to_make_--;
      if (full_track_name == client_->catalog_name_) {
        std::cout << "Subscription to catalog ";
      } else {
        std::cout << "Subscription to user " << full_track_name.track_namespace
                  << " ";
      }
      if (reason_phrase.has_value()) {
        std::cout << "REJECTED, reason = " << *reason_phrase << "\n";
      } else {
        std::cout << "ACCEPTED\n";
      }
    }

    void OnObjectFragment(const moqt::FullTrackName& full_track_name,
                          uint32_t /*stream_id*/, uint64_t group_sequence,
                          uint64_t object_sequence,
                          uint64_t /*object_send_order*/,
                          absl::string_view object,
                          bool end_of_message) override {
      if (!end_of_message) {
        std::cerr << "Error: received partial message despite requesting "
                     "buffering\n";
      }
      if (full_track_name == client_->catalog_name_) {
        if (group_sequence < client_->catalog_group_) {
          std::cout << "Ignoring old catalog";
          return;
        }
        client_->ProcessCatalog(object, this, group_sequence, object_sequence);
        return;
      }
      // TODO: Message is from another chat participant
    }

   private:
    ChatClient* client_;
  };

  // returns false on error
  bool AnnounceAndSubscribe() {
    session_ = client_->session();
    if (session_ == nullptr) {
      std::cout << "Failed to connect.\n";
      return false;
    }
    // By not sending a visitor, the application will not fulfill subscriptions
    // to previous objects.
    session_->AddLocalTrack(my_track_name_, nullptr);
    moqt::MoqtAnnounceCallback announce_callback =
        [&](absl::string_view track_namespace,
            std::optional<absl::string_view> message) {
          if (message.has_value()) {
            std::cout << "ANNOUNCE rejected, " << *message << "\n";
            session_->Error("Local ANNOUNCE rejected");
            return;
          }
          std::cout << "ANNOUNCE for " << track_namespace << " accepted\n";
          return;
        };
    std::cout << "Announcing " << my_track_name_.track_namespace << "\n";
    session_->Announce(my_track_name_.track_namespace,
                       std::move(announce_callback));
    remote_track_visitor_ = std::make_unique<RemoteTrackVisitor>(this);
    if (!session_->SubscribeCurrentGroup(
            catalog_name_.track_namespace, catalog_name_.track_name,
            remote_track_visitor_.get(), username_)) {
      std::cout << "Failed to get catalog for " << chat_id_ << "\n";
      return false;
    }
    return true;
  }

  class InputHandler : quic::QuicThread {
   public:
    explicit InputHandler(ChatClient* client)
        : quic::QuicThread("InputThread"), client_(client) {}

    void Run() final {
      while (client_->session_is_open_) {
        std::string message_to_send;
        std::cin >> message_to_send;  // Waiting to start input
        client_->entering_data_ = true;
        std::cout << "> ";
        std::cin >> message_to_send;
        client_->entering_data_ = false;
        while (!client_->incoming_messages_.empty()) {
          std::cout << client_->incoming_messages_.front() << "\n";
          client_->incoming_messages_.pop_front();
        }
        if (message_to_send.empty()) {
          continue;
        }
        if (message_to_send == ":exit") {
          std::cout << "Exiting the app.\n";
          // TODO: Close the session.
          client_->session_is_open_ = false;
          break;
        }
        // TODO: Send the message
        std::cout << client_->username_ << ": " << message_to_send << "\n";
      }
    }

   private:
    ChatClient* client_;
  };

 private:
  moqt::FullTrackName UsernameToTrackName(absl::string_view username) {
    return moqt::FullTrackName(
        absl::StrCat("moq-chat/", chat_id_, "/participant/", username), "");
  }

  // Objects from the same catalog group arrive on the same stream, and in
  // object sequence order.
  void ProcessCatalog(absl::string_view object,
                      moqt::RemoteTrack::Visitor* visitor,
                      uint64_t group_sequence, uint64_t object_sequence) {
    std::string message(object);
    std::istringstream f(message);
    std::string line;
    bool got_version = true;
    if (object_sequence == 0) {
      std::cout << "Received new Catalog. Users:\n";
      got_version = false;
    }
    while (std::getline(f, line)) {
      if (!got_version) {
        // Chat server currently does not send version
        if (line != "version=1") {
          session_->Error("Catalog does not begin with version");
          return;
        }
        got_version = true;
        continue;
      }
      if (line.empty()) {
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
        std::cout << user << "joined the chat\n";
      }
      auto it = other_users_.find(user);
      if (it == other_users_.end()) {
        moqt::FullTrackName to_subscribe = UsernameToTrackName(user);
        auto new_user = other_users_.emplace(
            std::make_pair(user, ChatUser(to_subscribe, group_sequence)));
        ChatUser& user_record = new_user.first->second;
        session_->SubscribeRelative(user_record.full_track_name.track_namespace,
                                    user_record.full_track_name.track_name, 0,
                                    0, visitor);
        subscribes_to_make_++;
      } else {
        if (it->second.from_group == group_sequence) {
          session_->Error("User listed twice in Catalog");
          return;
        }
        it->second.from_group = group_sequence;
      }
    }
    if (object_sequence == 0) {  // Eliminate users that are no longer present
      for (const auto& it : other_users_) {
        if (it.second.from_group != group_sequence) {
          other_users_.erase(it.first);
        }
      }
    }
    catalog_group_ = group_sequence;
  }

  struct ChatUser {
    moqt::FullTrackName full_track_name;
    uint64_t from_group;
    ChatUser(moqt::FullTrackName& ftn, uint64_t group)
        : full_track_name(ftn), from_group(group) {}
  };

  // Basic session information
  std::string chat_id_;
  std::string username_;
  moqt::FullTrackName my_track_name_;

  // General state variables
  std::unique_ptr<quic::QuicEventLoop> event_loop_;
  bool session_is_open_ = false;
  moqt::MoqtSession* session_ = nullptr;
  std::unique_ptr<moqt::MoqtClient> client_;
  moqt::MoqtSessionCallbacks session_callbacks_;

  // Related to syncing.
  std::optional<uint64_t> catalog_group_;
  moqt::FullTrackName catalog_name_;
  absl::flat_hash_map<std::string, ChatUser> other_users_;
  int subscribes_to_make_ = 1;

  // Related to subscriptions/announces
  // TODO: One for each subscribe
  std::unique_ptr<RemoteTrackVisitor> remote_track_visitor_;

  // Handling incoming and outgoing messages
  quiche::QuicheCircularDeque<std::string> incoming_messages_;
  bool entering_data_ = false;
};

// A client for MoQT over chat, used for interop testing. See
// https://afrind.github.io/draft-frindell-moq-chat/draft-frindell-moq-chat.html
int main(int argc, char* argv[]) {
  const char* usage = "Usage: chat_client [options] <url> <username> <chat-id>";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (args.size() != 3) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  quic::QuicUrl url(args[0], "https");
  quic::QuicServerId server_id(url.host(), url.port());
  std::string path = url.PathParamsQuery();
  std::string username = args[1];
  std::string chat_id = args[2];
  ChatClient client(server_id, path, username, chat_id);

  while (!client.session_is_open()) {
    client.RunEventLoop();
  }

  if (!client.AnnounceAndSubscribe()) {
    return 1;
  }
  while (client.is_syncing()) {
    client.RunEventLoop();
  }
  if (client.session_is_open()) {
    std::cout << "Fully connected. Press ENTER to begin input of message, "
              << "ENTER when done.\n";
  }
  ChatClient::InputHandler input_thread(&client);
  while (client.session_is_open()) {
    client.RunEventLoop();
  }
  return 0;
}
