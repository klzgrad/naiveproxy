// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_PLATFORM_IMPL_SPDY_ENDIANNESS_UTIL_IMPL_H_
#define NET_SPDY_PLATFORM_IMPL_SPDY_ENDIANNESS_UTIL_IMPL_H_

#include "base/sys_byteorder.h"

inline uint16_t SpdyNetToHost16Impl(uint16_t x) {
  return base::NetToHost16(x);
}

inline uint32_t SpdyNetToHost32Impl(uint32_t x) {
  return base::NetToHost32(x);
}

inline uint64_t SpdyNetToHost64Impl(uint64_t x) {
  return base::NetToHost64(x);
}

inline uint16_t SpdyHostToNet16Impl(uint16_t x) {
  return base::HostToNet16(x);
}

inline uint32_t SpdyHostToNet32Impl(uint32_t x) {
  return base::HostToNet32(x);
}

inline uint64_t SpdyHostToNet64Impl(uint64_t x) {
  return base::HostToNet64(x);
}

#endif  // NET_SPDY_PLATFORM_IMPL_SPDY_ENDIANNESS_UTIL_IMPL_H_
