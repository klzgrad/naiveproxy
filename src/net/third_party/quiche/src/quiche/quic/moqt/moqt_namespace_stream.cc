// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_namespace_stream.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/stream_helpers.h"

namespace moqt {

MoqtSubscribeNamespaceRequestStream::~MoqtSubscribeNamespaceRequestStream() {
  NamespaceTask* task = task_.GetIfAvailable();
  if (task != nullptr) {
    task->DeclareEof();
  }
  Detach();
}
absl::Status MoqtSubscribeNamespaceRequestStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "namespace subscriber");
}

void MoqtSubscribeNamespaceRequestStream::OnStreamBound() {
  // TODO(martinduke): Set the priority for this stream.
}

absl::Status MoqtSubscribeNamespaceRequestStream::OnControlMessage(
    const MoqtRequestOk& message) {
  if (message.request_id == request_id_) {
    // Response to the initial SUBSCRIBE_NAMESPACE.
    if (response_callback_ == nullptr) {
      return absl::InvalidArgumentError("Two responses");
    }
    std::move(response_callback_)(message.parameters);
    response_callback_ = nullptr;
    return absl::OkStatus();
  }
  NamespaceTask* task = task_.GetIfAvailable();
  if (task == nullptr) {
    // The application has already unsubscribed, and the stream has been reset.
    // This is irrelevant.
    return absl::OkStatus();
  }
  MoqtResponseCallback callback = task->GetResponseCallback(message.request_id);
  if (callback == nullptr) {
    return absl::InvalidArgumentError("Unexpected request ID in response");
  }
  std::move(callback)(message.parameters);
  return absl::OkStatus();
}

absl::Status MoqtSubscribeNamespaceRequestStream::OnControlMessage(
    const MoqtRequestError& message) {
  if (message.request_id == request_id_) {
    if (response_callback_ == nullptr) {
      return absl::InvalidArgumentError("Two responses");
    }
    std::move(response_callback_)(MoqtRequestErrorInfo{
        message.error_code, message.retry_interval, message.reason_phrase});
    response_callback_ = nullptr;
    return absl::OkStatus();
  }
  NamespaceTask* task = task_.GetIfAvailable();
  if (task == nullptr) {
    // The application has already unsubscribed, and the stream has been reset.
    // This is irrelevant.
    return absl::OkStatus();
  }
  MoqtResponseCallback callback = task->GetResponseCallback(message.request_id);
  if (callback == nullptr) {
    return absl::InvalidArgumentError("Unexpected request ID in response");
  }
  std::move(callback)(MoqtRequestErrorInfo{
      message.error_code, message.retry_interval, message.reason_phrase});
  return absl::OkStatus();
}

absl::Status MoqtSubscribeNamespaceRequestStream::OnControlMessage(
    const MoqtNamespace& message) {
  if (response_callback_ != nullptr) {
    return absl::InvalidArgumentError(
        "First message must be REQUEST_OK or REQUEST_ERROR");
  }
  NamespaceTask* task = task_.GetIfAvailable();
  if (task == nullptr) {
    // The application has already unsubscribed, and the stream has been reset.
    // This is irrelevant.
    return absl::OkStatus();
  }
  if (task->prefix().number_of_elements() +
          message.track_namespace_suffix.number_of_elements() >
      kMaxNamespaceElements) {
    return absl::InvalidArgumentError("Too many namespace elements");
  }
  if (task->prefix().total_length() +
          message.track_namespace_suffix.total_length() >
      kMaxFullTrackNameSize) {
    return absl::InvalidArgumentError("Namespace too large");
  }
  auto [it, inserted] =
      published_suffixes_.insert(message.track_namespace_suffix);
  if (!inserted) {
    return absl::InvalidArgumentError(
        "Two NAMESPACE messages for the same track namespace");
  }
  QUIC_DLOG(INFO) << "Received NAMESPACE message for "
                  << message.track_namespace_suffix;
  task->AddPendingSuffix(message.track_namespace_suffix, TransactionType::kAdd);
  return absl::OkStatus();
}

