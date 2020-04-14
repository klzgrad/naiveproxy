// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"

#include <cstddef>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackEncoderStreamSender::QpackEncoderStreamSender() : delegate_(nullptr) {}

void QpackEncoderStreamSender::SendInsertWithNameReference(
    bool is_static,
    uint64_t name_index,
    quiche::QuicheStringPiece value) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::InsertWithNameReference(is_static, name_index,
                                                          value),
      &buffer_);
}

void QpackEncoderStreamSender::SendInsertWithoutNameReference(
    quiche::QuicheStringPiece name,
    quiche::QuicheStringPiece value) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::InsertWithoutNameReference(name, value),
      &buffer_);
}

void QpackEncoderStreamSender::SendDuplicate(uint64_t index) {
  instruction_encoder_.Encode(QpackInstructionWithValues::Duplicate(index),
                              &buffer_);
}

void QpackEncoderStreamSender::SendSetDynamicTableCapacity(uint64_t capacity) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::SetDynamicTableCapacity(capacity), &buffer_);
}

QuicByteCount QpackEncoderStreamSender::Flush() {
  if (buffer_.empty()) {
    return 0;
  }

  delegate_->WriteStreamData(buffer_);
  const QuicByteCount bytes_written = buffer_.size();
  buffer_.clear();
  return bytes_written;
}

}  // namespace quic
