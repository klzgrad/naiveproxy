// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_decoder_stream_receiver.h"

#include "absl/strings/string_view.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/quic/core/qpack/qpack_instructions.h"

namespace quic {

QpackDecoderStreamReceiver::QpackDecoderStreamReceiver(Delegate* delegate)
    : instruction_decoder_(QpackDecoderStreamLanguage(), this),
      delegate_(delegate),
      error_detected_(false) {
  QUICHE_DCHECK(delegate_);
}

void QpackDecoderStreamReceiver::Decode(absl::string_view data) {
  if (data.empty() || error_detected_) {
    return;
  }

  instruction_decoder_.Decode(data);
}

bool QpackDecoderStreamReceiver::OnInstructionDecoded(
    const QpackInstruction* instruction) {
  if (instruction == InsertCountIncrementInstruction()) {
    delegate_->OnInsertCountIncrement(instruction_decoder_.varint());
    return true;
  }

  if (instruction == HeaderAcknowledgementInstruction()) {
    delegate_->OnHeaderAcknowledgement(instruction_decoder_.varint());
    return true;
  }

  QUICHE_DCHECK_EQ(instruction, StreamCancellationInstruction());
  delegate_->OnStreamCancellation(instruction_decoder_.varint());
  return true;
}

void QpackDecoderStreamReceiver::OnInstructionDecodingError(
    QpackInstructionDecoder::ErrorCode error_code,
    absl::string_view error_message) {
  QUICHE_DCHECK(!error_detected_);

  error_detected_ = true;

  // There is no string literals on the decoder stream,
  // the only possible error is INTEGER_TOO_LARGE.
  QuicErrorCode quic_error_code =
      (error_code == QpackInstructionDecoder::ErrorCode::INTEGER_TOO_LARGE)
          ? QUIC_QPACK_DECODER_STREAM_INTEGER_TOO_LARGE
          : QUIC_INTERNAL_ERROR;
  delegate_->OnErrorDetected(quic_error_code, error_message);
}

}  // namespace quic
