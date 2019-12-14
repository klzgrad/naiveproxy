// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"

#include <cstddef>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

QpackEncoderStreamSender::QpackEncoderStreamSender() : delegate_(nullptr) {}

void QpackEncoderStreamSender::SendInsertWithNameReference(
    bool is_static,
    uint64_t name_index,
    QuicStringPiece value) {
  values_.s_bit = is_static;
  values_.varint = name_index;
  values_.value = value;

  instruction_encoder_.Encode(InsertWithNameReferenceInstruction(), values_,
                              &buffer_);
}

void QpackEncoderStreamSender::SendInsertWithoutNameReference(
    QuicStringPiece name,
    QuicStringPiece value) {
  values_.name = name;
  values_.value = value;

  instruction_encoder_.Encode(InsertWithoutNameReferenceInstruction(), values_,
                              &buffer_);
}

void QpackEncoderStreamSender::SendDuplicate(uint64_t index) {
  values_.varint = index;

  instruction_encoder_.Encode(DuplicateInstruction(), values_, &buffer_);
}

void QpackEncoderStreamSender::SendSetDynamicTableCapacity(uint64_t capacity) {
  values_.varint = capacity;

  instruction_encoder_.Encode(SetDynamicTableCapacityInstruction(), values_,
                              &buffer_);
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
