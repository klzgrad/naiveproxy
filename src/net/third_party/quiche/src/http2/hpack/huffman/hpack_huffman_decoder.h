// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_DECODER_H_
#define QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_DECODER_H_

// HpackHuffmanDecoder is an incremental decoder of strings that have been
// encoded using the Huffman table defined in the HPACK spec.
// By incremental, we mean that the HpackHuffmanDecoder::Decode method does
// not require the entire string to be provided, and can instead decode the
// string as fragments of it become available (e.g. as HPACK block fragments
// are received for decoding by HpackEntryDecoder).

#include <stddef.h>

#include <cstdint>
#include <iosfwd>
#include <string>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {

// HuffmanAccumulator is used to store bits during decoding, e.g. next N bits
// that have not yet been decoded, but have been extracted from the encoded
// string).  An advantage of using a uint64 for the accumulator
// is that it has room for the bits of the longest code plus the bits of a full
// byte; that means that when adding more bits to the accumulator, it can always
// be done in whole bytes. For example, if we currently have 26 bits in the
// accumulator, and need more to decode the current symbol, we can add a whole
// byte to the accumulator, and not have to do juggling with adding 6 bits (to
// reach 30), and then keep track of the last two bits we've not been able to
// add to the accumulator.
typedef uint64_t HuffmanAccumulator;
typedef size_t HuffmanAccumulatorBitCount;

// HuffmanBitBuffer stores the leading edge of bits to be decoded. The high
// order bit of accumulator_ is the next bit to be decoded.
class QUICHE_EXPORT_PRIVATE HuffmanBitBuffer {
 public:
  HuffmanBitBuffer();

  // Prepare for decoding a new Huffman encoded string.
  void Reset();

  // Add as many whole bytes to the accumulator (accumulator_) as possible,
  // returning the number of bytes added.
  size_t AppendBytes(quiche::QuicheStringPiece input);

  // Get the bits of the accumulator.
  HuffmanAccumulator value() const { return accumulator_; }

  // Number of bits of the encoded string that are in the accumulator
  // (accumulator_).
  HuffmanAccumulatorBitCount count() const { return count_; }

  // Are there no bits in the accumulator?
  bool IsEmpty() const { return count_ == 0; }

  // Number of additional bits that can be added to the accumulator.
  HuffmanAccumulatorBitCount free_count() const;

  // Consume the leading |code_length| bits of the accumulator.
  void ConsumeBits(HuffmanAccumulatorBitCount code_length);

  // Are the contents valid for the end of a Huffman encoded string? The RFC
  // states that EOS (end-of-string) symbol must not be explicitly encoded in
  // the bit stream, but any unused bits in the final byte must be set to the
  // prefix of the EOS symbol, which is all 1 bits. So there can be at most 7
  // such bits.
  // Returns true if the bit buffer is empty, or contains at most 7 bits, all
  // of them 1. Otherwise returns false.
  bool InputProperlyTerminated() const;

  std::string DebugString() const;

 private:
  HuffmanAccumulator accumulator_;
  HuffmanAccumulatorBitCount count_;
};

inline std::ostream& operator<<(std::ostream& out, const HuffmanBitBuffer& v) {
  return out << v.DebugString();
}

class QUICHE_EXPORT_PRIVATE HpackHuffmanDecoder {
 public:
  HpackHuffmanDecoder();
  ~HpackHuffmanDecoder();

  // Prepare for decoding a new Huffman encoded string.
  void Reset() { bit_buffer_.Reset(); }

  // Decode the portion of a HPACK Huffman encoded string that is in |input|,
  // appending the decoded symbols into |*output|, stopping when more bits are
  // needed to determine the next symbol, which/ means that the input has been
  // drained, and also that the bit_buffer_ is empty or that the bits that are
  // in it are not a whole symbol.
  // If |input| is the start of a string, the caller must first call Reset.
  // If |input| includes the end of the encoded string, the caller must call
  // InputProperlyTerminated after Decode has returned true in order to
  // determine if the encoded string was properly terminated.
  // Returns false if something went wrong (e.g. the encoding contains the code
  // EOS symbol). Otherwise returns true, in which case input has been fully
  // decoded or buffered; in particular, if the low-order bit of the final byte
  // of the input is not the last bit of an encoded symbol, then bit_buffer_
  // will contain the leading bits of the code for that symbol, but not the
  // final bits of that code.
  // Note that output should be empty, but that it is not cleared by Decode().
  bool Decode(quiche::QuicheStringPiece input, std::string* output);

  // Is what remains in the bit_buffer_ valid at the end of an encoded string?
  // Call after passing the the final portion of a Huffman string to Decode,
  // and getting true as the result.
  bool InputProperlyTerminated() const {
    return bit_buffer_.InputProperlyTerminated();
  }

  std::string DebugString() const;

 private:
  HuffmanBitBuffer bit_buffer_;
};

inline std::ostream& operator<<(std::ostream& out,
                                const HpackHuffmanDecoder& v) {
  return out << v.DebugString();
}

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_DECODER_H_
