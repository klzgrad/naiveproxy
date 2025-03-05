// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_CHAT_CLIENT_H
#define QUICHE_QUIC_MOQT_TOOLS_CHAT_CLIENT_H

#include <memory>
#include <optional>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt::moq_chat {

constexpr quic::QuicTime::Delta kChatEventLoopDuration =
    quic::QuicTime::Delta::FromMilliseconds(500);

// Chat clients accept a ChatUserInterface that implements how user input is
// captured, and peer messages are displayed.
class ChatUserInterface {
 public:
  virtual ~ChatUserInterface() = default;

  // ChatUserInterface cannot be used until initialized. This is separate from
  // the constructor, because the constructor might create the event loop.
  // |callback| is what ChatUserInterface will call when there is user input.
  // |event_loop| is the event loop that the ChatUserInterface should use.
  virtual void Initialize(
      quiche::MultiUseCallback<void(absl::string_view)> callback,
      quic::QuicEventLoop* event_loop) = 0;
  // Write a peer message to the user output.
  virtual void WriteToOutput(absl::string_view user,
                             absl::string_view message) = 0;
  // Run the event loop for a short interval and exit.
  virtual void IoLoop() = 0;
};

class ChatClient {
 public:
  // If |event_loop| is nullptr, a new one will be created. If multiple
  // endpoints are running on the same thread, as in tests, they should share
  // an event loop.
  ChatClient(const quic::QuicServerId& server_id, bool ignore_certificate,
             std::unique_ptr<ChatUserInterface> interface,
             absl::string_view chat_id, absl::string_view username,
             absl::string_view localhost,
             quic::QuicEventLoop* event_loop = nullptr);
  ~ChatClient() {
    if (session_ != nullptr) {
      session_->Close();
      session_ = nullptr;
    }
  }

  // Establish the MoQT session. Returns false if it fails.
  bool Connect(absl::string_view path);

  void OnTerminalLineInput(absl::string_view input_message);

  // Run the event loop until an input or output event is ready, or the
  // session closes.
  void IoLoop() {
    while (session_is_open_) {
      interface_->IoLoop();
    }
  }

  void WriteToOutput(absl::string_view user, absl::string_view message) {
    if (interface_ != nullptr) {
      interface_->WriteToOutput(user, message);
    }
  }

  quic::QuicEventLoop* event_loop() { return event_loop_; }

  class QUICHE_EXPORT RemoteTrackVisitor
      : public moqt::SubscribeRemoteTrack::Visitor {
   public:
    RemoteTrackVisitor(ChatClient* client) : client_(client) {}

    void OnReply(const moqt::FullTrackName& full_track_name,
                 std::optional<FullSequence> largest_id,
                 std::optional<absl::string_view> reason_phrase) override;

    void OnCanAckObjects(MoqtObjectAckFunction) override {}

    void OnObjectFragment(const moqt::FullTrackName& full_track_name,
                          FullSequence sequence,
                          moqt::MoqtPriority publisher_priority,
                          moqt::MoqtObjectStatus status,
                          absl::string_view object,
                          bool end_of_message) override;

   private:
    ChatClient* client_;
  };

  // Returns false on error.
  bool AnnounceAndSubscribeAnnounces();

  bool session_is_open() const { return session_is_open_; }

  // Returns true if the client is still doing initial sync: retrieving the
  // catalog, subscribing to all the users in it, and waiting for the server
  // to subscribe to the local track.
  bool is_syncing() const {
    return subscribes_to_make_ > 0 ||
           (queue_ == nullptr || !queue_->HasSubscribers());
  }

 private:
  void RunEventLoop() { event_loop_->RunEventLoopOnce(kChatEventLoopDuration); }
  // Callback for incoming announces.
  std::optional<MoqtAnnounceErrorReason> OnIncomingAnnounce(
      const moqt::FullTrackName& track_namespace, AnnounceEvent announce_type);

  // Basic session information
  FullTrackName my_track_name_;

  // General state variables
  // The event loop to use for this client.
  quic::QuicEventLoop* event_loop_;
  // If the client created its own event loop, it will own it.
  std::unique_ptr<quic::QuicEventLoop> local_event_loop_;
  bool connect_failed_ = false;
  bool session_is_open_ = false;
  moqt::MoqtSession* session_ = nullptr;
  moqt::MoqtKnownTrackPublisher publisher_;
  std::unique_ptr<moqt::MoqtClient> client_;
  moqt::MoqtSessionCallbacks session_callbacks_;

  // Related to syncing.
  absl::flat_hash_set<FullTrackName> other_users_;
  int subscribes_to_make_ = 0;

  // Related to subscriptions/announces
  // TODO: One for each subscribe
  RemoteTrackVisitor remote_track_visitor_;

  // Handling outgoing messages
  std::shared_ptr<moqt::MoqtOutgoingQueue> queue_;

  // User interface for input and output.
  std::unique_ptr<ChatUserInterface> interface_;
};

}  // namespace moqt::moq_chat

#endif  // QUICHE_QUIC_MOQT_TOOLS_CHAT_CLIENT_H
