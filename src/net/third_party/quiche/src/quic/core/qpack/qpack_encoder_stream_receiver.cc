// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_receiver.h"

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackEncoderStreamReceiver::QpackEncoderStreamReceiver(Delegate* delegate)
    : instruction_decoder_(QpackEncoderStreamLanguage(), this),
      delegate_(delegate),
      error_detected_(false) {
  DCHECK(delegate_);
}

void QpackEncoderStreamReceiver::Decode(quiche::QuicheStringPiece data) {
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

  DCHECK_EQ(instruction, SetDynamicTableCapacityInstruction());
  delegate_->OnSetDynamicTableCapacity(instruction_decoder_.varint());
  return true;
}

void QpackEncoderStreamReceiver::OnError(
    quiche::QuicheStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  delegate_->OnErrorDetected(error_message);
}

}  // namespace quic
