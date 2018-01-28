// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines some bit utilities.

#ifndef BASE_BITS_H_
#define BASE_BITS_H_

#include <stddef.h>
#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/logging.h"

#if defined(COMPILER_MSVC)
#include <intrin.h>
#endif

namespace base {
namespace bits {

// Returns the integer i such as 2^i <= n < 2^(i+1)
inline int Log2Floor(uint32_t n) {
  if (n == 0)
    return -1;
  int log = 0;
  uint32_t value = n;
  for (int i = 4; i >= 0; --i) {
    int shift = (1 << i);
    uint32_t x = value >> shift;
    if (x != 0) {
      value = x;
      log += shift;
    }
  }
  DCHECK_EQ(value, 1u);
  return log;
}

// Returns the integer i such as 2^(i-1) < n <= 2^i
inline int Log2Ceiling(uint32_t n) {
  if (n == 0) {
    return -1;
  } else {
    // Log2Floor returns -1 for 0, so the following works correctly for n=1.
    return 1 + Log2Floor(n - 1);
  }
}

// Round up |size| to a multiple of alignment, which must be a power of two.
inline size_t Align(size_t size, size_t alignment) {
  DCHECK_EQ(alignment & (alignment - 1), 0u);
  return (size + alignment - 1) & ~(alignment - 1);
}

// These functions count the number of leading zeros in a binary value, starting
// with the most significant bit. C does not have an operator to do this, but
// fortunately the various compilers have built-ins that map to fast underlying
// processor instructions.
#if defined(COMPILER_MSVC)

ALWAYS_INLINE uint32_t CountLeadingZeroBits32(uint32_t x) {
  unsigned long index;
  return LIKELY(_BitScanReverse(&index, x)) ? (31 - index) : 32;
}

#if defined(ARCH_CPU_64_BITS)

// MSVC only supplies _BitScanForward64 when building for a 64-bit target.
ALWAYS_INLINE uint64_t CountLeadingZeroBits64(uint64_t x) {
  unsigned long index;
  return LIKELY(_BitScanReverse64(&index, x)) ? (63 - index) : 64;
}

#endif

#elif defined(COMPILER_GCC)

// This is very annoying. __builtin_clz has undefined behaviour for an input of
// 0, even though there's clearly a return value that makes sense, and even
// though some processor clz instructions have defined behaviour for 0. We could
// drop to raw __asm__ to do better, but we'll avoid doing that unless we see
// proof that we need to.
ALWAYS_INLINE uint32_t CountLeadingZeroBits32(uint32_t x) {
  return LIKELY(x) ? __builtin_clz(x) : 32;
}

ALWAYS_INLINE uint64_t CountLeadingZeroBits64(uint64_t x) {
  return LIKELY(x) ? __builtin_clzll(x) : 64;
}

#endif

#if defined(ARCH_CPU_64_BITS)

ALWAYS_INLINE size_t CountLeadingZeroBitsSizeT(size_t x) {
  return CountLeadingZeroBits64(x);
}

#else

ALWAYS_INLINE size_t CountLeadingZeroBitsSizeT(size_t x) {
  return CountLeadingZeroBits32(x);
}

#endif

}  // namespace bits
}  // namespace base

#endif  // BASE_BITS_H_
