// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/huffman/hpack_huffman_encoder.h"

#include <string>

#include "quiche/http2/hpack/huffman/huffman_spec_tables.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

size_t HuffmanSize(absl::string_view plain) {
  size_t bits = 0;
  for (const uint8_t c : plain) {
    bits += HuffmanSpecTables::kCodeLengths[c];
  }
  return (bits + 7) / 8;
}

void HuffmanEncode(absl::string_view plain, size_t encoded_size,
                   std::string* huffman) {
  QUICHE_DCHECK(huffman != nullptr);
  huffman->reserve(huffman->size() + encoded_size);
  uint64_t bit_buffer = 0;  // High-bit is next bit to output. Not clear if that
                            // is more performant than having the low-bit be the
                            // last to be output.
  size_t bits_unused = 64;  // Number of bits available for the next code.
  for (uint8_t c : plain) {
    size_t code_length = HuffmanSpecTables::kCodeLengths[c];
    if (bits_unused < code_length) {
      // There isn't enough room in bit_buffer for the code of c.
      // Flush until bits_unused > 56 (i.e. 64 - 8).
      do {
        char h = static_cast<char>(bit_buffer >> 56);
        bit_buffer <<= 8;
        bits_unused += 8;
        // Perhaps would be more efficient if we populated an array of chars,
        // so we don't have to call push_back each time. Reconsider if used
        // for production.
        huffman->push_back(h);
      } while (bits_unused <= 56);
    }
    uint64_t code = HuffmanSpecTables::kRightCodes[c];
    size_t shift_by = bits_unused - code_length;
    bit_buffer |= (code << shift_by);
    bits_unused -= code_length;
  }
  // bit_buffer contains (64-bits_unused) bits that still need to be flushed.
  // Output whole bytes until we don't have any whole bytes left.
  size_t bits_used = 64 - bits_unused;
  while (bits_used >= 8) {
    char h = static_cast<char>(bit_buffer >> 56);
    bit_buffer <<= 8;
    bits_used -= 8;
    huffman->push_back(h);
  }
  if (bits_used > 0) {
    // We have less than a byte left to output. The spec calls for padding out
    // the final byte with the leading bits of the EOS symbol (30 1-bits).
    constexpr uint64_t leading_eos_bits = 0b11111111;
    bit_buffer |= (leading_eos_bits << (56 - bits_used));
    char h = static_cast<char>(bit_buffer >> 56);
    huffman->push_back(h);
  }
}

void HuffmanEncodeFast(absl::string_view input, size_t encoded_size,
                       std::string* output) {
  const size_t original_size = output->size();
  const size_t final_size = original_size + encoded_size;
  // Reserve an extra four bytes to avoid accessing unallocated memory (even
  // though it would only be OR'd with zeros and thus not modified).
  output->resize(final_size + 4, 0);

  // Pointer to first appended byte.
  char* const first = &*output->begin() + original_size;
  size_t bit_counter = 0;
  for (uint8_t c : input) {
    // Align the Huffman code to byte boundaries as it needs to be written.
    // The longest Huffman code is 30 bits long, and it can be shifted by up to
    // 7 bits, requiring 37 bits in total.  The most significant 25 bits and
    // least significant 2 bits of |code| are always zero.
    uint64_t code = static_cast<uint64_t>(HuffmanSpecTables::kLeftCodes[c])
                    << (8 - (bit_counter % 8));
    // The byte where the first bit of |code| needs to be written.
    char* const current = first + (bit_counter / 8);

    bit_counter += HuffmanSpecTables::kCodeLengths[c];

    *current |= code >> 32;

    // Do not check if this write is zero before executing it, because with
    // uniformly random shifts and an ideal random input distribution
    // corresponding to the Huffman tree it would only be zero in 29% of the
    // cases.
    *(current + 1) |= (code >> 24) & 0xff;

    // Continue to next input character if there is nothing else to write.
    // (If next byte is zero, then rest must also be zero.)
    if ((code & 0xff0000) == 0) {
      continue;
    }
    *(current + 2) |= (code >> 16) & 0xff;

    // Continue to next input character if there is nothing else to write.
    // (If next byte is zero, then rest must also be zero.)
    if ((code & 0xff00) == 0) {
      continue;
    }
    *(current + 3) |= (code >> 8) & 0xff;

    // Do not check if this write is zero, because the check would probably be
    // as expensive as the write.
    *(current + 4) |= code & 0xff;
  }

  QUICHE_DCHECK_EQ(encoded_size, (bit_counter + 7) / 8);

  // EOF
  if (bit_counter % 8 != 0) {
    *(first + encoded_size - 1) |= 0xff >> (bit_counter & 7);
  }

  output->resize(final_size);
}

}  // namespace http2
