// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTIONS_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTIONS_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace test {
class QpackInstructionWithValuesPeer;
}  // namespace test

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
  // indicating the sign of Delta Base.  Called "S" bit because both "static"
  // and "sign" start with the letter "S".
  kSbit,
  // An integer encoded with variable length encoding.  This could be an index,
  // stream ID, maximum size, or Encoded Required Insert Count.
  kVarint,
  // A second integer encoded with variable length encoding.  This could be
  // Delta Base.
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
const QpackInstruction* SetDynamicTableCapacityInstruction();

// Encoder stream language.
const QpackLanguage* QpackEncoderStreamLanguage();

// 5.3 Decoder stream instructions

// 5.3.1 Insert Count Increment
const QpackInstruction* InsertCountIncrementInstruction();

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

// Storage for instruction and field values to be encoded.
// This class can only be instantiated using factory methods that take exactly
// the arguments that the corresponding instruction needs.
class QUIC_EXPORT_PRIVATE QpackInstructionWithValues {
 public:
  // 5.2 Encoder stream instructions
  static QpackInstructionWithValues InsertWithNameReference(
      bool is_static,
      uint64_t name_index,
      quiche::QuicheStringPiece value);
  static QpackInstructionWithValues InsertWithoutNameReference(
      quiche::QuicheStringPiece name,
      quiche::QuicheStringPiece value);
  static QpackInstructionWithValues Duplicate(uint64_t index);
  static QpackInstructionWithValues SetDynamicTableCapacity(uint64_t capacity);

  // 5.3 Decoder stream instructions
  static QpackInstructionWithValues InsertCountIncrement(uint64_t increment);
  static QpackInstructionWithValues HeaderAcknowledgement(uint64_t stream_id);
  static QpackInstructionWithValues StreamCancellation(uint64_t stream_id);

  // 5.4.1. Header data prefix.  Delta Base is hardcoded to be zero.
  static QpackInstructionWithValues Prefix(uint64_t required_insert_count);

  // 5.4.2. Request and push stream instructions
  static QpackInstructionWithValues IndexedHeaderField(bool is_static,
                                                       uint64_t index);
  static QpackInstructionWithValues LiteralHeaderFieldNameReference(
      bool is_static,
      uint64_t index,
      quiche::QuicheStringPiece value);
  static QpackInstructionWithValues LiteralHeaderField(
      quiche::QuicheStringPiece name,
      quiche::QuicheStringPiece value);

  const QpackInstruction* instruction() const { return instruction_; }
  bool s_bit() const { return s_bit_; }
  uint64_t varint() const { return varint_; }
  uint64_t varint2() const { return varint2_; }
  quiche::QuicheStringPiece name() const { return name_; }
  quiche::QuicheStringPiece value() const { return value_; }

  // Used by QpackEncoder, because in the first pass it stores absolute indices,
  // which are converted into relative indices in the second pass after base is
  // determined.
  void set_varint(uint64_t varint) { varint_ = varint; }

 private:
  friend test::QpackInstructionWithValuesPeer;

  QpackInstructionWithValues() = default;

  // |*instruction| is not owned.
  const QpackInstruction* instruction_;
  bool s_bit_;
  uint64_t varint_;
  uint64_t varint2_;
  quiche::QuicheStringPiece name_;
  quiche::QuicheStringPiece value_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTIONS_H_
