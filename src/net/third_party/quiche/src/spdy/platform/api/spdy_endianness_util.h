// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_ENDIANNESS_UTIL_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_ENDIANNESS_UTIL_H_

#include <stdint.h>

#include "net/spdy/platform/impl/spdy_endianness_util_impl.h"

namespace spdy {

// Converts the bytes in |x| from network to host order (endianness), and
// returns the result.
inline uint16_t SpdyNetToHost16(uint16_t x) {
  return SpdyNetToHost16Impl(x);
}

inline uint32_t SpdyNetToHost32(uint32_t x) {
  return SpdyNetToHost32Impl(x);
}

inline uint64_t SpdyNetToHost64(uint64_t x) {
  return SpdyNetToHost64Impl(x);
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline uint16_t SpdyHostToNet16(uint16_t x) {
  return SpdyHostToNet16Impl(x);
}

inline uint32_t SpdyHostToNet32(uint32_t x) {
  return SpdyHostToNet32Impl(x);
}

inline uint64_t SpdyHostToNet64(uint64_t x) {
  return SpdyHostToNet64Impl(x);
}

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_ENDIANNESS_UTIL_H_
