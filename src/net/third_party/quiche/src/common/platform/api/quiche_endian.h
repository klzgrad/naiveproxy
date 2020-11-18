// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace quiche {

enum Endianness {
  NETWORK_BYTE_ORDER,  // big endian
  HOST_BYTE_ORDER      // little endian
};

// Provide utility functions that convert from/to network order (big endian)
// to/from host order (can be either little or big endian depending on the
// platform).
class QUICHE_EXPORT_PRIVATE QuicheEndian {
 public:
  // Convert |x| from host order (little endian) to network order (big endian).
#if defined(__clang__) || \
    (defined(__GNUC__) && \
     ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ >= 5))
  static uint16_t HostToNet16(uint16_t x) { return __builtin_bswap16(x); }
  static uint32_t HostToNet32(uint32_t x) { return __builtin_bswap32(x); }
  static uint64_t HostToNet64(uint64_t x) { return __builtin_bswap64(x); }
#else
  static uint16_t HostToNet16(uint16_t x) { return PortableByteSwap(x); }
  static uint32_t HostToNet32(uint32_t x) { return PortableByteSwap(x); }
  static uint64_t HostToNet64(uint64_t x) { return PortableByteSwap(x); }
#endif

  // Convert |x| from network order (big endian) to host order (little endian).
  static uint16_t NetToHost16(uint16_t x) { return HostToNet16(x); }
  static uint32_t NetToHost32(uint32_t x) { return HostToNet32(x); }
  static uint64_t NetToHost64(uint64_t x) { return HostToNet64(x); }

  // Returns true if current host order is little endian.
  static bool HostIsLittleEndian() { return true; }

  // Left public for tests.
  template <typename T>
  static T PortableByteSwap(T input) {
    static_assert(std::is_unsigned<T>::value, "T has to be uintNN_t");
    union {
      T number;
      char bytes[sizeof(T)];
    } value;
    value.number = input;
    std::reverse(std::begin(value.bytes), std::end(value.bytes));
    return value.number;
  }
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_
