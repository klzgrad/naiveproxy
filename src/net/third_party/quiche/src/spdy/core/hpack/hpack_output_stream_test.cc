// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"

#include <cstddef>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_test.h"

namespace spdy {

namespace {

// Make sure that AppendBits() appends bits starting from the most
// significant bit, and that it can handle crossing a byte boundary.
TEST(HpackOutputStreamTest, AppendBits) {
  HpackOutputStream output_stream;
  std::string expected_str;

  output_stream.AppendBits(0x1, 1);
  expected_str.append(1, 0x00);
  expected_str.back() |= (0x1 << 7);

  output_stream.AppendBits(0x0, 1);

  output_stream.AppendBits(0x3, 2);
  *expected_str.rbegin() |= (0x3 << 4);

  output_stream.AppendBits(0x0, 2);

  // Byte-crossing append.
  output_stream.AppendBits(0x7, 3);
  *expected_str.rbegin() |= (0x7 >> 1);
  expected_str.append(1, 0x00);
  expected_str.back() |= (0x7 << 7);

  output_stream.AppendBits(0x0, 7);

  std::string str;
  output_stream.TakeString(&str);
  EXPECT_EQ(expected_str, str);
}

// Utility function to return I as a string encoded with an N-bit
// prefix.
std::string EncodeUint32(uint8_t N, uint32_t I) {
  HpackOutputStream output_stream;
  if (N < 8) {
    output_stream.AppendBits(0x00, 8 - N);
  }
  output_stream.AppendUint32(I);
  std::string str;
  output_stream.TakeString(&str);
  return str;
}

// The {Number}ByteIntegersEightBitPrefix tests below test that
// certain integers are encoded correctly with an 8-bit prefix in
// exactly {Number} bytes.

TEST(HpackOutputStreamTest, OneByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(8, 0x00));
  EXPECT_EQ("\x7f", EncodeUint32(8, 0x7f));
  // Maximum.
  EXPECT_EQ("\xfe", EncodeUint32(8, 0xfe));
}

TEST(HpackOutputStreamTest, TwoByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ(std::string("\xff\x00", 2), EncodeUint32(8, 0xff));
  EXPECT_EQ("\xff\x01", EncodeUint32(8, 0x0100));
  // Maximum.
  EXPECT_EQ("\xff\x7f", EncodeUint32(8, 0x017e));
}

TEST(HpackOutputStreamTest, ThreeByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ("\xff\x80\x01", EncodeUint32(8, 0x017f));
  EXPECT_EQ("\xff\x80\x1e", EncodeUint32(8, 0x0fff));
  // Maximum.
  EXPECT_EQ("\xff\xff\x7f", EncodeUint32(8, 0x40fe));
}

TEST(HpackOutputStreamTest, FourByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ("\xff\x80\x80\x01", EncodeUint32(8, 0x40ff));
  EXPECT_EQ("\xff\x80\xfe\x03", EncodeUint32(8, 0xffff));
  // Maximum.
  EXPECT_EQ("\xff\xff\xff\x7f", EncodeUint32(8, 0x002000fe));
}

TEST(HpackOutputStreamTest, FiveByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ("\xff\x80\x80\x80\x01", EncodeUint32(8, 0x002000ff));
  EXPECT_EQ("\xff\x80\xfe\xff\x07", EncodeUint32(8, 0x00ffffff));
  // Maximum.
  EXPECT_EQ("\xff\xff\xff\xff\x7f", EncodeUint32(8, 0x100000fe));
}

TEST(HpackOutputStreamTest, SixByteIntegersEightBitPrefix) {
  // Minimum.
  EXPECT_EQ("\xff\x80\x80\x80\x80\x01", EncodeUint32(8, 0x100000ff));
  // Maximum.
  EXPECT_EQ("\xff\x80\xfe\xff\xff\x0f", EncodeUint32(8, 0xffffffff));
}

// The {Number}ByteIntegersOneToSevenBitPrefix tests below test that
// certain integers are encoded correctly with an N-bit prefix in
// exactly {Number} bytes for N in {1, 2, ..., 7}.

TEST(HpackOutputStreamTest, OneByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(7, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(6, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(5, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(4, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(3, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(2, 0x00));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(1, 0x00));

  // Maximums.
  EXPECT_EQ("\x7e", EncodeUint32(7, 0x7e));
  EXPECT_EQ("\x3e", EncodeUint32(6, 0x3e));
  EXPECT_EQ("\x1e", EncodeUint32(5, 0x1e));
  EXPECT_EQ("\x0e", EncodeUint32(4, 0x0e));
  EXPECT_EQ("\x06", EncodeUint32(3, 0x06));
  EXPECT_EQ("\x02", EncodeUint32(2, 0x02));
  EXPECT_EQ(std::string("\x00", 1), EncodeUint32(1, 0x00));
}

TEST(HpackOutputStreamTest, TwoByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ(std::string("\x7f\x00", 2), EncodeUint32(7, 0x7f));
  EXPECT_EQ(std::string("\x3f\x00", 2), EncodeUint32(6, 0x3f));
  EXPECT_EQ(std::string("\x1f\x00", 2), EncodeUint32(5, 0x1f));
  EXPECT_EQ(std::string("\x0f\x00", 2), EncodeUint32(4, 0x0f));
  EXPECT_EQ(std::string("\x07\x00", 2), EncodeUint32(3, 0x07));
  EXPECT_EQ(std::string("\x03\x00", 2), EncodeUint32(2, 0x03));
  EXPECT_EQ(std::string("\x01\x00", 2), EncodeUint32(1, 0x01));

  // Maximums.
  EXPECT_EQ("\x7f\x7f", EncodeUint32(7, 0xfe));
  EXPECT_EQ("\x3f\x7f", EncodeUint32(6, 0xbe));
  EXPECT_EQ("\x1f\x7f", EncodeUint32(5, 0x9e));
  EXPECT_EQ("\x0f\x7f", EncodeUint32(4, 0x8e));
  EXPECT_EQ("\x07\x7f", EncodeUint32(3, 0x86));
  EXPECT_EQ("\x03\x7f", EncodeUint32(2, 0x82));
  EXPECT_EQ("\x01\x7f", EncodeUint32(1, 0x80));
}

TEST(HpackOutputStreamTest, ThreeByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ("\x7f\x80\x01", EncodeUint32(7, 0xff));
  EXPECT_EQ("\x3f\x80\x01", EncodeUint32(6, 0xbf));
  EXPECT_EQ("\x1f\x80\x01", EncodeUint32(5, 0x9f));
  EXPECT_EQ("\x0f\x80\x01", EncodeUint32(4, 0x8f));
  EXPECT_EQ("\x07\x80\x01", EncodeUint32(3, 0x87));
  EXPECT_EQ("\x03\x80\x01", EncodeUint32(2, 0x83));
  EXPECT_EQ("\x01\x80\x01", EncodeUint32(1, 0x81));

  // Maximums.
  EXPECT_EQ("\x7f\xff\x7f", EncodeUint32(7, 0x407e));
  EXPECT_EQ("\x3f\xff\x7f", EncodeUint32(6, 0x403e));
  EXPECT_EQ("\x1f\xff\x7f", EncodeUint32(5, 0x401e));
  EXPECT_EQ("\x0f\xff\x7f", EncodeUint32(4, 0x400e));
  EXPECT_EQ("\x07\xff\x7f", EncodeUint32(3, 0x4006));
  EXPECT_EQ("\x03\xff\x7f", EncodeUint32(2, 0x4002));
  EXPECT_EQ("\x01\xff\x7f", EncodeUint32(1, 0x4000));
}

TEST(HpackOutputStreamTest, FourByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ("\x7f\x80\x80\x01", EncodeUint32(7, 0x407f));
  EXPECT_EQ("\x3f\x80\x80\x01", EncodeUint32(6, 0x403f));
  EXPECT_EQ("\x1f\x80\x80\x01", EncodeUint32(5, 0x401f));
  EXPECT_EQ("\x0f\x80\x80\x01", EncodeUint32(4, 0x400f));
  EXPECT_EQ("\x07\x80\x80\x01", EncodeUint32(3, 0x4007));
  EXPECT_EQ("\x03\x80\x80\x01", EncodeUint32(2, 0x4003));
  EXPECT_EQ("\x01\x80\x80\x01", EncodeUint32(1, 0x4001));

  // Maximums.
  EXPECT_EQ("\x7f\xff\xff\x7f", EncodeUint32(7, 0x20007e));
  EXPECT_EQ("\x3f\xff\xff\x7f", EncodeUint32(6, 0x20003e));
  EXPECT_EQ("\x1f\xff\xff\x7f", EncodeUint32(5, 0x20001e));
  EXPECT_EQ("\x0f\xff\xff\x7f", EncodeUint32(4, 0x20000e));
  EXPECT_EQ("\x07\xff\xff\x7f", EncodeUint32(3, 0x200006));
  EXPECT_EQ("\x03\xff\xff\x7f", EncodeUint32(2, 0x200002));
  EXPECT_EQ("\x01\xff\xff\x7f", EncodeUint32(1, 0x200000));
}

TEST(HpackOutputStreamTest, FiveByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ("\x7f\x80\x80\x80\x01", EncodeUint32(7, 0x20007f));
  EXPECT_EQ("\x3f\x80\x80\x80\x01", EncodeUint32(6, 0x20003f));
  EXPECT_EQ("\x1f\x80\x80\x80\x01", EncodeUint32(5, 0x20001f));
  EXPECT_EQ("\x0f\x80\x80\x80\x01", EncodeUint32(4, 0x20000f));
  EXPECT_EQ("\x07\x80\x80\x80\x01", EncodeUint32(3, 0x200007));
  EXPECT_EQ("\x03\x80\x80\x80\x01", EncodeUint32(2, 0x200003));
  EXPECT_EQ("\x01\x80\x80\x80\x01", EncodeUint32(1, 0x200001));

  // Maximums.
  EXPECT_EQ("\x7f\xff\xff\xff\x7f", EncodeUint32(7, 0x1000007e));
  EXPECT_EQ("\x3f\xff\xff\xff\x7f", EncodeUint32(6, 0x1000003e));
  EXPECT_EQ("\x1f\xff\xff\xff\x7f", EncodeUint32(5, 0x1000001e));
  EXPECT_EQ("\x0f\xff\xff\xff\x7f", EncodeUint32(4, 0x1000000e));
  EXPECT_EQ("\x07\xff\xff\xff\x7f", EncodeUint32(3, 0x10000006));
  EXPECT_EQ("\x03\xff\xff\xff\x7f", EncodeUint32(2, 0x10000002));
  EXPECT_EQ("\x01\xff\xff\xff\x7f", EncodeUint32(1, 0x10000000));
}

TEST(HpackOutputStreamTest, SixByteIntegersOneToSevenBitPrefixes) {
  // Minimums.
  EXPECT_EQ("\x7f\x80\x80\x80\x80\x01", EncodeUint32(7, 0x1000007f));
  EXPECT_EQ("\x3f\x80\x80\x80\x80\x01", EncodeUint32(6, 0x1000003f));
  EXPECT_EQ("\x1f\x80\x80\x80\x80\x01", EncodeUint32(5, 0x1000001f));
  EXPECT_EQ("\x0f\x80\x80\x80\x80\x01", EncodeUint32(4, 0x1000000f));
  EXPECT_EQ("\x07\x80\x80\x80\x80\x01", EncodeUint32(3, 0x10000007));
  EXPECT_EQ("\x03\x80\x80\x80\x80\x01", EncodeUint32(2, 0x10000003));
  EXPECT_EQ("\x01\x80\x80\x80\x80\x01", EncodeUint32(1, 0x10000001));

  // Maximums.
  EXPECT_EQ("\x7f\x80\xff\xff\xff\x0f", EncodeUint32(7, 0xffffffff));
  EXPECT_EQ("\x3f\xc0\xff\xff\xff\x0f", EncodeUint32(6, 0xffffffff));
  EXPECT_EQ("\x1f\xe0\xff\xff\xff\x0f", EncodeUint32(5, 0xffffffff));
  EXPECT_EQ("\x0f\xf0\xff\xff\xff\x0f", EncodeUint32(4, 0xffffffff));
  EXPECT_EQ("\x07\xf8\xff\xff\xff\x0f", EncodeUint32(3, 0xffffffff));
  EXPECT_EQ("\x03\xfc\xff\xff\xff\x0f", EncodeUint32(2, 0xffffffff));
  EXPECT_EQ("\x01\xfe\xff\xff\xff\x0f", EncodeUint32(1, 0xffffffff));
}

// Test that encoding an integer with an N-bit prefix preserves the
// upper (8-N) bits of the first byte.
TEST(HpackOutputStreamTest, AppendUint32PreservesUpperBits) {
  HpackOutputStream output_stream;
  output_stream.AppendBits(0x7f, 7);
  output_stream.AppendUint32(0x01);
  std::string str;
  output_stream.TakeString(&str);
  EXPECT_EQ(std::string("\xff\x00", 2), str);
}

TEST(HpackOutputStreamTest, AppendBytes) {
  HpackOutputStream output_stream;

  output_stream.AppendBytes("buffer1");
  output_stream.AppendBytes("buffer2");

  std::string str;
  output_stream.TakeString(&str);
  EXPECT_EQ("buffer1buffer2", str);
}

TEST(HpackOutputStreamTest, BoundedTakeString) {
  HpackOutputStream output_stream;

  output_stream.AppendBytes("buffer12");
  output_stream.AppendBytes("buffer456");

  std::string str;
  output_stream.BoundedTakeString(9, &str);
  EXPECT_EQ("buffer12b", str);

  output_stream.AppendBits(0x7f, 7);
  output_stream.AppendUint32(0x11);
  output_stream.BoundedTakeString(9, &str);
  EXPECT_EQ("uffer456\xff", str);

  output_stream.BoundedTakeString(9, &str);
  EXPECT_EQ("\x10", str);
}

}  // namespace

}  // namespace spdy
