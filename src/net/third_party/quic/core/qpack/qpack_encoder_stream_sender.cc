// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_encoder_stream_sender.h"

#include <cstddef>
#include <limits>

#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

QpackEncoderStreamSender::QpackEncoderStreamSender(Delegate* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_);
}

void QpackEncoderStreamSender::SendInsertWithNameReference(
    bool is_static,
    uint64_t name_index,
    QuicStringPiece value) {
  instruction_encoder_.set_s_bit(is_static);
  instruction_encoder_.set_varint(name_index);
  instruction_encoder_.set_value(value);

  instruction_encoder_.Encode(InsertWithNameReferenceInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->WriteEncoderStreamData(output);
}

void QpackEncoderStreamSender::SendInsertWithoutNameReference(
    QuicStringPiece name,
    QuicStringPiece value) {
  instruction_encoder_.set_name(name);
  instruction_encoder_.set_value(value);

  instruction_encoder_.Encode(InsertWithoutNameReferenceInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->WriteEncoderStreamData(output);
}

void QpackEncoderStreamSender::SendDuplicate(uint64_t index) {
  instruction_encoder_.set_varint(index);

  instruction_encoder_.Encode(DuplicateInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->WriteEncoderStreamData(output);
}

void QpackEncoderStreamSender::SendSetDynamicTableCapacity(uint64_t capacity) {
  instruction_encoder_.set_varint(capacity);

  instruction_encoder_.Encode(SetDynamicTableCapacityInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->WriteEncoderStreamData(output);
}

}  // namespace quic
