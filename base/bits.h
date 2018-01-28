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
#include "build/build_config.h"

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

// CountLeadingZeroBits(value) returns the number of zero bits following the
// most significant 1 bit in |value| if |value| is non-zero, otherwise it
// returns {sizeof(T) * 8}.
// Example: 00100010 -> 2
//
// CountTrailingZeroBits(value) returns the number of zero bits preceding the
// least significant 1 bit in |value| if |value| is non-zero, otherwise it
// returns {sizeof(T) * 8}.
// Example: 00100010 -> 1
//
// C does not have an operator to do this, but fortunately the various
// compilers have built-ins that map to fast underlying processor instructions.
#if defined(COMPILER_MSVC)

template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 4,
                            unsigned>::type
    CountLeadingZeroBits(T x) {
  static_assert(bits > 0, "invalid instantiation");
  unsigned long index;
  return LIKELY(_BitScanReverse(&index, static_cast<uint32_t>(x)))
             ? (31 - index - (32 - bits))
             : bits;
}

template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) == 8,
                            unsigned>::type
    CountLeadingZeroBits(T x) {
  static_assert(bits > 0, "invalid instantiation");
  unsigned long index;
  return LIKELY(_BitScanReverse64(&index, static_cast<uint64_t>(x)))
             ? (63 - index)
             : 64;
}

template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 4,
                            unsigned>::type
    CountTrailingZeroBits(T x) {
  static_assert(bits > 0, "invalid instantiation");
  unsigned long index;
  return LIKELY(_BitScanForward(&index, static_cast<uint32_t>(x))) ? index
                                                                   : bits;
}

template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) == 8,
                            unsigned>::type
    CountTrailingZeroBits(T x) {
  static_assert(bits > 0, "invalid instantiation");
  unsigned long index;
  return LIKELY(_BitScanForward64(&index, static_cast<uint64_t>(x))) ? index
                                                                     : 64;
}

ALWAYS_INLINE uint32_t CountLeadingZeroBits32(uint32_t x) {
  return CountLeadingZeroBits(x);
}

#if defined(ARCH_CPU_64_BITS)

// MSVC only supplies _BitScanForward64 when building for a 64-bit target.
ALWAYS_INLINE uint64_t CountLeadingZeroBits64(uint64_t x) {
  return CountLeadingZeroBits(x);
}

#endif

#elif defined(COMPILER_GCC)

// __builtin_clz has undefined behaviour for an input of 0, even though there's
// clearly a return value that makes sense, and even though some processor clz
// instructions have defined behaviour for 0. We could drop to raw __asm__ to
// do better, but we'll avoid doing that unless we see proof that we need to.
template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountLeadingZeroBits(T value) {
  static_assert(bits > 0, "invalid instantiation");
  return LIKELY(value)
             ? bits == 64
                   ? __builtin_clzll(static_cast<uint64_t>(value))
                   : __builtin_clz(static_cast<uint32_t>(value)) - (32 - bits)
             : bits;
}

template <typename T, unsigned bits = sizeof(T) * 8>
ALWAYS_INLINE
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountTrailingZeroBits(T value) {
  return LIKELY(value) ? bits == 64
                             ? __builtin_ctzll(static_cast<uint64_t>(value))
                             : __builtin_ctz(static_cast<uint32_t>(value))
                       : bits;
}

ALWAYS_INLINE uint32_t CountLeadingZeroBits32(uint32_t x) {
  return CountLeadingZeroBits(x);
}

#if defined(ARCH_CPU_64_BITS)

ALWAYS_INLINE uint64_t CountLeadingZeroBits64(uint64_t x) {
  return CountLeadingZeroBits(x);
}

#endif

#endif

ALWAYS_INLINE size_t CountLeadingZeroBitsSizeT(size_t x) {
  return CountLeadingZeroBits(x);
}

ALWAYS_INLINE size_t CountTrailingZeroBitsSizeT(size_t x) {
  return CountTrailingZeroBits(x);
}

}  // namespace bits
}  // namespace base

#endif  // BASE_BITS_H_
