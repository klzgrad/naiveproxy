// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"

#include <functional>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

namespace http2 {
namespace test {
namespace {

enum class TestEnumClass32 {
  kValue1 = 1,
  kValue99 = 99,
  kValue1M = 1000000,
};

enum class TestEnumClass8 {
  kValue1 = 1,
  kValue2 = 1,
  kValue99 = 99,
  kValue255 = 255,
};

enum TestEnum8 {
  kMaskLo = 0x01,
  kMaskHi = 0x80,
};

struct TestStruct {
  uint8_t f1;
  uint16_t f2;
  uint32_t f3;  // Decoded as a uint24
  uint32_t f4;
  uint32_t f5;  // Decoded as if uint31
  TestEnumClass32 f6;
  TestEnumClass8 f7;
  TestEnum8 f8;
};

class DecodeBufferTest : public ::testing::Test {
 protected:
  Http2Random random_;
  uint32_t decode_offset_;
};

TEST_F(DecodeBufferTest, DecodesFixedInts) {
  const char data[] = "\x01\x12\x23\x34\x45\x56\x67\x78\x89\x9a";
  DecodeBuffer b1(data, strlen(data));
  EXPECT_EQ(1, b1.DecodeUInt8());
  EXPECT_EQ(0x1223u, b1.DecodeUInt16());
  EXPECT_EQ(0x344556u, b1.DecodeUInt24());
  EXPECT_EQ(0x6778899Au, b1.DecodeUInt32());
}

// Make sure that DecodeBuffer is not copying input, just pointing into
// provided input buffer.
TEST_F(DecodeBufferTest, HasNotCopiedInput) {
  const char data[] = "ab";
  DecodeBuffer b1(data, 2);

  EXPECT_EQ(2u, b1.Remaining());
  EXPECT_EQ(0u, b1.Offset());
  EXPECT_FALSE(b1.Empty());
  EXPECT_EQ(data, b1.cursor());  // cursor points to input buffer
  EXPECT_TRUE(b1.HasData());

  b1.AdvanceCursor(1);

  EXPECT_EQ(1u, b1.Remaining());
  EXPECT_EQ(1u, b1.Offset());
  EXPECT_FALSE(b1.Empty());
  EXPECT_EQ(&data[1], b1.cursor());
  EXPECT_TRUE(b1.HasData());

  b1.AdvanceCursor(1);

  EXPECT_EQ(0u, b1.Remaining());
  EXPECT_EQ(2u, b1.Offset());
  EXPECT_TRUE(b1.Empty());
  EXPECT_EQ(&data[2], b1.cursor());
  EXPECT_FALSE(b1.HasData());

  DecodeBuffer b2(data, 0);

  EXPECT_EQ(0u, b2.Remaining());
  EXPECT_EQ(0u, b2.Offset());
  EXPECT_TRUE(b2.Empty());
  EXPECT_EQ(data, b2.cursor());
  EXPECT_FALSE(b2.HasData());
}

// DecodeBufferSubset can't go beyond the end of the base buffer.
TEST_F(DecodeBufferTest, DecodeBufferSubsetLimited) {
  const char data[] = "abc";
  DecodeBuffer base(data, 3);
  base.AdvanceCursor(1);
  DecodeBufferSubset subset(&base, 100);
  EXPECT_EQ(2u, subset.FullSize());
}

// DecodeBufferSubset advances the cursor of its base upon destruction.
TEST_F(DecodeBufferTest, DecodeBufferSubsetAdvancesCursor) {
  const char data[] = "abc";
  const size_t size = sizeof(data) - 1;
  EXPECT_EQ(3u, size);
  DecodeBuffer base(data, size);
  {
    // First no change to the cursor.
    DecodeBufferSubset subset(&base, size + 100);
    EXPECT_EQ(size, subset.FullSize());
    EXPECT_EQ(base.FullSize(), subset.FullSize());
    EXPECT_EQ(0u, subset.Offset());
  }
  EXPECT_EQ(0u, base.Offset());
  EXPECT_EQ(size, base.Remaining());
}

// Make sure that DecodeBuffer ctor complains about bad args.
#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
TEST(DecodeBufferDeathTest, NonNullBufferRequired) {
  EXPECT_DEBUG_DEATH({ DecodeBuffer b(nullptr, 3); }, "nullptr");
}

// Make sure that DecodeBuffer ctor complains about bad args.
TEST(DecodeBufferDeathTest, ModestBufferSizeRequired) {
  EXPECT_DEBUG_DEATH(
      {
        const char data[] = "abc";
        size_t len = 0;
        DecodeBuffer b(data, ~len);
      },
      "Max.*Length");
}

// Make sure that DecodeBuffer detects advance beyond end, in debug mode.
TEST(DecodeBufferDeathTest, LimitedAdvance) {
  {
    // Advance right up to end is OK.
    const char data[] = "abc";
    DecodeBuffer b(data, 3);
    b.AdvanceCursor(3);  // OK
    EXPECT_TRUE(b.Empty());
  }
  EXPECT_DEBUG_DEATH(
      {
        // Going beyond is not OK.
        const char data[] = "abc";
        DecodeBuffer b(data, 3);
        b.AdvanceCursor(4);
      },
      "4 vs. 3");
}

// Make sure that DecodeBuffer detects decode beyond end, in debug mode.
TEST(DecodeBufferDeathTest, DecodeUInt8PastEnd) {
  const char data[] = {0x12, 0x23};
  DecodeBuffer b(data, sizeof data);
  EXPECT_EQ(2u, b.FullSize());
  EXPECT_EQ(0x1223, b.DecodeUInt16());
  EXPECT_DEBUG_DEATH({ b.DecodeUInt8(); }, "1 vs. 0");
}

// Make sure that DecodeBuffer detects decode beyond end, in debug mode.
TEST(DecodeBufferDeathTest, DecodeUInt16OverEnd) {
  const char data[] = {0x12, 0x23, 0x34};
  DecodeBuffer b(data, sizeof data);
  EXPECT_EQ(3u, b.FullSize());
  EXPECT_EQ(0x1223, b.DecodeUInt16());
  EXPECT_DEBUG_DEATH({ b.DecodeUInt16(); }, "2 vs. 1");
}

// Make sure that DecodeBuffer doesn't agree with having two subsets.
TEST(DecodeBufferSubsetDeathTest, TwoSubsets) {
  const char data[] = "abc";
  DecodeBuffer base(data, 3);
  DecodeBufferSubset subset1(&base, 1);
  EXPECT_DEBUG_DEATH({ DecodeBufferSubset subset2(&base, 1); },
                     "There is already a subset");
}

// Make sure that DecodeBufferSubset notices when the base's cursor has moved.
TEST(DecodeBufferSubsetDeathTest, BaseCursorAdvanced) {
  const char data[] = "abc";
  DecodeBuffer base(data, 3);
  base.AdvanceCursor(1);
  EXPECT_DEBUG_DEATH(
      {
        DecodeBufferSubset subset1(&base, 2);
        base.AdvanceCursor(1);
      },
      "Access via subset only when present");
}
#endif  // GTEST_HAS_DEATH_TEST && !defined(NDEBUG)

}  // namespace
}  // namespace test
}  // namespace http2
