// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_decoder.h"

#include <algorithm>
#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

// Maximum length of header name and header value.  This limits the amount of
// memory the peer can make the decoder allocate when sending string literals.
const size_t kStringLiteralLengthLimit = 1024 * 1024;

}  // namespace

QpackInstructionDecoder::QpackInstructionDecoder(const QpackLanguage* language,
                                                 Delegate* delegate)
    : language_(language),
      delegate_(delegate),
      s_bit_(false),
      varint_(0),
      varint2_(0),
      is_huffman_encoded_(false),
      string_length_(0),
      error_detected_(false),
      state_(State::kStartInstruction) {}

bool QpackInstructionDecoder::Decode(quiche::QuicheStringPiece data) {
  DCHECK(!data.empty());
  DCHECK(!error_detected_);

  while (true) {
    bool success = true;
    size_t bytes_consumed = 0;

    switch (state_) {
      case State::kStartInstruction:
        success = DoStartInstruction(data);
        break;
      case State::kStartField:
        success = DoStartField();
        break;
      case State::kReadBit:
        success = DoReadBit(data);
        break;
      case State::kVarintStart:
        success = DoVarintStart(data, &bytes_consumed);
        break;
      case State::kVarintResume:
        success = DoVarintResume(data, &bytes_consumed);
        break;
      case State::kVarintDone:
        success = DoVarintDone();
        break;
      case State::kReadString:
        success = DoReadString(data, &bytes_consumed);
        break;
      case State::kReadStringDone:
        success = DoReadStringDone();
        break;
    }

    if (!success) {
      return false;
    }

    // |success| must be false if an error is detected.
    DCHECK(!error_detected_);

    DCHECK_LE(bytes_consumed, data.size());

    data = quiche::QuicheStringPiece(data.data() + bytes_consumed,
                                     data.size() - bytes_consumed);

    // Stop processing if no more data but next state would require it.
    if (data.empty() && (state_ != State::kStartField) &&
        (state_ != State::kVarintDone) && (state_ != State::kReadStringDone)) {
      return true;
    }
  }

  return true;
}

bool QpackInstructionDecoder::AtInstructionBoundary() const {
  return state_ == State::kStartInstruction;
}

bool QpackInstructionDecoder::DoStartInstruction(
    quiche::QuicheStringPiece data) {
  DCHECK(!data.empty());

  instruction_ = LookupOpcode(data[0]);
  field_ = instruction_->fields.begin();

  state_ = State::kStartField;
  return true;
}

bool QpackInstructionDecoder::DoStartField() {
  if (field_ == instruction_->fields.end()) {
    // Completed decoding this instruction.

    if (!delegate_->OnInstructionDecoded(instruction_)) {
      return false;
    }

    state_ = State::kStartInstruction;
    return true;
  }

  switch (field_->type) {
    case QpackInstructionFieldType::kSbit:
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue:
      state_ = State::kReadBit;
      return true;
    case QpackInstructionFieldType::kVarint:
    case QpackInstructionFieldType::kVarint2:
      state_ = State::kVarintStart;
      return true;
    default:
      QUIC_BUG << "Invalid field type.";
      return false;
  }
}

bool QpackInstructionDecoder::DoReadBit(quiche::QuicheStringPiece data) {
  DCHECK(!data.empty());

  switch (field_->type) {
    case QpackInstructionFieldType::kSbit: {
      const uint8_t bitmask = field_->param;
      s_bit_ = (data[0] & bitmask) == bitmask;

      ++field_;
      state_ = State::kStartField;

      return true;
    }
    case QpackInstructionFieldType::kName:
    case QpackInstructionFieldType::kValue: {
      const uint8_t prefix_length = field_->param;
      DCHECK_GE(7, prefix_length);
      const uint8_t bitmask = 1 << prefix_length;
      is_huffman_encoded_ = (data[0] & bitmask) == bitmask;

      state_ = State::kVarintStart;

      return true;
    }
    default:
      QUIC_BUG << "Invalid field type.";
      return false;
  }
}

