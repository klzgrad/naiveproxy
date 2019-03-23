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
  // A single bit indicating whether the index refers to the static table, or
  // indicating the sign of Delta Base Index.  Called "S" bit because both
  // "static" and "sign" start with the letter "S".
  kSbit,
  // An integer encoded with variable length encoding.  This could be an index,
  // stream ID, maximum size, or Largest Reference.
  kVarint,
  // A second integer encoded with variable length encoding.  This could be
  // Delta Base Index.
  kVarint2,
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
  // For a kSbit field, |param| is a mask with exactly one bit set.
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
// Every possible input must match exactly one instruction.
using QpackLanguage = std::vector<const QpackInstruction*>;

// TODO(bnc): Move this into HpackVarintEncoder.
// The integer encoder can encode up to 2^64-1, which can take up to 10 bytes
// (each carrying 7 bits) after the prefix.
const uint8_t kMaxExtensionBytesForVarintEncoding = 10;

// Wire format defined in
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5

// 5.2 Encoder stream instructions

// 5.2.1 Insert With Name Reference
const QpackInstruction* InsertWithNameReferenceInstruction();

// 5.2.2 Insert Without Name Reference
const QpackInstruction* InsertWithoutNameReferenceInstruction();

// 5.2.3 Duplicate
const QpackInstruction* DuplicateInstruction();

// 5.2.4 Dynamic Table Size Update
const QpackInstruction* DynamicTableSizeUpdateInstruction();

// Encoder stream language.
const QpackLanguage* QpackEncoderStreamLanguage();

// 5.3 Decoder stream instructions

// 5.3.1 Table State Synchronize
const QpackInstruction* TableStateSynchronizeInstruction();

// 5.3.2 Header Acknowledgement
const QpackInstruction* HeaderAcknowledgementInstruction();

// 5.3.3 Stream Cancellation
const QpackInstruction* StreamCancellationInstruction();

// Decoder stream language.
const QpackLanguage* QpackDecoderStreamLanguage();

// 5.4.1. Header data prefix instructions

const QpackInstruction* QpackPrefixInstruction();

const QpackLanguage* QpackPrefixLanguage();

// 5.4.2. Request and push stream instructions

// 5.4.2.1. Indexed Header Field
const QpackInstruction* QpackIndexedHeaderFieldInstruction();

// 5.4.2.2. Indexed Header Field With Post-Base Index
const QpackInstruction* QpackIndexedHeaderFieldPostBaseInstruction();

// 5.4.2.3. Literal Header Field With Name Reference
const QpackInstruction* QpackLiteralHeaderFieldNameReferenceInstruction();

// 5.4.2.4. Literal Header Field With Post-Base Name Reference
const QpackInstruction* QpackLiteralHeaderFieldPostBaseInstruction();

// 5.4.2.5. Literal Header Field Without Name Reference
const QpackInstruction* QpackLiteralHeaderFieldInstruction();

// Request and push stream language.
const QpackLanguage* QpackRequestStreamLanguage();

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_CONSTANTS_H_
