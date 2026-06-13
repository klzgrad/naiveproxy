// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_encoder_stream_receiver.h"

#include "absl/strings/string_view.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/quic/core/qpack/qpack_instructions.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

QpackEncoderStreamReceiver::QpackEncoderStreamReceiver(Delegate* delegate)
    : instruction_decoder_(QpackEncoderStreamLanguage(), this),
      delegate_(delegate),
      error_detected_(false) {
  QUICHE_DCHECK(delegate_);
}

void QpackEncoderStreamReceiver::Decode(absl::string_view data) {
  if (data.empty() || error_detected_) {
    return;
  }

  instruction_decoder_.Decode(data);
}

bool QpackEncoderStreamReceiver::OnInstructionDecoded(
    const QpackInstruction* instruction) {
  if (instruction == InsertWithNameReferenceInstruction()) {
    delegate_->OnInsertWithNameReference(instruction_decoder_.s_bit(),
                                         instruction_decoder_.varint(),
                                         instruction_decoder_.value());
    return true;
  }

  if (instruction == InsertWithoutNameReferenceInstruction()) {
    delegate_->OnInsertWithoutNameReference(instruction_decoder_.name(),
                                            instruction_decoder_.value());
    return true;
  }

  if (instruction == DuplicateInstruction()) {
    delegate_->OnDuplicate(instruction_decoder_.varint());
    return true;
  }

  QUICHE_DCHECK_EQ(instruction, SetDynamicTableCapacityInstruction());
  delegate_->OnSetDynamicTableCapacity(instruction_decoder_.varint());
  return true;
}

void QpackEncoderStreamReceiver::OnInstructionDecodingError(
    QpackInstructionDecoder::ErrorCode error_code,
    absl::string_view error_message) {
  QUICHE_DCHECK(!error_detected_);

  error_detected_ = true;

  QuicErrorCode quic_error_code;
  switch (error_code) {
    case QpackInstructionDecoder::ErrorCode::INTEGER_TOO_LARGE:
      quic_error_code = QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE;
      break;
    case QpackInstructionDecoder::ErrorCode::STRING_LITERAL_TOO_LONG:
      quic_error_code = QUIC_QPACK_ENCODER_STREAM_STRING_LITERAL_TOO_LONG;
      break;
    case QpackInstructionDecoder::ErrorCode::HUFFMAN_ENCODING_ERROR:
      quic_error_code = QUIC_QPACK_ENCODER_STREAM_HUFFMAN_ENCODING_ERROR;
      break;
    default:
      quic_error_code = QUIC_INTERNAL_ERROR;
  }

  delegate_->OnErrorDetected(quic_error_code, error_message);
}

}  // namespace quic
