// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/quiche/common/platform/impl/quiche_endian_impl.h"

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
  // Convert |x| from host order (can be either little or big endian depending
  // on the platform) to network order (big endian).
  static uint16_t HostToNet16(uint16_t x) {
    return QuicheEndianImpl::HostToNet16(x);
  }
  static uint32_t HostToNet32(uint32_t x) {
    return QuicheEndianImpl::HostToNet32(x);
  }
  static uint64_t HostToNet64(uint64_t x) {
    return QuicheEndianImpl::HostToNet64(x);
  }

  // Convert |x| from network order (big endian) to host order (can be either
  // little or big endian depending on the platform).
  static uint16_t NetToHost16(uint16_t x) {
    return QuicheEndianImpl::NetToHost16(x);
  }
  static uint32_t NetToHost32(uint32_t x) {
    return QuicheEndianImpl::NetToHost32(x);
  }
  static uint64_t NetToHost64(uint64_t x) {
    return QuicheEndianImpl::NetToHost64(x);
  }

  // Returns true if current host order is little endian.
  static bool HostIsLittleEndian() {
    return QuicheEndianImpl::HostIsLittleEndian();
  }
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_ENDIAN_H_
