// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_publish_namespace_stream.h"

#include <optional>
#include <utility>
#include <variant>

#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"

namespace moqt {

void MoqtPublishNamespaceRequestStream::OnStreamBound() {
  // TODO(martinduke): Set the priority for this stream.
  SendOrBufferMessageOrFatal(
      framer()->SerializePublishNamespace(
          MoqtPublishNamespace{request_id_, prefix_, parameters_}),
      false);
  QUIC_DLOG(INFO) << "Sent PUBLISH_NAMESPACE message for " << prefix_;
}

absl::Status MoqtPublishNamespaceRequestStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "namespace publisher");
}

absl::Status MoqtPublishNamespaceRequestStream::OnControlMessage(
    const MoqtRequestOk& message) {
  if (response_callback_ != nullptr) {
    // Response to the initial PUBLISH_NAMESPACE.
    auto callback = std::move(response_callback_);
    response_callback_ = nullptr;
    std::move(callback)(message.parameters);
    return absl::OkStatus();
  }
  absl::StatusOr<MessageParameters> old_parameters =
      request_update_queue().NextParameters();
  if (!old_parameters.ok()) {
    return old_parameters.status();
  }
  parameters_.Update(*old_parameters);
  // Response to REQUEST_UPDATE.
  return request_update_queue().OnControlMessage(message);
}

absl::Status MoqtPublishNamespaceRequestStream::OnControlMessage(
    const MoqtRequestError& message) {
  if (response_callback_ != nullptr) {
    // Response to the initial PUBLISH_NAMESPACE.
    auto callback = std::move(response_callback_);
    response_callback_ = nullptr;
    Fin();
    std::move(callback)(MoqtRequestErrorInfo{
        message.error_code, message.retry_interval, message.reason_phrase});
    return absl::OkStatus();
  }
  // The REQUEST_ERROR is a response to the REQUEST_UPDATE message.
  absl::Status status = request_update_queue().OnControlMessage(message);
  if (status.ok()) {
    Fin();
  }
  return status;
}

void MoqtPublishNamespaceRequestStream::Detach() {
  if (remove_callback_ != nullptr) {
    RemovePublishNamespaceCallback callback = std::move(remove_callback_);
    remove_callback_ = nullptr;
    std::move(callback)(prefix_);
  }
}

absl::Status MoqtPublishNamespaceResponseStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "namespace publisher");
}

absl::Status MoqtPublishNamespaceResponseStream::OnControlMessage(
    const MoqtPublishNamespace& message) {
  if (add_callback_ == nullptr) {
    return absl::InvalidArgumentError("Two PUBLISH_NAMESPACE on one stream");
  }
  request_id_ = message.request_id;
  if (!std::move(add_callback_)(message.track_namespace, this)) {
    add_callback_ = nullptr;
    return SendRequestError(request_id_, RequestErrorCode::kInternalError,
                            std::nullopt, "", /*fin=*/true);
  }
  add_callback_ = nullptr;
  prefix_ = message.track_namespace;
  application_(
      *prefix_, &message.parameters,
      [weakptr = weak_ptr_factory_.Create(), id = request_id_](
          std::variant<MessageParameters, MoqtRequestErrorInfo> response) {
        MoqtPublishNamespaceResponseStream* stream = weakptr.GetIfAvailable();
        if (stream == nullptr) {
          return;
        }
        std::visit(
            absl::Overload{[&](const MessageParameters& parameters) {
                             stream->CheckStatus(
                                 stream->SendRequestOk(id, parameters));
                           },
                           [&](const MoqtRequestErrorInfo& error) {
                             stream->CheckStatus(stream->SendRequestError(
                                 id, error.error_code, error.retry_interval,
                                 error.reason_phrase));
                           }},
            response);
      });
  return absl::OkStatus();
}

absl::Status MoqtPublishNamespaceResponseStream::OnControlMessage(
    const MoqtRequestUpdate& message) {
  if (!prefix_.has_value()) {
    return absl::InvalidArgumentError(
        "REQUEST_UPDATE before PUBLISH_NAMESPACE on a PN stream");
  }
  application_(
      *prefix_, &message.parameters,
      [weakptr = weak_ptr_factory_.Create(), id = message.request_id](
          std::variant<MessageParameters, MoqtRequestErrorInfo> response) {
        MoqtPublishNamespaceResponseStream* stream = weakptr.GetIfAvailable();
        if (stream == nullptr) {
          return;
        }
        std::visit(
            absl::Overload{[&](const MessageParameters& parameters) {
                             stream->CheckStatus(
                                 stream->SendRequestOk(id, parameters));
                           },
                           [&](const MoqtRequestErrorInfo& error) {
                             stream->CheckStatus(stream->SendRequestError(
                                 id, error.error_code, error.retry_interval,
                                 error.reason_phrase));
                           }},
            response);
      });
  return absl::OkStatus();
}

void MoqtPublishNamespaceResponseStream::Detach() {
  if (!prefix_.has_value()) {
    return;
  }
  if (remove_callback_ != nullptr) {
    RemovePublishNamespaceCallback callback = std::move(remove_callback_);
    remove_callback_ = nullptr;
    std::move(callback)(*prefix_);
    application_(*prefix_, nullptr, nullptr);
  }
}

}  // namespace moqt
