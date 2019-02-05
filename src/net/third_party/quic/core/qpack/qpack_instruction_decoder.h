// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_DECODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_DECODER_H_

#include <cstddef>

#include "net/third_party/http2/hpack/huffman/hpack_huffman_decoder.h"
#include "net/third_party/http2/hpack/varint/hpack_varint_decoder.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// Generic instruction decoder class.  Takes a QpackLanguage that describes a
// language, that is, a set of instruction opcodes together with a list of
// fields that follow each instruction.
class QUIC_EXPORT_PRIVATE QpackInstructionDecoder {
 public:
  // Delegate is notified each time an instruction is decoded or when an error
  // occurs.
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when an instruction (including all its fields) is decoded.
    // |instruction| points to an entry in |language|.
    // Returns true if decoded fields are valid.
    // Returns false otherwise, in which case QpackInstructionDecoder stops
    // decoding: Delegate methods will not be called, and Decode() must not be
    // called.
    virtual bool OnInstructionDecoded(const QpackInstruction* instruction) = 0;

    // Called by QpackInstructionDecoder if an error has occurred.
    // No more data is processed afterwards.
    virtual void OnError(QuicStringPiece error_message) = 0;
  };

  // Both |*language| and |*delegate| must outlive this object.
  QpackInstructionDecoder(const QpackLanguage* language, Delegate* delegate);
  QpackInstructionDecoder() = delete;
  QpackInstructionDecoder(const QpackInstructionDecoder&) = delete;
  QpackInstructionDecoder& operator=(const QpackInstructionDecoder&) = delete;

  // Provide a data fragment to decode.  Must not be called after an error has
  // occurred.  Must not be called with empty |data|.
  void Decode(QuicStringPiece data);

  // Returns true if no decoding has taken place yet or if the last instruction
  // has been entirely parsed.
  bool AtInstructionBoundary() const;

  // Accessors for decoded values.  Should only be called for fields that are
  // part of the most recently decoded instruction, and only after |this| calls
  // Delegate::OnInstructionDecoded() but before Decode() is called again.
  bool is_static() const { return is_static_; }
  size_t varint() const { return varint_; }
  const QuicString& name() const { return name_; }
  const QuicString& value() const { return value_; }

 private:
  enum class State {
    // Identify instruction.
    kStartInstruction,
    // Start decoding next field.
    kStartField,
    // Read a single bit.
    kReadBit,
    // Start reading integer.
    kVarintStart,
    // Resume reading integer.
    kVarintResume,
    // Done reading integer.
    kVarintDone,
    // Read string.
    kReadString,
    // Done reading string.
    kReadStringDone
  };

  // One method for each state.  Some take input data and return the number of
  // octets processed.  Some take input data but do have void return type
  // because they not consume any bytes.  Some do not take any arguments because
  // they only change internal state.
  void DoStartInstruction(QuicStringPiece data);
  void DoStartField();
  void DoReadBit(QuicStringPiece data);
  size_t DoVarintStart(QuicStringPiece data);
  size_t DoVarintResume(QuicStringPiece data);
  void DoVarintDone();
  size_t DoReadString(QuicStringPiece data);
  void DoReadStringDone();

  // Identify instruction based on opcode encoded in |byte|.
  // Returns a pointer to an element of |*language_|.
  const QpackInstruction* LookupOpcode(uint8_t byte) const;

  // Stops decoding and calls Delegate::OnError().
  void OnError(QuicStringPiece error_message);

  // Describes the language used for decoding.
  const QpackLanguage* const language_;

  // The Delegate to notify of decoded instructions and errors.
  Delegate* const delegate_;

  // Storage for decoded field values.
  bool is_static_;
  size_t varint_;
  QuicString name_;
  QuicString value_;
  // Whether the currently decoded header name or value is Huffman encoded.
  bool is_huffman_encoded_;
  // Length of string being read into |name_| or |value_|.
  size_t string_length_;

  // Decoder instance for decoding integers.
  http2::HpackVarintDecoder varint_decoder_;

  // Decoder instance for decoding Huffman encoded strings.
  http2::HpackHuffmanDecoder huffman_decoder_;

  // True if a decoding error has been detected either by
  // QpackInstructionDecoder or by Delegate.
  bool error_detected_;

  // Decoding state.
  State state_;

  // Instruction currently being decoded.
  const QpackInstruction* instruction_;

  // Field currently being decoded.
  QpackInstructionFields::const_iterator field_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_INSTRUCTION_DECODER_H_
