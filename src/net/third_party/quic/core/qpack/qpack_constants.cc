// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_constants.h"

namespace quic {

bool operator==(const QpackInstructionOpcode& a,
                const QpackInstructionOpcode& b) {
  return std::tie(a.value, a.mask) == std::tie(b.value, b.mask);
}

const QpackInstruction* InsertWithNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kInsertWithNameReferenceOpcode,
                                 kInsertWithNameReferenceOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kStaticBit,
        kInsertWithNameReferenceStaticBit},
       {QpackInstructionFieldType::kVarint,
        kInsertWithNameReferenceNameIndexPrefixLength},
       {QpackInstructionFieldType::kValue, kLiteralValuePrefixLength}}};
  return instruction;
}

const QpackInstruction* InsertWithoutNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kInsertWithoutNameReferenceOpcode,
                                 kInsertWithoutNameReferenceOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kName,
        kInsertWithoutNameReferenceNameLengthPrefixLength},
       {QpackInstructionFieldType::kValue, kLiteralValuePrefixLength}}};
  return instruction;
}

const QpackInstruction* DuplicateInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kDuplicateOpcode, kDuplicateOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kVarint, kDuplicateIndexPrefixLength}}};
  return instruction;
}

const QpackInstruction* DynamicTableSizeUpdateInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kDynamicTableSizeUpdateOpcode,
                                 kDynamicTableSizeUpdateOpcodeMask};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kVarint,
                             kDynamicTableSizeUpdateMaxSizePrefixLength}}};
  return instruction;
}

const QpackLanguage* QpackEncoderStreamLanguage() {
  static const QpackLanguage* const language = new QpackLanguage{
      InsertWithNameReferenceInstruction(),
      InsertWithoutNameReferenceInstruction(), DuplicateInstruction(),
      DynamicTableSizeUpdateInstruction()};
  return language;
}

const QpackInstruction* QpackIndexedHeaderFieldInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kIndexedHeaderFieldOpcodeValue,
                                 kIndexedHeaderFieldOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kStaticBit, kIndexedHeaderFieldStaticBit},
       {QpackInstructionFieldType::kVarint, kIndexedHeaderFieldPrefixLength}}};
  return instruction;
}

const QpackInstruction* QpackIndexedHeaderFieldPostBaseInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kIndexedHeaderFieldPostBaseOpcodeValue,
                                 kIndexedHeaderFieldPostBaseOpcodeMask};
  static const QpackInstruction* const instruction =
      new QpackInstruction{*opcode,
                           {{QpackInstructionFieldType::kVarint,
                             kIndexedHeaderFieldPostBasePrefixLength}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldNameReferenceInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kLiteralHeaderFieldNameReferenceOpcodeValue,
                                 kLiteralHeaderFieldNameReferenceOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kStaticBit,
        kLiteralHeaderFieldNameReferenceStaticBit},
       {QpackInstructionFieldType::kVarint,
        kLiteralHeaderFieldNameReferencePrefixLength},
       {QpackInstructionFieldType::kValue, kLiteralValuePrefixLength}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldPostBaseInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kLiteralHeaderFieldPostBaseOpcodeValue,
                                 kLiteralHeaderFieldPostBaseOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kVarint,
        kLiteralHeaderFieldPostBasePrefixLength},
       {QpackInstructionFieldType::kValue, kLiteralValuePrefixLength}}};
  return instruction;
}

const QpackInstruction* QpackLiteralHeaderFieldInstruction() {
  static const QpackInstructionOpcode* const opcode =
      new QpackInstructionOpcode{kLiteralHeaderFieldOpcodeValue,
                                 kLiteralHeaderFieldOpcodeMask};
  static const QpackInstruction* const instruction = new QpackInstruction{
      *opcode,
      {{QpackInstructionFieldType::kName, kLiteralHeaderFieldPrefixLength},
       {QpackInstructionFieldType::kValue, kLiteralValuePrefixLength}}};
  return instruction;
}

const QpackLanguage* QpackRequestStreamLanguage() {
  static const QpackLanguage* const language =
      new QpackLanguage{QpackIndexedHeaderFieldInstruction(),
                        QpackIndexedHeaderFieldPostBaseInstruction(),
                        QpackLiteralHeaderFieldNameReferenceInstruction(),
                        QpackLiteralHeaderFieldPostBaseInstruction(),
                        QpackLiteralHeaderFieldInstruction()};
  return language;
}

}  // namespace quic