absl::Status MoqtSubscribeNamespaceRequestStream::OnControlMessage(
    const MoqtNamespaceDone& message) {
  if (response_callback_ != nullptr) {
    return absl::InvalidArgumentError(
        "First message must be REQUEST_OK or REQUEST_ERROR");
  }
  NamespaceTask* task = task_.GetIfAvailable();
  if (task == nullptr) {
    return absl::OkStatus();
  }
  if (published_suffixes_.erase(message.track_namespace_suffix) == 0) {
    return absl::InvalidArgumentError(
        "NAMESPACE_DONE with no active namespace");
  }
  QUIC_DLOG(INFO) << "Received NAMESPACE_DONE message for "
                  << message.track_namespace_suffix;
  task->AddPendingSuffix(message.track_namespace_suffix,
                         TransactionType::kDelete);
  return absl::OkStatus();
}

std::unique_ptr<MoqtNamespaceTask>
MoqtSubscribeNamespaceRequestStream::CreateTask(const TrackNamespace& prefix) {
  auto task = std::make_unique<NamespaceTask>(this, prefix);
  QUICHE_DCHECK(task != nullptr);
  task_ = task->GetWeakPtr();
  QUICHE_DCHECK(task_.IsValid());
  return std::move(task);
}

MoqtSubscribeNamespaceRequestStream::NamespaceTask::~NamespaceTask() {
  if (state_ != nullptr) {
    state_->Reset(kResetCodeCancelled);
  }
}

void MoqtSubscribeNamespaceRequestStream::NamespaceTask::
    SetObjectsAvailableCallback(ObjectsAvailableCallback
                                absl_nullable callback) {
  callback_ = std::move(callback);
  if (!pending_suffixes_.empty() && callback_ != nullptr) {
    callback_();
  }
}

void MoqtSubscribeNamespaceRequestStream::NamespaceTask::Update(
    const MessageParameters& parameters,
    MoqtResponseCallback response_callback) {
  if (state_ == nullptr) {
    std::move(response_callback)(
        MoqtRequestErrorInfo{RequestErrorCode::kInternalError, std::nullopt,
                             "Stream has been reset"});
    return;
  }
  MoqtRequestUpdate message{next_request_id_, state_->request_id_, parameters};
  pending_updates_[message.request_id] = std::move(response_callback);
  state_->SendOrBufferMessageOrFatal(
      state_->framer()->SerializeRequestUpdate(message));
  next_request_id_ += 2;
}

GetNextResult MoqtSubscribeNamespaceRequestStream::NamespaceTask::GetNextSuffix(
    TrackNamespace& suffix, TransactionType& type) {
  if (pending_suffixes_.empty()) {
    if (error_.has_value()) {
      return kError;
    }
    if (eof_) {
      return kEof;
    }
    return kPending;
  }
  suffix = pending_suffixes_.front().suffix;
  type = pending_suffixes_.front().type;
  pending_suffixes_.pop_front();
  return kSuccess;
}

void MoqtSubscribeNamespaceRequestStream::NamespaceTask::AddPendingSuffix(
    TrackNamespace suffix, TransactionType type) {
  if (pending_suffixes_.size() == kMaxPendingSuffixes) {
    error_ = kResetCodeTooFarBehind;
    if (state_ != nullptr) {
      state_->Reset(kResetCodeTooFarBehind);
    }
    return;
  }
  pending_suffixes_.push_back(PendingSuffix{std::move(suffix), type});
  if (callback_ != nullptr) {
    callback_();
  }
}

void MoqtSubscribeNamespaceRequestStream::NamespaceTask::DeclareEof() {
  if (eof_) {
    return;
  }
  eof_ = true;
  state_ = nullptr;
  if (callback_ != nullptr) {
    callback_();
  }
}

MoqtResponseCallback
MoqtSubscribeNamespaceRequestStream::NamespaceTask::GetResponseCallback(
    uint64_t request_id) {
  auto it = pending_updates_.find(request_id);
  if (it == pending_updates_.end()) {
    return nullptr;
  }
  MoqtResponseCallback callback = std::move(it->second);
  pending_updates_.erase(it);
  return callback;
}

MoqtSubscribeNamespaceResponseStream::MoqtSubscribeNamespaceResponseStream(
    MoqtFramer* framer, const MoqtControlMessageParser& message_parser,
    AddPrefixCallback add_callback, RemovePrefixCallback remove_callback,
    SessionErrorCallback session_error_callback,
    MoqtIncomingSubscribeNamespaceCallback& application)
    // No stream_deleted_callback because there's no state yet.
    : MoqtBidiStreamBase(framer, message_parser,
                         std::move(session_error_callback)),
      add_callback_(std::move(add_callback)),
      remove_callback_(std::move(remove_callback)),
      application_(application) {}

