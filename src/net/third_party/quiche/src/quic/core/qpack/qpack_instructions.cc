// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"

#include <limits>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

// Validate that
//  * in each instruction, the bits of |value| that are zero in |mask| are zero;
//  * every byte matches exactly one opcode.
void ValidateLangague(const QpackLanguage* language) {
#ifndef NDEBUG
  for (const auto* instruction : *language) {
    DCHECK_EQ(0, instruction->opcode.value & ~instruction->opcode.mask);
  }

  for (uint8_t byte = 0; byte < std::numeric_limits<uint8_t>::max(); ++byte) {
    size_t match_count = 0;
    for (const auto* instruction : *language) {
      if ((byte & instruction->opcode.mask) == instruction->opcode.value) {
        ++match_count;
      }
    }
    DCHECK_EQ(1u, match_count) << static_cast<int>(byte);
  }
#else
  (void)language;
#endif
}

}  // namespace

bool operator==(const QpackInstructionOpcode& a,
                const QpackInstructionOpcode& b) {
  return std::tie(a.value, a.mask) == std::tie(b.value, b.mask);
}

const QpackInstruction* InsertWithNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b10000000, 0b10000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kSbit, 0b01000000},
                            {QpackInstructionFieldType::kVarint, 6},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackInstruction* InsertWithoutNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b01000000, 0b11000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kName, 5},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackInstruction* DuplicateInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00000000, 0b11100000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 5}}};
  return instruction;
}

const QpackInstruction* SetDynamicTableCapacityInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00100000, 0b11100000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 5}}};
  return instruction;
}

const QpackLanguage* QpackEncoderStreamLanguage() {
  static const QpackLanguage* const language = new QpackLanguage{
      InsertWithNameReferenceInstruction(),
      InsertWithoutNameReferenceInstruction(), DuplicateInstruction(),
      SetDynamicTableCapacityInstruction()};
  ValidateLangague(language);
  return language;
}

const QpackInstruction* InsertCountIncrementInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00000000, 0b11000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 6}}};
  return instruction;
}

const QpackInstruction* HeaderAcknowledgementInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b10000000, 0b10000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 7}}};
  return instruction;
}

const QpackInstruction* StreamCancellationInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b01000000, 0b11000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 6}}};
  return instruction;
}

const QpackLanguage* QpackDecoderStreamLanguage() {
  static const QpackLanguage* const language = new QpackLanguage{
      InsertCountIncrementInstruction(), HeaderAcknowledgementInstruction(),
      StreamCancellationInstruction()};
  ValidateLangague(language);
  return language;
}

const QpackInstruction* QpackPrefixInstruction() {
  // This opcode matches every input.
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00000000, 0b00000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kVarint, 8},
                            {QpackInstructionFieldType::kSbit, 0b10000000},
                            {QpackInstructionFieldType::kVarint2, 7}}};
  return instruction;
}

const QpackLanguage* QpackPrefixLanguage() {
  static const QpackLanguage* const language =
      new QpackLanguage{QpackPrefixInstruction()};
  ValidateLangague(language);
  return language;
}

const QpackInstruction* QpackIndexedHeaderFieldInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b10000000, 0b10000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kSbit, 0b01000000},
                            {QpackInstructionFieldType::kVarint, 6}}};
  return instruction;
}

const QpackInstruction* QpackIndexedHeaderFieldPostBaseInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00010000, 0b11110000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode, {{QpackInstructionFieldType::kVarint, 4}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b01000000, 0b11000000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kSbit, 0b00010000},
                            {QpackInstructionFieldType::kVarint, 4},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldPostBaseInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00000000, 0b11110000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kVarint, 3},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{0b00100000, 0b11100000};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kName, 3},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackLanguage* QpackRequestStreamLanguage() {
  static const QpackLanguage* const language =
      new QpackLanguage{QpackIndexedHeaderFieldInstruction(),
                        QpackIndexedHeaderFieldPostBaseInstruction(),
                        QpackLiteralHeaderFieldNameReferenceInstruction(),
                        QpackLiteralHeaderFieldPostBaseInstruction(),
                        QpackLiteralHeaderFieldInstruction()};
  ValidateLangague(language);
  return language;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::InsertWithNameReference(
    bool is_static,
    uint64_t name_index,
    quiche::QuicheStringPiece value) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = InsertWithNameReferenceInstruction();
  instruction_with_values.s_bit_ = is_static;
  instruction_with_values.varint_ = name_index;
  instruction_with_values.value_ = value;

  return instruction_with_values;
}

// static
QpackInstructionWithValues
QpackInstructionWithValues::InsertWithoutNameReference(
    quiche::QuicheStringPiece name,
    quiche::QuicheStringPiece value) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ =
      InsertWithoutNameReferenceInstruction();
  instruction_with_values.name_ = name;
  instruction_with_values.value_ = value;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::Duplicate(
    uint64_t index) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = DuplicateInstruction();
  instruction_with_values.varint_ = index;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::SetDynamicTableCapacity(
    uint64_t capacity) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = SetDynamicTableCapacityInstruction();
  instruction_with_values.varint_ = capacity;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::InsertCountIncrement(
    uint64_t increment) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = InsertCountIncrementInstruction();
  instruction_with_values.varint_ = increment;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::HeaderAcknowledgement(
    uint64_t stream_id) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = HeaderAcknowledgementInstruction();
  instruction_with_values.varint_ = stream_id;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::StreamCancellation(
    uint64_t stream_id) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = StreamCancellationInstruction();
  instruction_with_values.varint_ = stream_id;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::Prefix(
    uint64_t required_insert_count) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = QpackPrefixInstruction();
  instruction_with_values.varint_ = required_insert_count;
  instruction_with_values.varint2_ = 0;    // Delta Base.
  instruction_with_values.s_bit_ = false;  // Delta Base sign.

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::IndexedHeaderField(
    bool is_static,
    uint64_t index) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = QpackIndexedHeaderFieldInstruction();
  instruction_with_values.s_bit_ = is_static;
  instruction_with_values.varint_ = index;

  return instruction_with_values;
}

// static
QpackInstructionWithValues
QpackInstructionWithValues::LiteralHeaderFieldNameReference(
    bool is_static,
    uint64_t index,
    quiche::QuicheStringPiece value) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ =
      QpackLiteralHeaderFieldNameReferenceInstruction();
  instruction_with_values.s_bit_ = is_static;
  instruction_with_values.varint_ = index;
  instruction_with_values.value_ = value;

  return instruction_with_values;
}

// static
QpackInstructionWithValues QpackInstructionWithValues::LiteralHeaderField(
    quiche::QuicheStringPiece name,
    quiche::QuicheStringPiece value) {
  QpackInstructionWithValues instruction_with_values;
  instruction_with_values.instruction_ = QpackLiteralHeaderFieldInstruction();
  instruction_with_values.name_ = name;
  instruction_with_values.value_ = value;

  return instruction_with_values;
}

}  // namespace quic
