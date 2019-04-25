// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_constants.h"

#include <limits>

#include "base/logging.h"

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

}  // namespace quic
