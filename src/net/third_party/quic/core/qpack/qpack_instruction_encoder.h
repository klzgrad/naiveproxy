// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_encoder.h"

namespace quic {

// Generic instruction encoder class.  Takes a QpackLanguage that describes a
// language, that is, a set of instruction opcodes together with a list of
// fields that follow each instruction.
class QUIC_EXPORT_PRIVATE QpackInstructionEncoder {
 public:
  QpackInstructionEncoder();
  QpackInstructionEncoder(const QpackInstructionEncoder&) = delete;
  QpackInstructionEncoder& operator=(const QpackInstructionEncoder&) = delete;

  // Setters for values to be encoded.
  // |name| and |value| must remain valid until the instruction is encoded.
  void set_s_bit(bool s_bit) { s_bit_ = s_bit; }
  void set_varint(uint64_t varint) { varint_ = varint; }
  void set_varint2(uint64_t varint2) { varint2_ = varint2; }
  void set_name(QuicStringPiece name) { name_ = name; }
  void set_value(QuicStringPiece value) { value_ = value; }

  // Start encoding an instruction.  Must only be called after the previous
  // instruction has been completely encoded.
  void Encode(const QpackInstruction* instruction);

  // Returns true iff more data remains to be encoded for the current
  // instruction.  Returns false if there is no current instruction, that is, if
  // Encode() has never been called.
  bool HasNext() const;

  // Encodes the next up to |max_encoded_bytes| octets of the current
  // instruction, appending to |output|.  Must only be called when HasNext()
  // returns true.  |max_encoded_bytes| must be positive.
  void Next(size_t max_encoded_bytes, QuicString* output);

 private:
  enum class State {
    // Write instruction opcode to |byte_|.
    kOpcode,
    // Select state based on type of current field.
    kStartField,
    // Write static bit to |byte_|.
    kSbit,
    // Start encoding an integer (|varint_| or |varint2_| or string length) with
    // a prefix, using |byte_| for the high bits.
    kVarintStart,
    // Resume encoding an integer.
    kVarintResume,
    // Determine if Huffman encoding should be used for |name_| or |value_|, set
    // up |name_| or |value_| and |huffman_encoded_string_| accordingly, and
    // write the Huffman bit to |byte_|.
    kStartString,
    // Write string.
    kWriteString
  };

  // One method for each state.  Some encode up to |max_encoded_bytes| octets,
  // appending to |output|.  Some only change internal state.
  void DoOpcode();
  void DoStartField();
  void DoStaticBit();
  size_t DoVarintStart(size_t max_encoded_bytes, QuicString* output);
  size_t DoVarintResume(size_t max_encoded_bytes, QuicString* output);
  void DoStartString();
  size_t DoWriteString(size_t max_encoded_bytes, QuicString* output);

  // Storage for field values to be encoded.
  bool s_bit_;
  uint64_t varint_;
  uint64_t varint2_;
  // The caller must keep the string that |name_| and |value_| point to
  // valid until they are encoded.
  QuicStringPiece name_;
  QuicStringPiece value_;

  // Storage for the Huffman encoded string literal to be written if Huffman
  // encoding is used.
  QuicString huffman_encoded_string_;

  // If Huffman encoding is used, points to a substring of
  // |huffman_encoded_string_|.
  // Otherwise points to a substring of |name_| or |value_|.
  QuicStringPiece string_to_write_;

  // Storage for a single byte that contains multiple fields, that is, multiple
  // states are writing it.
  uint8_t byte_;

  // Encoding state.
  State state_;

  // Instruction currently being decoded.
  const QpackInstruction* instruction_;

  // Field currently being decoded.
  QpackInstructionFields::const_iterator field_;

  // Decoder instance for decoding integers.
  http2::HpackVarintEncoder varint_encoder_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_
