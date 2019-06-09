// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the unit tests for the bit utilities.

#include "base/bits.h"
#include "build/build_config.h"

#include <stddef.h>

#include <limits>

#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace bits {

TEST(BitsTest, Log2Floor) {
  EXPECT_EQ(-1, Log2Floor(0));
  EXPECT_EQ(0, Log2Floor(1));
  EXPECT_EQ(1, Log2Floor(2));
  EXPECT_EQ(1, Log2Floor(3));
  EXPECT_EQ(2, Log2Floor(4));
  for (int i = 3; i < 31; ++i) {
    unsigned int value = 1U << i;
    EXPECT_EQ(i, Log2Floor(value));
    EXPECT_EQ(i, Log2Floor(value + 1));
    EXPECT_EQ(i, Log2Floor(value + 2));
    EXPECT_EQ(i - 1, Log2Floor(value - 1));
    EXPECT_EQ(i - 1, Log2Floor(value - 2));
  }
  EXPECT_EQ(31, Log2Floor(0xffffffffU));
}

TEST(BitsTest, Log2Ceiling) {
  EXPECT_EQ(-1, Log2Ceiling(0));
  EXPECT_EQ(0, Log2Ceiling(1));
  EXPECT_EQ(1, Log2Ceiling(2));
  EXPECT_EQ(2, Log2Ceiling(3));
  EXPECT_EQ(2, Log2Ceiling(4));
  for (int i = 3; i < 31; ++i) {
    unsigned int value = 1U << i;
    EXPECT_EQ(i, Log2Ceiling(value));
    EXPECT_EQ(i + 1, Log2Ceiling(value + 1));
    EXPECT_EQ(i + 1, Log2Ceiling(value + 2));
    EXPECT_EQ(i, Log2Ceiling(value - 1));
    EXPECT_EQ(i, Log2Ceiling(value - 2));
  }
  EXPECT_EQ(32, Log2Ceiling(0xffffffffU));
}

TEST(BitsTest, Align) {
  static constexpr size_t kSizeTMax = std::numeric_limits<size_t>::max();
  EXPECT_EQ(0ul, Align(0, 4));
  EXPECT_EQ(4ul, Align(1, 4));
  EXPECT_EQ(4096ul, Align(1, 4096));
  EXPECT_EQ(4096ul, Align(4096, 4096));
  EXPECT_EQ(4096ul, Align(4095, 4096));
  EXPECT_EQ(8192ul, Align(4097, 4096));
  EXPECT_EQ(kSizeTMax - 31, Align(kSizeTMax - 62, 32));
  EXPECT_EQ(kSizeTMax / 2 + 1, Align(1, kSizeTMax / 2 + 1));
}

TEST(BitsTest, AlignDown) {
  static constexpr size_t kSizeTMax = std::numeric_limits<size_t>::max();
  EXPECT_EQ(0ul, AlignDown(0, 4));
  EXPECT_EQ(0ul, AlignDown(1, 4));
  EXPECT_EQ(0ul, AlignDown(1, 4096));
  EXPECT_EQ(4096ul, AlignDown(4096, 4096));
  EXPECT_EQ(0ul, AlignDown(4095, 4096));
  EXPECT_EQ(4096ul, AlignDown(4097, 4096));
  EXPECT_EQ(kSizeTMax - 63, AlignDown(kSizeTMax - 62, 32));
  EXPECT_EQ(kSizeTMax - 31, AlignDown(kSizeTMax, 32));
  EXPECT_EQ(0ul, AlignDown(1, kSizeTMax / 2 + 1));
}

TEST(BitsTest, CountLeadingZeroBits8) {
  EXPECT_EQ(8u, CountLeadingZeroBits(uint8_t{0}));
  EXPECT_EQ(7u, CountLeadingZeroBits(uint8_t{1}));
  for (uint8_t shift = 0; shift <= 7; shift++) {
    EXPECT_EQ(7u - shift,
              CountLeadingZeroBits(static_cast<uint8_t>(1 << shift)));
  }
  EXPECT_EQ(4u, CountLeadingZeroBits(uint8_t{0x0f}));
}

TEST(BitsTest, CountLeadingZeroBits16) {
  EXPECT_EQ(16u, CountLeadingZeroBits(uint16_t{0}));
  EXPECT_EQ(15u, CountLeadingZeroBits(uint16_t{1}));
  for (uint16_t shift = 0; shift <= 15; shift++) {
    EXPECT_EQ(15u - shift,
              CountLeadingZeroBits(static_cast<uint16_t>(1 << shift)));
  }
  EXPECT_EQ(4u, CountLeadingZeroBits(uint16_t{0x0f0f}));
}

TEST(BitsTest, CountLeadingZeroBits32) {
  EXPECT_EQ(32u, CountLeadingZeroBits(uint32_t{0}));
  EXPECT_EQ(31u, CountLeadingZeroBits(uint32_t{1}));
  for (uint32_t shift = 0; shift <= 31; shift++) {
    EXPECT_EQ(31u - shift, CountLeadingZeroBits(uint32_t{1} << shift));
  }
  EXPECT_EQ(4u, CountLeadingZeroBits(uint32_t{0x0f0f0f0f}));
}

TEST(BitsTest, CountTrailingeZeroBits8) {
  EXPECT_EQ(8u, CountTrailingZeroBits(uint8_t{0}));
  EXPECT_EQ(7u, CountTrailingZeroBits(uint8_t{128}));
  for (uint8_t shift = 0; shift <= 7; shift++) {
    EXPECT_EQ(shift, CountTrailingZeroBits(static_cast<uint8_t>(1 << shift)));
  }
  EXPECT_EQ(4u, CountTrailingZeroBits(uint8_t{0xf0}));
}

