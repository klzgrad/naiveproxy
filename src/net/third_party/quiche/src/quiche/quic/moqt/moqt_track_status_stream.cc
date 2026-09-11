// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_track_status_stream.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

MoqtTrackStatusRequestStream::MoqtTrackStatusRequestStream(
    MoqtFramer* absl_nonnull framer,
    const MoqtControlMessageParser& message_parser, uint64_t request_id,
    const FullTrackName& full_track_name, const MessageParameters& parameters,
    SessionErrorCallback session_error_callback,
    MoqtResponseCallback response_callback)
    : MoqtBidiStreamBase(framer, message_parser,
                         std::move(session_error_callback)),
      request_id_(request_id),
      full_track_name_(full_track_name),
      parameters_(parameters),
      response_callback_(std::move(response_callback)) {}

void MoqtTrackStatusRequestStream::OnStreamBound() {
  stream_parser()->set_allow_fin(true);
  MoqtTrackStatus message;
  message.request_id = request_id_;
  message.full_track_name = full_track_name_;
  message.parameters = parameters_;
  SendOrBufferMessageOrFatal(framer()->SerializeTrackStatus(message));
}

absl::Status MoqtTrackStatusRequestStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "track status request");
}

absl::Status MoqtTrackStatusRequestStream::OnControlMessage(
    const MoqtRequestOk& message) {
  if (response_callback_ == nullptr) {
    return absl::InvalidArgumentError("Duplicate REQUEST_OK");
  }
  MoqtResponseCallback callback = std::move(response_callback_);
  response_callback_ = nullptr;
  Fin();
  // `message.request_id` is ignored, since request IDs in REQUEST_OK are
  // deprecated and not present in draft-18.
  std::move(callback)(message.parameters);
  return absl::OkStatus();
}

absl::Status MoqtTrackStatusRequestStream::OnControlMessage(
    const MoqtRequestError& message) {
  if (response_callback_ == nullptr) {
    return absl::InvalidArgumentError("Duplicate REQUEST_ERROR");
  }
  MoqtResponseCallback callback = std::move(response_callback_);
  response_callback_ = nullptr;
  Fin();
  // `message.request_id` is ignored, since request IDs in REQUEST_ERROR are
  // deprecated and not present in draft-18.
  std::move(callback)(MoqtRequestErrorInfo{
      message.error_code, message.retry_interval, message.reason_phrase});
  return absl::OkStatus();
}

void MoqtTrackStatusRequestStream::Detach() {
  if (response_callback_ != nullptr) {
    MoqtResponseCallback callback = std::move(response_callback_);
    response_callback_ = nullptr;
    std::move(callback)(MoqtRequestErrorInfo{RequestErrorCode::kInternalError,
                                             std::nullopt, "Stream closed"});
  }
}

MoqtTrackStatusResponseStream::MoqtTrackStatusResponseStream(
    MoqtFramer* absl_nonnull framer,
    const MoqtControlMessageParser& message_parser,
    SessionErrorCallback session_error_callback,
    quiche::QuicheWeakPtr<SessionToPublisherInterface> session)
    : MoqtBidiStreamBase(framer, message_parser,
                         std::move(session_error_callback)),
      session_(session) {}

absl::Status MoqtTrackStatusResponseStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "track status response");
}

absl::Status MoqtTrackStatusResponseStream::OnControlMessage(
    const MoqtTrackStatus& message) {
  if (request_id_.has_value()) {
    return absl::InvalidArgumentError("Duplicate TRACK_STATUS received");
  }
  request_id_ = message.request_id;
  if (session() == nullptr) {
    return absl::InternalError("Session unavailable");
  }
  publisher_ = session()->GetTrackPublisher(message.full_track_name);
  if (publisher_ == nullptr) {
    return SendRequestError(message.request_id, RequestErrorCode::kDoesNotExist,
                            std::nullopt, "Track does not exist",
                            /*fin=*/true);
  }
  // If the upstream subscription is already established, the code below will
  // invoke `OnSubscribeAccepted` immediately.
  publisher_->AddObjectListener(this);
  return absl::OkStatus();
}

void MoqtTrackStatusResponseStream::OnSubscribeAccepted() {
  if (publisher_ == nullptr || !request_id_.has_value()) {
    QUICHE_NOTREACHED();
    return;
  }
  MessageParameters parameters;
  parameters.expires = publisher_->expiration();
  parameters.largest_object = publisher_->largest_location();
  // Since `fin` is true, this will also reset `publisher_`.
  CheckStatus(SendRequestOk(*request_id_, parameters, /*fin=*/true));
}

void MoqtTrackStatusResponseStream::OnSubscribeRejected(
    MoqtRequestErrorInfo info) {
  if (!request_id_.has_value()) {
    QUICHE_NOTREACHED();
    return;
  }
  // Since `fin` is true, this will also reset `publisher_` if present.
  CheckStatus(SendRequestError(*request_id_, info.error_code,
                               info.retry_interval, info.reason_phrase,
                               /*fin=*/true));
}

void MoqtTrackStatusResponseStream::OnTrackPublisherGone() {
  publisher_ = nullptr;
  OnSubscribeRejected(MoqtRequestErrorInfo(RequestErrorCode::kDoesNotExist,
                                           std::nullopt,
                                           "Track publisher destroyed"));
}

void MoqtTrackStatusResponseStream::Detach() {
  if (publisher_ != nullptr) {
    publisher_->RemoveObjectListener(this);
    publisher_ = nullptr;
  }
}

}  // namespace moqt
