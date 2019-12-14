// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_
#define QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"

namespace http2 {

// HPACK integer encoder class with single static method implementing variable
// length integer representation defined in RFC7541, Section 5.1:
// https://httpwg.org/specs/rfc7541.html#integer.representation
class HTTP2_EXPORT_PRIVATE HpackVarintEncoder {
 public:
  // Encode |varint|, appending encoded data to |*output|.
  // Appends between 1 and 11 bytes in total.
  static void Encode(uint8_t high_bits,
                     uint8_t prefix_length,
                     uint64_t varint,
                     std::string* output);
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_
