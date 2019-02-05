// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_instruction_decoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"

namespace quic {

QpackInstructionDecoder::QpackInstructionDecoder(const QpackLanguage* language,
                                                 Delegate* delegate)
    : language_(language),
      delegate_(delegate),
      is_static_(false),
      varint_(0),
      is_huffman_encoded_(false),
      string_length_(0),
      error_detected_(false),
      state_(State::kStartInstruction) {}

void QpackInstructionDecoder::Decode(QuicStringPiece data) {
  DCHECK(!data.empty());
  DCHECK(!error_detected_);

  while (true) {
    size_t bytes_consumed = 0;

    switch (state_) {
      case State::kStartInstruction:
        DoStartInstruction(data);
        break;
      case State::kStartField:
        DoStartField();
        break;
      case State::kReadBit:
        DoReadBit(data);
        break;
      case State::kVarintStart:
        bytes_consumed = DoVarintStart(data);
        break;
      case State::kVarintResume:
        bytes_consumed = DoVarintResume(data);
        break;
      case State::kVarintDone:
        DoVarintDone();
        break;
      case State::kReadString:
        bytes_consumed = DoReadString(data);
        break;
      case State::kReadStringDone:
        DoReadStringDone();
        break;
    }

    if (error_detected_) {
      return;
    }

    DCHECK_LE(bytes_consumed, data.size());

    data = QuicStringPiece(data.data() + bytes_consumed,
                           data.size() - bytes_consumed);

    // Stop processing if no more data but next state would require it.
    if (data.empty() && (state_ != State::kStartField) &&
        (state_ != State::kVarintDone) && (state_ != State::kReadStringDone)) {
      return;
    }
  }
}

bool QpackInstructionDecoder::AtInstructionBoundary() const {
  return state_ == State::kStartInstruction;
}

void QpackInstructionDecoder::DoStartInstruction(QuicStringPiece data) {
  DCHECK(!data.empty());

  instruction_ = LookupOpcode(data[0]);
  field_ = instruction_->fields.begin();

  state_ = State::kStartField;
}

void QpackInstructionDecoder::DoStartField() {
  if (field_ == instruction_->fields.end()) {
    // Completed decoding this instruction.

    if (!delegate_->OnInstructionDecoded(instruction_)) {
      error_detected_ = true;
      return;
    }

    state_ = State::kStartInstruction;
    return;
  }

  switch (field_->type) {
    case QpackInstructionFieldType::kStaticBit:
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue:
      state_ = State::kReadBit;
      return;
    case QpackInstructionFieldType::kVarint:
      state_ = State::kVarintStart;
      return;
  }
}

void QpackInstructionDecoder::DoReadBit(QuicStringPiece data) {
  DCHECK(!data.empty());

  switch (field_->type) {
    case QpackInstructionFieldType::kStaticBit: {
      const uint8_t bitmask = field_->param;
      is_static_ = (data[0] & bitmask) == bitmask;

      ++field_;
      state_ = State::kStartField;

      return;
    }
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue: {
      const uint8_t prefix_length = field_->param;
      DCHECK_GE(7, prefix_length);
      const uint8_t bitmask = 1 << prefix_length;
      is_huffman_encoded_ = (data[0] & bitmask) == bitmask;

      state_ = State::kVarintStart;

      return;
    }
    default:
      DCHECK(false);
  }
}

size_t QpackInstructionDecoder::DoVarintStart(QuicStringPiece data) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  http2::DecodeBuffer buffer(data.data() + 1, data.size() - 1);
  http2::DecodeStatus status =
      varint_decoder_.Start(data[0], field_->param, &buffer);

  size_t bytes_consumed = 1 + buffer.Offset();
  switch (status) {
    case http2::DecodeStatus::kDecodeDone:
      state_ = State::kVarintDone;
      return bytes_consumed;
    case http2::DecodeStatus::kDecodeInProgress:
      state_ = State::kVarintResume;
      return bytes_consumed;
    case http2::DecodeStatus::kDecodeError:
      OnError("Encoded integer too large.");
      return bytes_consumed;
  }
}

size_t QpackInstructionDecoder::DoVarintResume(QuicStringPiece data) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  http2::DecodeBuffer buffer(data);
  http2::DecodeStatus status = varint_decoder_.Resume(&buffer);

  size_t bytes_consumed = buffer.Offset();
  switch (status) {
    case http2::DecodeStatus::kDecodeDone:
      state_ = State::kVarintDone;
      return bytes_consumed;
    case http2::DecodeStatus::kDecodeInProgress:
      DCHECK_EQ(bytes_consumed, data.size());
      DCHECK(buffer.Empty());
      return bytes_consumed;
    case http2::DecodeStatus::kDecodeError:
      OnError("Encoded integer too large.");
      return bytes_consumed;
  }
}

void QpackInstructionDecoder::DoVarintDone() {
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  if (field_->type == QpackInstructionFieldType::kVarint) {
    varint_ = varint_decoder_.value();

    ++field_;
    state_ = State::kStartField;
    return;
  }

  string_length_ = varint_decoder_.value();
  if (string_length_ > kStringLiteralLengthLimit) {
    OnError("String literal too long.");
    return;
  }

  QuicString* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  string->clear();

  if (string_length_ == 0) {
    ++field_;
    state_ = State::kStartField;
    return;
  }

  string->reserve(string_length_);

  state_ = State::kReadString;
}

size_t QpackInstructionDecoder::DoReadString(QuicStringPiece data) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  QuicString* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  DCHECK_LT(string->size(), string_length_);

  size_t bytes_consumed =
      std::min(string_length_ - string->size(), data.size());
  string->append(data.data(), bytes_consumed);

  DCHECK_LE(string->size(), string_length_);
  if (string->size() == string_length_) {
    state_ = State::kReadStringDone;
  }
  return bytes_consumed;
}

void QpackInstructionDecoder::DoReadStringDone() {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  QuicString* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  DCHECK_EQ(string->size(), string_length_);

  if (is_huffman_encoded_) {
    huffman_decoder_.Reset();
    // HpackHuffmanDecoder::Decode() cannot perform in-place decoding.
    QuicString decoded_value;
    huffman_decoder_.Decode(*string, &decoded_value);
    if (!huffman_decoder_.InputProperlyTerminated()) {
      OnError("Error in Huffman-encoded string.");
      return;
    }
    *string = std::move(decoded_value);
  }

  ++field_;
  state_ = State::kStartField;
}

const QpackInstruction* QpackInstructionDecoder::LookupOpcode(
    uint8_t byte) const {
  for (const auto* instruction : *language_) {
    if ((byte & instruction->opcode.mask) == instruction->opcode.value) {
      return instruction;
    }
  }
  // |language_| should be defined such that instruction opcodes cover every
  // possible input.
  DCHECK(false);
  return nullptr;
}

void QpackInstructionDecoder::OnError(QuicStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  delegate_->OnError(error_message);
}

}  // namespace quic