bool QpackInstructionDecoder::DoVarintStart(quiche::QuicheStringPiece data,
                                            size_t* bytes_consumed) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  http2::DecodeBuffer buffer(data.data() + 1, data.size() - 1);
  http2::DecodeStatus status =
      varint_decoder_.Start(data[0], field_->param, &buffer);

  *bytes_consumed = 1 + buffer.Offset();
  switch (status) {
    case http2::DecodeStatus::kDecodeDone:
      state_ = State::kVarintDone;
      return true;
    case http2::DecodeStatus::kDecodeInProgress:
      state_ = State::kVarintResume;
      return true;
    case http2::DecodeStatus::kDecodeError:
      OnError("Encoded integer too large.");
      return false;
    default:
      QUIC_BUG << "Unknown decode status " << status;
      return false;
  }
}

bool QpackInstructionDecoder::DoVarintResume(quiche::QuicheStringPiece data,
                                             size_t* bytes_consumed) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  http2::DecodeBuffer buffer(data);
  http2::DecodeStatus status = varint_decoder_.Resume(&buffer);

  *bytes_consumed = buffer.Offset();
  switch (status) {
    case http2::DecodeStatus::kDecodeDone:
      state_ = State::kVarintDone;
      return true;
    case http2::DecodeStatus::kDecodeInProgress:
      DCHECK_EQ(*bytes_consumed, data.size());
      DCHECK(buffer.Empty());
      return true;
    case http2::DecodeStatus::kDecodeError:
      OnError("Encoded integer too large.");
      return false;
    default:
      QUIC_BUG << "Unknown decode status " << status;
      return false;
  }
}

bool QpackInstructionDecoder::DoVarintDone() {
  DCHECK(field_->type == QpackInstructionFieldType::kVarint ||
         field_->type == QpackInstructionFieldType::kVarint2 ||
         field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  if (field_->type == QpackInstructionFieldType::kVarint) {
    varint_ = varint_decoder_.value();

    ++field_;
    state_ = State::kStartField;
    return true;
  }

  if (field_->type == QpackInstructionFieldType::kVarint2) {
    varint2_ = varint_decoder_.value();

    ++field_;
    state_ = State::kStartField;
    return true;
  }

  string_length_ = varint_decoder_.value();
  if (string_length_ > kStringLiteralLengthLimit) {
    OnError("String literal too long.");
    return false;
  }

  std::string* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  string->clear();

  if (string_length_ == 0) {
    ++field_;
    state_ = State::kStartField;
    return true;
  }

  string->reserve(string_length_);

  state_ = State::kReadString;
  return true;
}

bool QpackInstructionDecoder::DoReadString(quiche::QuicheStringPiece data,
                                           size_t* bytes_consumed) {
  DCHECK(!data.empty());
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  std::string* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  DCHECK_LT(string->size(), string_length_);

  *bytes_consumed = std::min(string_length_ - string->size(), data.size());
  string->append(data.data(), *bytes_consumed);

  DCHECK_LE(string->size(), string_length_);
  if (string->size() == string_length_) {
    state_ = State::kReadStringDone;
  }
  return true;
}

bool QpackInstructionDecoder::DoReadStringDone() {
  DCHECK(field_->type == QpackInstructionFieldType::kName ||
         field_->type == QpackInstructionFieldType::kValue);

  std::string* const string =
      (field_->type == QpackInstructionFieldType::kName) ? &name_ : &value_;
  DCHECK_EQ(string->size(), string_length_);

  if (is_huffman_encoded_) {
    huffman_decoder_.Reset();
    // HpackHuffmanDecoder::Decode() cannot perform in-place decoding.
    std::string decoded_value;
    huffman_decoder_.Decode(*string, &decoded_value);
    if (!huffman_decoder_.InputProperlyTerminated()) {
      OnError("Error in Huffman-encoded string.");
      return false;
    }
    *string = std::move(decoded_value);
  }

  ++field_;
  state_ = State::kStartField;
  return true;
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

void QpackInstructionDecoder::OnError(quiche::QuicheStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  delegate_->OnError(error_message);
}

}  // namespace quic
