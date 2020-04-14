// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_ENDIAN_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_ENDIAN_IMPL_H_

#include "base/sys_byteorder.h"
#include "build/build_config.h"

namespace quiche {

class QuicheEndianImpl {
 public:
  // Convert |x| from host order (can be either little or big endian depending
  // on the platform) to network order (big endian).
  static uint16_t HostToNet16(uint16_t x) { return base::HostToNet16(x); }
  static uint32_t HostToNet32(uint32_t x) { return base::HostToNet32(x); }
  static uint64_t HostToNet64(uint64_t x) { return base::HostToNet64(x); }

  // Convert |x| from network order (big endian) to host order (can be either
  // little or big endian depending on the platform).
  static uint16_t NetToHost16(uint16_t x) { return base::NetToHost16(x); }
  static uint32_t NetToHost32(uint32_t x) { return base::NetToHost32(x); }
  static uint64_t NetToHost64(uint64_t x) { return base::NetToHost64(x); }

  // Returns true if current host order is little endian.
  static bool HostIsLittleEndian() {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
    return true;
#else
    return false;
#endif
  }
};

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_ENDIAN_IMPL_H_
