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

QpackEncoderStreamSender::QpackEncoderStreamSender(
    QpackStreamSenderDelegate* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_);
}

void QpackEncoderStreamSender::SendInsertWithNameReference(
    bool is_static,
    uint64_t name_index,
    QuicStringPiece value) {
  values_.s_bit = is_static;
  values_.varint = name_index;
  values_.value = value;

  std::string output;
  instruction_encoder_.Encode(InsertWithNameReferenceInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

void QpackEncoderStreamSender::SendInsertWithoutNameReference(
    QuicStringPiece name,
    QuicStringPiece value) {
  values_.name = name;
  values_.value = value;

  std::string output;
  instruction_encoder_.Encode(InsertWithoutNameReferenceInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

void QpackEncoderStreamSender::SendDuplicate(uint64_t index) {
  values_.varint = index;

  std::string output;
  instruction_encoder_.Encode(DuplicateInstruction(), values_, &output);
  delegate_->WriteStreamData(output);
}

void QpackEncoderStreamSender::SendSetDynamicTableCapacity(uint64_t capacity) {
  values_.varint = capacity;

  std::string output;
  instruction_encoder_.Encode(SetDynamicTableCapacityInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

}  // namespace quic
