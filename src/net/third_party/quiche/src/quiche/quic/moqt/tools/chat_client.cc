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
#include <variant>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
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

void ChatClient::OnIncomingPublishNamespace(
    const moqt::TrackNamespace& track_namespace,
    const std::optional<MessageParameters>& parameters,
    moqt::MoqtResponseCallback absl_nullable callback) {
  if (!session_is_open_) {
    return;
  }
  if (track_namespace == GetUserNamespace(my_track_name_)) {
    // Ignore PUBLISH_NAMESPACE for my own track.
    if (parameters.has_value() && callback != nullptr) {  // callback exists.
      std::move(callback)(std::nullopt);
    }
    return;
  }
  std::optional<FullTrackName> track_name = ConstructTrackNameFromNamespace(
      track_namespace, GetChatId(my_track_name_));
  if (!parameters.has_value()) {
    std::cout << "PUBLISH_NAMESPACE_DONE for " << track_namespace.ToString()
              << "\n";
    if (track_name.has_value()) {
      other_users_.erase(*track_name);
    }
    return;
  }
  std::cout << "PUBLISH_NAMESPACE for " << track_namespace.ToString() << "\n";
  if (!track_name.has_value()) {
    std::cout << "PUBLISH_NAMESPACE rejected, invalid namespace\n";
    if (callback != nullptr) {
      std::move(callback)(std::make_optional<MoqtRequestErrorInfo>(
          RequestErrorCode::kDoesNotExist, std::nullopt,
          "Not a subscribed namespace"));
    }
    return;
  }
  if (other_users_.contains(*track_name)) {
    std::cout << "Duplicate PUBLISH_NAMESPACE, send OK and ignore\n";
    if (callback != nullptr) {
      std::move(callback)(std::nullopt);
    }
    return;
  }
  if (GetUsername(my_track_name_) == GetUsername(*track_name)) {
    std::cout << "PUBLISH_NAMESPACE for a previous instance of my track, "
                 "do not subscribe\n";
    if (callback != nullptr) {
      std::move(callback)(std::nullopt);
    }
    return;
  }
  MessageParameters subscribe_parameters(MoqtFilterType::kLargestObject);
  subscribe_parameters.authorization_tokens.emplace_back(
      AuthTokenType::kOutOfBand, std::string(GetUsername(my_track_name_)));
  // session_ could be nullptr if we get unsolicited PUBLISH_NAMESPACE at the
  // start of the session.
  other_users_.emplace(*track_name);
  if (session_ != nullptr &&
      session_->Subscribe(*track_name, &remote_track_visitor_,
                          subscribe_parameters)) {
    ++subscribes_to_make_;
  }
  if (callback != nullptr) {
    std::move(callback)(std::nullopt);  // Send PUBLISH_NAMESPACE_OK.
  }
}

ChatClient::ChatClient(const quic::QuicServerId& server_id,
                       bool ignore_certificate,
                       std::unique_ptr<ChatUserInterface> interface,
                       absl::string_view chat_id, absl::string_view username,
                       absl::string_view localhost,
                       quic::QuicEventLoop* event_loop)
    : my_track_name_(ConstructTrackName(chat_id, username, localhost)),
      remote_track_visitor_(this),
      event_loop_(event_loop),
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
  session_callbacks_.incoming_publish_namespace_callback =
      absl::bind_front(&ChatClient::OnIncomingPublishNamespace, this);
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
    // Clean teardown of SUBSCRIBE_NAMESPACE, PUBLISH_NAMESPACE, SUBSCRIBE.
    namespace_task_.reset();
    // TODO(martinduke): Add a session API to send PUBLISH_DONE.
    session_->PublishNamespaceDone(GetUserNamespace(my_track_name_));
    for (const auto& track_name : other_users_) {
      session_->Unsubscribe(track_name);
    }
    session_->callbacks() = MoqtSessionCallbacks();
    other_users_.clear();
    session_->Close();
    session_is_open_ = false;
    return;
  }
  quiche::QuicheMemSlice message_slice(quiche::QuicheBuffer::Copy(
      quiche::SimpleBufferAllocator::Get(), input_message));
  queue_->AddObject(std::move(message_slice), /*key=*/true);
}

void ChatClient::RemoteTrackVisitor::OnReply(
    const FullTrackName& full_track_name,
    std::variant<SubscribeOkData, MoqtRequestErrorInfo> response) {
  auto it = client_->other_users_.find(full_track_name);
  if (it == client_->other_users_.end()) {
    std::cout << "Error: received reply for unknown user "
              << full_track_name.ToString() << "\n";
    return;
  }
  --client_->subscribes_to_make_;
  std::cout << "Subscription to user " << GetUsername(*it) << " ";
  if (std::holds_alternative<SubscribeOkData>(response)) {
    std::cout << "ACCEPTED\n";
  } else {
    auto request_error = std::get<MoqtRequestErrorInfo>(response);
    std::cout << "REJECTED, reason = "
              << std::get<MoqtRequestErrorInfo>(response).reason_phrase << "\n";
    client_->other_users_.erase(it);
  }
}

