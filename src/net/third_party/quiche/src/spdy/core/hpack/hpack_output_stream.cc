// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"

#include <utility>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {

HpackOutputStream::HpackOutputStream() : bit_offset_(0) {}

HpackOutputStream::~HpackOutputStream() = default;

void HpackOutputStream::AppendBits(uint8_t bits, size_t bit_size) {
  DCHECK_GT(bit_size, 0u);
  DCHECK_LE(bit_size, 8u);
  DCHECK_EQ(bits >> bit_size, 0);
  size_t new_bit_offset = bit_offset_ + bit_size;
  if (bit_offset_ == 0) {
    // Buffer ends on a byte boundary.
    DCHECK_LE(bit_size, 8u);
    buffer_.append(1, bits << (8 - bit_size));
  } else if (new_bit_offset <= 8) {
    // Buffer does not end on a byte boundary but the given bits fit
    // in the remainder of the last byte.
    buffer_.back() |= bits << (8 - new_bit_offset);
  } else {
    // Buffer does not end on a byte boundary and the given bits do
    // not fit in the remainder of the last byte.
    buffer_.back() |= bits >> (new_bit_offset - 8);
    buffer_.append(1, bits << (16 - new_bit_offset));
  }
  bit_offset_ = new_bit_offset % 8;
}

void HpackOutputStream::AppendPrefix(HpackPrefix prefix) {
  AppendBits(prefix.bits, prefix.bit_size);
}

void HpackOutputStream::AppendBytes(SpdyStringPiece buffer) {
  DCHECK_EQ(bit_offset_, 0u);
  buffer_.append(buffer.data(), buffer.size());
}

void HpackOutputStream::AppendUint32(uint32_t I) {
  // The algorithm below is adapted from the pseudocode in 6.1.
  size_t N = 8 - bit_offset_;
  uint8_t max_first_byte = static_cast<uint8_t>((1 << N) - 1);
  if (I < max_first_byte) {
    AppendBits(static_cast<uint8_t>(I), N);
  } else {
    AppendBits(max_first_byte, N);
    I -= max_first_byte;
    while ((I & ~0x7f) != 0) {
      buffer_.append(1, (I & 0x7f) | 0x80);
      I >>= 7;
    }
    AppendBits(static_cast<uint8_t>(I), 8);
  }
}

void HpackOutputStream::TakeString(std::string* output) {
  // This must hold, since all public functions cause the buffer to
  // end on a byte boundary.
  DCHECK_EQ(bit_offset_, 0u);
  buffer_.swap(*output);
  buffer_.clear();
  bit_offset_ = 0;
}

void HpackOutputStream::BoundedTakeString(size_t max_size,
                                          std::string* output) {
  if (buffer_.size() > max_size) {
    // Save off overflow bytes to temporary string (causes a copy).
    std::string overflow(buffer_.data() + max_size, buffer_.size() - max_size);

    // Resize buffer down to the given limit.
    buffer_.resize(max_size);

    // Give buffer to output string.
    *output = std::move(buffer_);

    // Reset to contain overflow.
    buffer_ = std::move(overflow);
  } else {
    TakeString(output);
  }
}

size_t HpackOutputStream::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(buffer_);
}

}  // namespace spdy