absl::Status MoqtSubscribeNamespaceResponseStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "namespace publisher");
}

absl::Status MoqtSubscribeNamespaceResponseStream::OnControlMessage(
    const MoqtSubscribeNamespace& message) {
  request_id_ = message.request_id;
  if (add_callback_ == nullptr) {
    return absl::InvalidArgumentError("Two SUBSCRIBE_NAMESPACE on one stream");
  }
  if (!std::move(add_callback_)(message.track_namespace_prefix)) {
    add_callback_ = nullptr;
    return SendRequestError(request_id_, RequestErrorCode::kPrefixOverlap,
                            std::nullopt, "", /*fin=*/true);
  }
  add_callback_ = nullptr;
  QUICHE_DCHECK(task_ == nullptr);
  task_ = application_(message.track_namespace_prefix, message.parameters,
                       ResponseCallback(request_id_));
  if (task_ != nullptr) {
    task_->SetObjectsAvailableCallback([this]() { ProcessNamespaces(); });
  }
  return absl::OkStatus();
}

absl::Status MoqtSubscribeNamespaceResponseStream::OnControlMessage(
    const MoqtRequestUpdate& message) {
  if (task_ == nullptr) {
    // This stream is dying.
    return absl::OkStatus();
  }
  task_->Update(message.parameters, ResponseCallback(message.request_id));
  return absl::OkStatus();
}

void MoqtSubscribeNamespaceResponseStream::Detach() {
  if (remove_callback_ != nullptr) {
    std::move(remove_callback_)(prefix_);
    remove_callback_ = nullptr;
  }
}

void MoqtSubscribeNamespaceResponseStream::ProcessNamespaces() {
  if (task_ == nullptr) {
    return;
  }
  TrackNamespace suffix;
  TransactionType type;
  while (!QueueIsFull()) {
    GetNextResult result = task_->GetNextSuffix(suffix, type);
    switch (result) {
      case kPending:
        return;
      case kEof:
        if (absl::Status status = webtransport::SendFinOnStream(*stream());
            !status.ok()) {
          OnFatalError(status);
        };
        return;
      case kError:
        Reset(kResetCodeCancelled);
        return;
      case kSuccess: {
        absl::Status write_status;
        switch (type) {
          case TransactionType::kAdd: {
            auto [it, inserted] = published_suffixes_.insert(suffix);
            if (!inserted) {
              // This should never happen. Do not send something that would
              // cause a protocol violation.
              return;
            }
            write_status = SendOrBufferMessage(
                framer()->SerializeNamespace(MoqtNamespace{suffix}));
            break;
          }
          case TransactionType::kDelete: {
            if (published_suffixes_.erase(suffix) == 0) {
              // This should never happen. Do not send something that would
              // cause a protocol violation.
              return;
            }
            write_status = SendOrBufferMessage(
                framer()->SerializeNamespaceDone(MoqtNamespaceDone{suffix}));
            break;
          }
        }
        if (!write_status.ok()) {
          if (absl::IsResourceExhausted(write_status)) {
            // The peer is not reading data fast enough, and the sender has
            // reached its buffer limit; reset the stream.
            Reset(kResetCodeTooFarBehind);
            return;
          }
          // All other write errors are fatal.
          OnFatalError(write_status);
          return;
        }
        break;
      }
    }
  }
}

MoqtResponseCallback MoqtSubscribeNamespaceResponseStream::ResponseCallback(
    uint64_t request_id) {
  return [this, request_id](
             std::variant<MessageParameters, MoqtRequestErrorInfo> response) {
    std::visit(absl::Overload{
                   [this, request_id](const MessageParameters& parameters) {
                     // In draft-18, there are no useful parameters in
                     // SUBSCRIBE_NAMESPACE_OK, but Issue #1639 would change
                     // that.
                     CheckStatus(SendRequestOk(request_id, parameters));
                   },
                   [this, request_id](const MoqtRequestErrorInfo& error_info) {
                     CheckStatus(SendRequestError(request_id, error_info,
                                                  /*fin=*/true));
                   }},
               response);
  };
}

}  // namespace moqt
