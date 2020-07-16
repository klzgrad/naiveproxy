// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"

#include <limits>

#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_encoder.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_encoder.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackInstructionEncoder::QpackInstructionEncoder()
    : byte_(0), state_(State::kOpcode), instruction_(nullptr) {}

void QpackInstructionEncoder::Encode(
    const QpackInstructionWithValues& instruction_with_values,
    std::string* output) {
  DCHECK(instruction_with_values.instruction());

  state_ = State::kOpcode;
  instruction_ = instruction_with_values.instruction();
  field_ = instruction_->fields.begin();

  // Field list must not be empty.
  DCHECK(field_ != instruction_->fields.end());

  do {
    switch (state_) {
      case State::kOpcode:
        DoOpcode();
        break;
      case State::kStartField:
        DoStartField();
        break;
      case State::kSbit:
        DoSBit(instruction_with_values.s_bit());
        break;
      case State::kVarintEncode:
        DoVarintEncode(instruction_with_values.varint(),
                       instruction_with_values.varint2(), output);
        break;
      case State::kStartString:
        DoStartString(instruction_with_values.name(),
                      instruction_with_values.value());
        break;
      case State::kWriteString:
        DoWriteString(output);
        break;
    }
  } while (field_ != instruction_->fields.end());

  DCHECK(state_ == State::kStartField);
}

void QpackInstructionEncoder::DoOpcode() {
  DCHECK_EQ(0u, byte_);

  byte_ = instruction_->opcode.value;

  state_ = State::kStartField;
}

void QpackInstructionEncoder::DoStartField() {
  switch (field_->type) {
    case QpackInstructionFieldType::kSbit:
      state_ = State::kSbit;
      return;
    case QpackInstructionFieldType::kVarint:
    case QpackInstructionFieldType::kVarint2:
      state_ = State::kVarintEncode;
      return;
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue:
      state_ = State::kStartString;
      return;
  }
}

void QpackInstructionEncoder::DoSBit(bool s_bit) {
  DCHECK(field_->type == QpackInstructionFieldType::kSbit);

  if (s_bit) {
    DCHECK_EQ(0, byte_ & field_->param);

    byte_ |= field_->param;
  }

  ++field_;
  state_ = State::kStartField;
}

void QpackInstructionEncoder::DoVarintEncode(uint64_t varint,
                                             uint64_t varint2,
                                             std::string* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);
  uint64_t integer_to_encode;
  switch (field_->type) {
    case QpackInstructionFieldType::kVarint:
      integer_to_encode = varint;
      break;
    case QpackInstructionFieldType::kVarint2:
      integer_to_encode = varint2;
      break;
    default:
      integer_to_encode = string_to_write_.size();
      break;
  }

  http2::HpackVarintEncoder::Encode(byte_, field_->param, integer_to_encode,
                                    output);
  byte_ = 0;

  if (field_->type == QpackInstructionFieldType::kVarint ||
      field_->type == QpackInstructionFieldType::kVarint2) {
    ++field_;
    state_ = State::kStartField;
    return;
  }

  state_ = State::kWriteString;
}

void QpackInstructionEncoder::DoStartString(quiche::QuicheStringPiece name,
                                            quiche::QuicheStringPiece value) {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  string_to_write_ =
      (field_->type == QpackInstructionFieldType::kName) ? name : value;
  http2::HuffmanEncode(string_to_write_, &huffman_encoded_string_);

  if (huffman_encoded_string_.size() < string_to_write_.size()) {
    DCHECK_EQ(0, byte_ & (1 << field_->param));

    byte_ |= (1 << field_->param);
    string_to_write_ = huffman_encoded_string_;
  }

  state_ = State::kVarintEncode;
}

void QpackInstructionEncoder::DoWriteString(std::string* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  QuicStrAppend(output, string_to_write_);

  ++field_;
  state_ = State::kStartField;
}

}  // namespace quic
