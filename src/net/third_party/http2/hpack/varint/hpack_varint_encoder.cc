// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/varint/hpack_varint_encoder.h"

#include "base/logging.h"

namespace http2 {

HpackVarintEncoder::HpackVarintEncoder()
    : varint_(0), encoding_in_progress_(false) {}

unsigned char HpackVarintEncoder::StartEncoding(uint8_t high_bits,
                                                uint8_t prefix_length,
                                                uint64_t varint) {
  DCHECK(!encoding_in_progress_);
  DCHECK_EQ(0u, varint_);
  DCHECK_LE(1u, prefix_length);
  DCHECK_LE(prefix_length, 7u);

  // prefix_mask defines the sequence of low-order bits of the first byte
  // that encode the prefix of the value. It is also the marker in those bits
  // of the first byte indicating that at least one extension byte is needed.
  const uint8_t prefix_mask = (1 << prefix_length) - 1;
  DCHECK_EQ(0, high_bits & prefix_mask);

  if (varint < prefix_mask) {
    // The integer fits into the prefix in its entirety.
    return high_bits | static_cast<unsigned char>(varint);
  }

  // We need extension bytes.
  varint_ = varint - prefix_mask;
  encoding_in_progress_ = true;
  return high_bits | prefix_mask;
}

size_t HpackVarintEncoder::ResumeEncoding(size_t max_encoded_bytes,
                                          Http2String* output) {
  DCHECK(encoding_in_progress_);
  DCHECK_NE(0u, max_encoded_bytes);

  size_t encoded_bytes = 0;
  while (encoded_bytes < max_encoded_bytes) {
    ++encoded_bytes;
    if (varint_ < 128) {
      // Encode final seven bits, with continuation bit set to zero.
      output->push_back(varint_);
      varint_ = 0;
      encoding_in_progress_ = false;
      break;
    }
    // Encode the next seven bits, with continuation bit set to one.
    output->push_back(0b10000000 | (varint_ % 128));
    varint_ >>= 7;
  }
  return encoded_bytes;
}

bool HpackVarintEncoder::IsEncodingInProgress() const {
  return encoding_in_progress_;
}

}  // namespace http2
