// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/huffman/hpack_huffman_encoder.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/http2/hpack/huffman/huffman_spec_tables.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace http2 {

namespace {

void FlushAccumulator(uint64_t accumulator, size_t to_write, char* dest) {
  const uint32_t word = static_cast<uint32_t>(accumulator >> 32);
  const uint32_t net_word = quiche::QuicheEndian::HostToNet32(word);
  std::memcpy(dest, &net_word, to_write);
}

}  // namespace

size_t HuffmanSize(absl::string_view plain) {
  uint64_t bits = 0;
  for (const uint8_t c : plain) {
    bits += HuffmanSpecTables::kCodeLengths[c];
  }
  uint64_t result = (bits + 7) / 8;
  if (result > std::numeric_limits<uint32_t>::max()) {
    return plain.size();
  }
  return result;
}

void HuffmanEncode(absl::string_view input, size_t encoded_size,
                   std::string* output) {
  // Uses the 64-bit accumulator path for inputs >= 25 bytes. For smaller
  // inputs, the overhead outweighs the benefits, causing a regression.
  if (input.size() >= 25 &&
      GetQuicheReloadableFlag(hpack_huffman_encoder_64bit_accumulator)) {
    QUICHE_RELOADABLE_FLAG_COUNT(hpack_huffman_encoder_64bit_accumulator);
    const size_t original_size = output->size();
    // The destination must be large enough to contain the original `output` as
    // well as the new encoded data.
    const size_t final_size = original_size + encoded_size;
    // Reserve an extra four bytes to avoid accessing unallocated memory (even
    // though it would only be OR'd with zeros and thus not modified).
    output->resize(final_size + sizeof(uint32_t), 0);

    char* dest = output->data() + original_size;
    // Maintains the bits to be written. The next code is shifted and OR'd
    // into the accumulator.
    uint64_t accumulator = 0;
    // Number of bits currently in the accumulator.
    int count = 0;

    for (const uint8_t c : input) {
      const uint32_t left_code = HuffmanSpecTables::kLeftCodes[c];
      const uint8_t len = HuffmanSpecTables::kCodeLengths[c];
      // Shifts the 32-bit left-aligned code to the right by the current bit
      // count and merges it into the 64-bit accumulator.
      accumulator |= (static_cast<uint64_t>(left_code) << 32) >> count;
      count += len;
      // When the accumulator has at least 32 bits, flushes them as a 32-bit
      // big-endian word.
      if (count >= 32) {
        FlushAccumulator(accumulator, sizeof(uint32_t), dest);
        dest += sizeof(uint32_t);
        accumulator <<= 32;
        count -= 32;
      }
    }

    // Writes the remaining bits (up to 31 bits).
    if (count > 0) {
      // HPACK requires end-of-stream padding to be 1s.
      accumulator |= (~0ULL >> count);
      const size_t remaining_bytes = (count + 7) / 8;
      FlushAccumulator(accumulator, remaining_bytes, dest);
    }

    output->resize(final_size);
    return;
  }

  const size_t original_size = output->size();
  // The destination must be large enough to contain the original `output` as
  // well as the new encoded data.
  const size_t final_size = original_size + encoded_size;
  // Reserve an extra four bytes to avoid accessing unallocated memory (even
  // though it would only be OR'd with zeros and thus not modified).
  output->resize(final_size + sizeof(uint32_t), 0);

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
