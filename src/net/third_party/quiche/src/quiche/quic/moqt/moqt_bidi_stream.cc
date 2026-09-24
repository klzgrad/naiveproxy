// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_bidi_stream.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt {

void MoqtBidiStreamBase::OnCanRead() {
  if (!stream_parser_.has_value()) {
    QUICHE_BUG(MoqtBidiStreamBase_OnCanRead_no_stream)
        << "OnCanRead() called when no stream is bound";
    return;
  }
  while (!stream_parser_->fin_read()) {
    absl::StatusOr<MoqtRawControlMessage> message =
        stream_parser_->ReadNextMessage();
    if (absl::IsUnavailable(message.status())) {
      return;
    }
    if (!message.ok()) {
      OnFatalError(message.status());
      return;
    }
    absl::Status status = OnRawControlMessage(*message);
    if (!status.ok()) {
      OnFatalError(status);
      return;
    }
  }
}

void MoqtBidiStreamBase::OnCanWrite() {
  if (!stream_parser_.has_value()) {
    QUICHE_BUG(MoqtBidiStreamBase_OnCanWrite_no_stream)
        << "OnCanWrite() called when no stream is bound";
    return;
  }
  absl::Status status = outgoing_message_queue_.OnCanWrite();
  if (!status.ok()) {
    OnFatalError(status);
  }
}

absl::Status MoqtBidiStreamBase::SendRequestOk(
    uint64_t request_id, const MessageParameters& parameters, bool fin) {
  return SendOrBufferMessage(
      framer_->SerializeRequestOk(MoqtRequestOk{request_id, parameters}), fin);
}

absl::Status MoqtBidiStreamBase::SendRequestError(
    uint64_t request_id, RequestErrorCode error_code,
    std::optional<quic::QuicTimeDelta> retry_interval,
    absl::string_view reason_phrase, bool fin) {
  MoqtRequestError request_error;
  request_error.request_id = request_id;
  request_error.error_code = error_code;
  request_error.retry_interval = retry_interval;
  request_error.reason_phrase = reason_phrase;
  return SendOrBufferMessage(framer_->SerializeRequestError(request_error),
                             fin);
}

absl::Status MoqtBidiStreamBase::SendRequestError(uint64_t request_id,
                                                  MoqtRequestErrorInfo info,
                                                  bool fin) {
  return SendRequestError(request_id, info.error_code, info.retry_interval,
                          info.reason_phrase, fin);
}

absl::Status MoqtBidiStreamBase::SendRequestUpdate(
    uint64_t request_id, uint64_t existing_request_id,
    const MessageParameters& parameters, MoqtResponseCallback callback) {
  MoqtRequestUpdate request_update;
  request_update.request_id = request_id;
  request_update.existing_request_id = existing_request_id;
  request_update.parameters = parameters;
  request_update_queue_.Enqueue(parameters, std::move(callback));
  return SendOrBufferMessage(framer_->SerializeRequestUpdate(request_update),
                             /*fin=*/false);
}

void MoqtBidiStreamBase::OnFatalError(absl::Status status) {
  QUICHE_DCHECK(!status.ok());
  if (session_error_callback_ == nullptr) {
    return;
  }
  std::optional<MoqtError> error_code = GetMoqtErrorForStatus(status);
  if (!error_code.has_value()) {
    switch (status.code()) {
      case absl::StatusCode::kInvalidArgument:
        error_code = MoqtError::kProtocolViolation;
        break;
      case absl::StatusCode::kAlreadyExists:
        error_code = MoqtError::kDuplicateTrackAlias;
        break;
      default:
        error_code = MoqtError::kInternalError;
        break;
    }
  }
  std::move(session_error_callback_)(*error_code, status.message());
  session_error_callback_ = nullptr;
}

}  // namespace moqt