TEST(BitsTest, CountTrailingeZeroBits16) {
  EXPECT_EQ(16u, CountTrailingZeroBits(uint16_t{0}));
  EXPECT_EQ(15u, CountTrailingZeroBits(uint16_t{32768}));
  for (uint16_t shift = 0; shift <= 15; shift++) {
    EXPECT_EQ(shift, CountTrailingZeroBits(static_cast<uint16_t>(1 << shift)));
  }
  EXPECT_EQ(4u, CountTrailingZeroBits(uint16_t{0xf0f0}));
}

TEST(BitsTest, CountTrailingeZeroBits32) {
  EXPECT_EQ(32u, CountTrailingZeroBits(uint32_t{0}));
  EXPECT_EQ(31u, CountTrailingZeroBits(uint32_t{1} << 31));
  for (uint32_t shift = 0; shift <= 31; shift++) {
    EXPECT_EQ(shift, CountTrailingZeroBits(uint32_t{1} << shift));
  }
  EXPECT_EQ(4u, CountTrailingZeroBits(uint32_t{0xf0f0f0f0}));
}

TEST(BitsTest, CountLeadingZeroBits64) {
  EXPECT_EQ(64u, CountLeadingZeroBits(uint64_t{0}));
  EXPECT_EQ(63u, CountLeadingZeroBits(uint64_t{1}));
  for (uint64_t shift = 0; shift <= 63; shift++) {
    EXPECT_EQ(63u - shift, CountLeadingZeroBits(uint64_t{1} << shift));
  }
  EXPECT_EQ(4u, CountLeadingZeroBits(uint64_t{0x0f0f0f0f0f0f0f0f}));
}

TEST(BitsTest, CountTrailingeZeroBits64) {
  EXPECT_EQ(64u, CountTrailingZeroBits(uint64_t{0}));
  EXPECT_EQ(63u, CountTrailingZeroBits(uint64_t{1} << 63));
  for (uint64_t shift = 0; shift <= 31; shift++) {
    EXPECT_EQ(shift, CountTrailingZeroBits(uint64_t{1} << shift));
  }
  EXPECT_EQ(4u, CountTrailingZeroBits(uint64_t{0xf0f0f0f0f0f0f0f0}));
}

TEST(BitsTest, CountLeadingZeroBitsSizeT) {
#if defined(ARCH_CPU_64_BITS)
  EXPECT_EQ(64u, CountLeadingZeroBitsSizeT(size_t{0}));
  EXPECT_EQ(63u, CountLeadingZeroBitsSizeT(size_t{1}));
  EXPECT_EQ(32u, CountLeadingZeroBitsSizeT(size_t{1} << 31));
  EXPECT_EQ(1u, CountLeadingZeroBitsSizeT(size_t{1} << 62));
  EXPECT_EQ(0u, CountLeadingZeroBitsSizeT(size_t{1} << 63));
#else
  EXPECT_EQ(32u, CountLeadingZeroBitsSizeT(size_t{0}));
  EXPECT_EQ(31u, CountLeadingZeroBitsSizeT(size_t{1}));
  EXPECT_EQ(1u, CountLeadingZeroBitsSizeT(size_t{1} << 30));
  EXPECT_EQ(0u, CountLeadingZeroBitsSizeT(size_t{1} << 31));
#endif  // ARCH_CPU_64_BITS
}

TEST(BitsTest, CountTrailingZeroBitsSizeT) {
#if defined(ARCH_CPU_64_BITS)
  EXPECT_EQ(64u, CountTrailingZeroBitsSizeT(size_t{0}));
  EXPECT_EQ(63u, CountTrailingZeroBitsSizeT(size_t{1} << 63));
  EXPECT_EQ(31u, CountTrailingZeroBitsSizeT(size_t{1} << 31));
  EXPECT_EQ(1u, CountTrailingZeroBitsSizeT(size_t{2}));
  EXPECT_EQ(0u, CountTrailingZeroBitsSizeT(size_t{1}));
#else
  EXPECT_EQ(32u, CountTrailingZeroBitsSizeT(size_t{0}));
  EXPECT_EQ(31u, CountTrailingZeroBitsSizeT(size_t{1} << 31));
  EXPECT_EQ(1u, CountTrailingZeroBitsSizeT(size_t{2}));
  EXPECT_EQ(0u, CountTrailingZeroBitsSizeT(size_t{1}));
#endif  // ARCH_CPU_64_BITS
}

TEST(BitsTest, PowerOfTwo) {
  EXPECT_FALSE(IsPowerOfTwo(-1));
  EXPECT_FALSE(IsPowerOfTwo(0));
  EXPECT_TRUE(IsPowerOfTwo(1));
  EXPECT_TRUE(IsPowerOfTwo(2));
  // Unsigned 64 bit cases.
  for (uint32_t i = 2; i < 64; i++) {
    const uint64_t val = uint64_t{1} << i;
    EXPECT_FALSE(IsPowerOfTwo(val - 1));
    EXPECT_TRUE(IsPowerOfTwo(val));
    EXPECT_FALSE(IsPowerOfTwo(val + 1));
  }
  // Signed 64 bit cases.
  for (uint32_t i = 2; i < 63; i++) {
    const int64_t val = int64_t{1} << i;
    EXPECT_FALSE(IsPowerOfTwo(val - 1));
    EXPECT_TRUE(IsPowerOfTwo(val));
    EXPECT_FALSE(IsPowerOfTwo(val + 1));
  }
  // Signed integers with only the last bit set are negative, not powers of two.
  EXPECT_FALSE(IsPowerOfTwo(int64_t{1} << 63));
}

}  // namespace bits
}  // namespace base
