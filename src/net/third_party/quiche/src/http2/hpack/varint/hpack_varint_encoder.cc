// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_encoder.h"

#include <limits>

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

// static
void HpackVarintEncoder::Encode(uint8_t high_bits,
                                uint8_t prefix_length,
                                uint64_t varint,
                                std::string* output) {
  DCHECK_LE(1u, prefix_length);
  DCHECK_LE(prefix_length, 8u);

  // prefix_mask defines the sequence of low-order bits of the first byte
  // that encode the prefix of the value. It is also the marker in those bits
  // of the first byte indicating that at least one extension byte is needed.
  const uint8_t prefix_mask = (1 << prefix_length) - 1;
  DCHECK_EQ(0, high_bits & prefix_mask);

  if (varint < prefix_mask) {
    // The integer fits into the prefix in its entirety.
    unsigned char first_byte = high_bits | static_cast<unsigned char>(varint);
    output->push_back(first_byte);
    return;
  }

  // Extension bytes are needed.
  unsigned char first_byte = high_bits | prefix_mask;
  output->push_back(first_byte);

  varint -= prefix_mask;
  while (varint >= 128) {
    // Encode the next seven bits, with continuation bit set to one.
    output->push_back(0b10000000 | (varint % 128));
    varint >>= 7;
  }

  // Encode final seven bits, with continuation bit set to zero.
  output->push_back(varint);
}

}  // namespace http2
