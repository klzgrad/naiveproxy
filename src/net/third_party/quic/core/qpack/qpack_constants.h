// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_CONSTANTS_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_CONSTANTS_H_

#include <cstdint>
#include <tuple>
#include <vector>

#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// Each instruction is identified with an opcode in the first byte.
// |mask| determines which bits are part of the opcode.
// |value| is the value of these bits.  (Other bits in value must be zero.)
struct QUIC_EXPORT_PRIVATE QpackInstructionOpcode {
  uint8_t value;
  uint8_t mask;
};

bool operator==(const QpackInstructionOpcode& a,
                const QpackInstructionOpcode& b);

// Possible types of an instruction field.  Decoding a static bit does not
// consume the current byte.  Decoding an integer or a length-prefixed string
// literal consumes all bytes containing the field value.
enum class QpackInstructionFieldType {
  // A single bit indicating whether the index is static.
  kStaticBit,
  // An integer encoded with variable length encoding.  This could be an index,
  // stream ID, or maximum size.
  kVarint,
  // A header name or header value encoded as:
  //   a bit indicating whether it is Huffman encoded;
  //   the encoded length of the string;
  //   the header name or value optionally Huffman encoded.
  kName,
  kValue
};

// Each instruction field has a type and a parameter.
// The meaning of the parameter depends on the field type.
struct QUIC_EXPORT_PRIVATE QpackInstructionField {
  QpackInstructionFieldType type;
  // For a kStaticBit field, |param| is a mask with exactly one bit set.
  // For kVarint fields, |param| is the prefix length of the integer encoding.
  // For kName and kValue fields, |param| is the prefix length of the length of
  // the string, and the bit immediately preceding the prefix is interpreted as
  // the Huffman bit.
  uint8_t param;
};

using QpackInstructionFields = std::vector<QpackInstructionField>;

// A QPACK instruction consists of an opcode identifying the instruction,
// followed by a non-empty list of fields.  The last field must be integer or
// string literal type to guarantee that all bytes of the instruction are
// consumed.
struct QUIC_EXPORT_PRIVATE QpackInstruction {
  QpackInstruction(const QpackInstruction&) = delete;
  const QpackInstruction& operator=(const QpackInstruction&) = delete;

  QpackInstructionOpcode opcode;
  QpackInstructionFields fields;
};

// A language is a collection of instructions.  The order does not matter.
// The set of instruction opcodes must cover all possible input.
using QpackLanguage = std::vector<const QpackInstruction*>;

// Maximum length of header name and header value.  This limits the amount of
// memory the peer can make the decoder allocate when sending string literals.
const size_t kStringLiteralLengthLimit = 1024 * 1024;

// TODO(bnc): Move this into HpackVarintEncoder.
// The integer encoder can encode up to 2^64-1, which can take up to 10 bytes
// (each carrying 7 bits) after the prefix.
const uint8_t kMaxExtensionBytesForVarintEncoding = 10;

// Wire format defined in
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5

// Value encoding for instructions with literal value.
const uint8_t kLiteralValueHuffmanMask = 0b10000000;
const uint8_t kLiteralValueWithoutHuffmanEncoding = 0b00000000;
const uint8_t kLiteralValuePrefixLength = 7;

// Encoder stream

// 5.2.1 Insert With Name Reference
const uint8_t kInsertWithNameReferenceOpcode = 0b10000000;
const uint8_t kInsertWithNameReferenceOpcodeMask = 0b10000000;
const uint8_t kInsertWithNameReferenceStaticBit = 0b01000000;
const uint8_t kInsertWithNameReferenceNameIndexPrefixLength = 6;

const QpackInstruction* InsertWithNameReferenceInstruction();

// 5.2.2 Insert Without Name Reference
const uint8_t kInsertWithoutNameReferenceOpcode = 0b01000000;
const uint8_t kInsertWithoutNameReferenceOpcodeMask = 0b11000000;
const uint8_t kInsertWithoutNameReferenceNameHuffmanBit = 0b00100000;
const uint8_t kInsertWithoutNameReferenceNameLengthPrefixLength = 5;

const QpackInstruction* InsertWithoutNameReferenceInstruction();

// 5.2.3 Duplicate
const uint8_t kDuplicateOpcode = 0b00000000;
const uint8_t kDuplicateOpcodeMask = 0b11100000;
const uint8_t kDuplicateIndexPrefixLength = 5;

const QpackInstruction* DuplicateInstruction();

// 5.2.4 Dynamic Table Size Update
const uint8_t kDynamicTableSizeUpdateOpcode = 0b00100000;
const uint8_t kDynamicTableSizeUpdateOpcodeMask = 0b11100000;
const uint8_t kDynamicTableSizeUpdateMaxSizePrefixLength = 5;

const QpackInstruction* DynamicTableSizeUpdateInstruction();

// Description of language (set of instructions) used on the encoder stream.
const QpackLanguage* QpackEncoderStreamLanguage();

// Request and push streams

// 5.4.2.1. Indexed Header Field
const uint8_t kIndexedHeaderFieldOpcodeValue = 0b10000000;
const uint8_t kIndexedHeaderFieldOpcodeMask = 0b10000000;
const uint8_t kIndexedHeaderFieldStaticBit = 0b01000000;
const uint8_t kIndexedHeaderFieldPrefixLength = 6;

const QpackInstruction* QpackIndexedHeaderFieldInstruction();

// 5.4.2.2. Indexed Header Field With Post-Base Index
const uint8_t kIndexedHeaderFieldPostBaseOpcodeValue = 0b00010000;
const uint8_t kIndexedHeaderFieldPostBaseOpcodeMask = 0b11110000;
const uint8_t kIndexedHeaderFieldPostBasePrefixLength = 4;

const QpackInstruction* QpackIndexedHeaderFieldPostBaseInstruction();

// 5.4.2.3. Literal Header Field With Name Reference
const uint8_t kLiteralHeaderFieldNameReferenceOpcodeValue = 0b01000000;
const uint8_t kLiteralHeaderFieldNameReferenceOpcodeMask = 0b11000000;
const uint8_t kLiteralHeaderFieldNameReferenceStaticBit = 0b00010000;
const uint8_t kLiteralHeaderFieldNameReferencePrefixLength = 4;

const QpackInstruction* QpackLiteralHeaderFieldNameReferenceInstruction();

// 5.4.2.4. Literal Header Field With Post-Base Name Reference
const uint8_t kLiteralHeaderFieldPostBaseOpcodeValue = 0b00000000;
const uint8_t kLiteralHeaderFieldPostBaseOpcodeMask = 0b11110000;
const uint8_t kLiteralHeaderFieldPostBasePrefixLength = 3;

const QpackInstruction* QpackLiteralHeaderFieldPostBaseInstruction();

// 5.4.2.5. Literal Header Field Without Name Reference
const uint8_t kLiteralHeaderFieldOpcodeValue = 0b00100000;
const uint8_t kLiteralHeaderFieldOpcodeMask = 0b11100000;
const uint8_t kLiteralNameHuffmanMask = 0b00001000;
const uint8_t kLiteralHeaderFieldPrefixLength = 3;

const QpackInstruction* QpackLiteralHeaderFieldInstruction();

// Description of language (set of instructions) used on request and push
// streams.
const QpackLanguage* QpackRequestStreamLanguage();

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_CONSTANTS_H_