void ChatClient::RemoteTrackVisitor::OnObjectFragment(
    const FullTrackName& full_track_name, const PublishedObjectMetadata&,
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

bool ChatClient::PublishNamespaceAndSubscribeNamespace() {
  session_ = client_->session();
  if (session_ == nullptr) {
    std::cout << "Failed to connect.\n";
    return false;
  }
  // There might already be published namespaces that have populated
  // other_users_. Subscribe to their tracks now.
  for (const auto& track_name : other_users_) {
    MessageParameters subscribe_parameters(MoqtFilterType::kLargestObject);
    subscribe_parameters.authorization_tokens.emplace_back(
        AuthTokenType::kOutOfBand, std::string(GetUsername(my_track_name_)));
    if (session_->Subscribe(track_name, &remote_track_visitor_,
                            subscribe_parameters)) {
      ++subscribes_to_make_;
    }
  }
  // TODO: A server log might choose to not provide a username, thus getting all
  // the messages without adding itself to the catalog.
  queue_ = std::make_shared<MoqtOutgoingQueue>(my_track_name_);
  publisher_.Add(queue_);
  session_->set_publisher(&publisher_);
  MoqtResponseCallback publish_namespace_callback =
      [this](std::optional<MoqtRequestErrorInfo> reason) {
        if (reason.has_value()) {
          std::cout << "PUBLISH_NAMESPACE rejected, " << reason->reason_phrase
                    << "\n";
          session_->Error(MoqtError::kInternalError,
                          "Local PUBLISH_NAMESPACE rejected");
          return;
        }
        std::cout << "PUBLISH_NAMESPACE accepted\n";
        return;
      };
  std::cout << "Announcing " << GetUserNamespace(my_track_name_).ToString()
            << "\n";
  session_->PublishNamespace(
      GetUserNamespace(my_track_name_), MessageParameters(),
      [this](std::optional<MoqtRequestErrorInfo> reason) {
        if (reason.has_value()) {
          std::cout << "PUBLISH_NAMESPACE rejected, " << reason->reason_phrase
                    << "\n";
          session_->Error(MoqtError::kInternalError,
                          "Local PUBLISH_NAMESPACE rejected");
          return;
        }
        std::cout << "PUBLISH_NAMESPACE for "
                  << GetUserNamespace(my_track_name_).ToString()
                  << " accepted\n";
        return;
      },
      [](MoqtRequestErrorInfo) {});

  // Send SUBSCRIBE_NAMESPACE. Pop 3 levels of namespace to get to
  // {moq-chat, chat-id}
  bool subscribe_response_received = false;
  TrackNamespace prefix = GetChatNamespace(my_track_name_);
  MoqtResponseCallback response_callback =
      [&, this, prefix](std::optional<MoqtRequestErrorInfo> error) {
        subscribe_response_received = true;
        if (error.has_value()) {
          std::cout << "SUBSCRIBE_NAMESPACE rejected, " << error->reason_phrase
                    << "\n";
          session_->Error(MoqtError::kInternalError,
                          "Local SUBSCRIBE_NAMESPACE rejected");
          return;
        }
        std::cout << "SUBSCRIBE_NAMESPACE for " << prefix.ToString()
                  << " accepted\n";
        return;
      };
  MessageParameters parameters;
  parameters.authorization_tokens.emplace_back(
      AuthTokenType::kOutOfBand, std::string(GetUsername(my_track_name_)));
  namespace_task_ =
      session_->SubscribeNamespace(prefix, SubscribeNamespaceOption::kNamespace,
                                   parameters, std::move(response_callback));
  if (namespace_task_ != nullptr) {
    namespace_task_->SetObjectsAvailableCallback(
        [this]() {
          TrackNamespace suffix;
          TransactionType type;
          for (;;) {
            GetNextResult result = namespace_task_->GetNextSuffix(suffix, type);
            switch (result) {
              case GetNextResult::kPending:
                return;
              case GetNextResult::kEof:
                return;
              case GetNextResult::kError:
                std::cerr << "Error: received error from namespace task\n";
                return;
              case GetNextResult::kSuccess:
                absl::StatusOr<TrackNamespace> track_namespace =
                    namespace_task_->prefix().AddSuffix(suffix);
                if (!track_namespace.ok()) {
                  std::cerr
                      << "Error: received invalid suffix from namespace task\n";
                  return;
                }
                OnIncomingPublishNamespace(
                    *track_namespace,
                    (type == TransactionType::kAdd)
                        ? std::make_optional(MessageParameters())
                        : std::nullopt,
                    /*callback=*/nullptr);
                break;
            }
          }
        });
  }

  while (session_is_open_ && !subscribe_response_received) {
    RunEventLoop();
  }
  return session_is_open_;
}

}  // namespace moqt::moq_chat
