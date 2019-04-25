// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_instruction_encoder.h"

#include "base/logging.h"
#include "net/third_party/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_encoder.h"

namespace quic {

QpackInstructionEncoder::QpackInstructionEncoder()
    : s_bit_(false),
      varint_(0),
      varint2_(0),
      byte_(0),
      state_(State::kOpcode),
      instruction_(nullptr) {}

void QpackInstructionEncoder::Encode(const QpackInstruction* instruction) {
  DCHECK(!HasNext());

  state_ = State::kOpcode;
  instruction_ = instruction;
  field_ = instruction_->fields.begin();

  // Field list must not be empty.
  DCHECK(field_ != instruction_->fields.end());
}

bool QpackInstructionEncoder::HasNext() const {
  return instruction_ && (field_ != instruction_->fields.end());
}

void QpackInstructionEncoder::Next(size_t max_encoded_bytes,
                                   QuicString* output) {
  DCHECK(HasNext());
  DCHECK_NE(0u, max_encoded_bytes);

  while (max_encoded_bytes > 0 && HasNext()) {
    size_t encoded_bytes = 0;

    switch (state_) {
      case State::kOpcode:
        DoOpcode();
        break;
      case State::kStartField:
        DoStartField();
        break;
      case State::kSbit:
        DoStaticBit();
        break;
      case State::kVarintStart:
        encoded_bytes = DoVarintStart(max_encoded_bytes, output);
        break;
      case State::kVarintResume:
        encoded_bytes = DoVarintResume(max_encoded_bytes, output);
        break;
      case State::kStartString:
        DoStartString();
        break;
      case State::kWriteString:
        encoded_bytes = DoWriteString(max_encoded_bytes, output);
        break;
    }

    DCHECK_LE(encoded_bytes, max_encoded_bytes);
    max_encoded_bytes -= encoded_bytes;
  }
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
      state_ = State::kVarintStart;
      return;
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue:
      state_ = State::kStartString;
      return;
  }
}

void QpackInstructionEncoder::DoStaticBit() {
  DCHECK(field_->type == QpackInstructionFieldType::kSbit);

  if (s_bit_) {
    DCHECK_EQ(0, byte_ & field_->param);

    byte_ |= field_->param;
  }

  ++field_;
  state_ = State::kStartField;
}

size_t QpackInstructionEncoder::DoVarintStart(size_t max_encoded_bytes,
                                              QuicString* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);
  DCHECK(!varint_encoder_.IsEncodingInProgress());

  uint64_t integer_to_encode;
  switch (field_->type) {
    case QpackInstructionFieldType::kVarint:
      integer_to_encode = varint_;
      break;
    case QpackInstructionFieldType::kVarint2:
      integer_to_encode = varint2_;
      break;
    default:
      integer_to_encode = string_to_write_.size();
      break;
  }

  output->push_back(
      varint_encoder_.StartEncoding(byte_, field_->param, integer_to_encode));
  byte_ = 0;

  if (varint_encoder_.IsEncodingInProgress()) {
    state_ = State::kVarintResume;
    return 1;
  }

  if (field_->type == QpackInstructionFieldType::kVarint ||
      field_->type == QpackInstructionFieldType::kVarint2) {
    ++field_;
    state_ = State::kStartField;
    return 1;
  }

  state_ = State::kWriteString;
  return 1;
}

size_t QpackInstructionEncoder::DoVarintResume(size_t max_encoded_bytes,
                                               QuicString* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);
  DCHECK(varint_encoder_.IsEncodingInProgress());

  const size_t encoded_bytes =
      varint_encoder_.ResumeEncoding(max_encoded_bytes, output);
  if (varint_encoder_.IsEncodingInProgress()) {
    DCHECK_EQ(encoded_bytes, max_encoded_bytes);
    return encoded_bytes;
  }

  DCHECK_LE(encoded_bytes, max_encoded_bytes);

  if (field_->type == QpackInstructionFieldType::kVarint ||
      field_->type == QpackInstructionFieldType::kVarint2) {
    ++field_;
    state_ = State::kStartField;
    return encoded_bytes;
  }

  state_ = State::kWriteString;
  return encoded_bytes;
}

void QpackInstructionEncoder::DoStartString() {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  string_to_write_ =
      (field_->type == QpackInstructionFieldType::kName) ? name_ : value_;
  http2::HuffmanEncode(string_to_write_, &huffman_encoded_string_);

  if (huffman_encoded_string_.size() < string_to_write_.size()) {
    DCHECK_EQ(0, byte_ & (1 << field_->param));

    byte_ |= (1 << field_->param);
    string_to_write_ = huffman_encoded_string_;
  }

  state_ = State::kVarintStart;
}

size_t QpackInstructionEncoder::DoWriteString(size_t max_encoded_bytes,
                                              QuicString* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  if (max_encoded_bytes < string_to_write_.size()) {
    const size_t encoded_bytes = max_encoded_bytes;
    QuicStrAppend(output, string_to_write_.substr(0, encoded_bytes));
    string_to_write_ = string_to_write_.substr(encoded_bytes);
    return encoded_bytes;
  }

  const size_t encoded_bytes = string_to_write_.size();
  QuicStrAppend(output, string_to_write_);

  ++field_;
  state_ = State::kStartField;
  return encoded_bytes;
}

}  // namespace quic
