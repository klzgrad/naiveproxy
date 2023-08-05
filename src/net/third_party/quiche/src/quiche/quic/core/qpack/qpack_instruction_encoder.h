// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_instructions.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Generic instruction encoder class.  Takes a QpackLanguage that describes a
// language, that is, a set of instruction opcodes together with a list of
// fields that follow each instruction.
class QUIC_EXPORT_PRIVATE QpackInstructionEncoder {
 public:
  QpackInstructionEncoder();
  QpackInstructionEncoder(const QpackInstructionEncoder&) = delete;
  QpackInstructionEncoder& operator=(const QpackInstructionEncoder&) = delete;

  // Append encoded instruction to |output|.
  void Encode(const QpackInstructionWithValues& instruction_with_values,
              std::string* output);

 private:
  enum class State {
    // Write instruction opcode to |byte_|.
    kOpcode,
    // Select state based on type of current field.
    kStartField,
    // Write static bit to |byte_|.
    kSbit,
    // Encode an integer (|varint_| or |varint2_| or string length) with a
    // prefix, using |byte_| for the high bits.
    kVarintEncode,
    // Determine if Huffman encoding should be used for the header name or
    // value, set |use_huffman_| and |string_length_| appropriately, write the
    // Huffman bit to |byte_|.
    kStartString,
    // Write header name or value, performing Huffman encoding if |use_huffman_|
    // is true.
    kWriteString
  };

  // One method for each state.  Some append encoded bytes to |output|.
  // Some only change internal state.
  void DoOpcode();
  void DoStartField();
  void DoSBit(bool s_bit);
  void DoVarintEncode(uint64_t varint, uint64_t varint2, std::string* output);
  void DoStartString(absl::string_view name, absl::string_view value);
  void DoWriteString(absl::string_view name, absl::string_view value,
                     std::string* output);

  // True if name or value should be Huffman encoded.
  bool use_huffman_;

  // Length of name or value string to be written.
  // If |use_huffman_| is true, length is after Huffman encoding.
  size_t string_length_;

  // Storage for a single byte that contains multiple fields, that is, multiple
  // states are writing it.
  uint8_t byte_;

  // Encoding state.
  State state_;

  // Instruction currently being decoded.
  const QpackInstruction* instruction_;

  // Field currently being decoded.
  QpackInstructionFields::const_iterator field_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_INSTRUCTION_ENCODER_H_
