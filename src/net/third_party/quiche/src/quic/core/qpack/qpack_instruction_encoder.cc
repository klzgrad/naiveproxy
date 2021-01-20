// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"

#include <limits>

#include "absl/strings/string_view.h"
#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_encoder.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_encoder.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"

namespace quic {

QpackInstructionEncoder::QpackInstructionEncoder()
    : use_huffman_(false),
      string_length_(0),
      byte_(0),
      state_(State::kOpcode),
      instruction_(nullptr),
      use_fast_huffman_encoder_(
          GetQuicReloadableFlag(quic_use_fast_huffman_encoder)) {
  if (use_fast_huffman_encoder_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_use_fast_huffman_encoder);
  }
}

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
        DoWriteString(instruction_with_values.name(),
                      instruction_with_values.value(), output);
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
      integer_to_encode = string_length_;
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

void QpackInstructionEncoder::DoStartString(absl::string_view name,
                                            absl::string_view value) {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  absl::string_view string_to_write =
      (field_->type == QpackInstructionFieldType::kName) ? name : value;
  string_length_ = string_to_write.size();

  size_t encoded_size = http2::HuffmanSize(string_to_write);
  use_huffman_ = encoded_size < string_length_;

  if (use_huffman_) {
    DCHECK_EQ(0, byte_ & (1 << field_->param));
    byte_ |= (1 << field_->param);

    string_length_ = encoded_size;
  }

  state_ = State::kVarintEncode;
}

void QpackInstructionEncoder::DoWriteString(absl::string_view name,
                                            absl::string_view value,
                                            std::string* output) {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  absl::string_view string_to_write =
      (field_->type == QpackInstructionFieldType::kName) ? name : value;
  if (use_huffman_) {
    if (use_fast_huffman_encoder_) {
      http2::HuffmanEncodeFast(string_to_write, string_length_, output);
    } else {
      http2::HuffmanEncode(string_to_write, string_length_, output);
    }
  } else {
    QuicStrAppend(output, string_to_write);
  }

  ++field_;
  state_ = State::kStartField;
}

}  // namespace quic
