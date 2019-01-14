// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/varint/hpack_varint_encoder.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace http2 {
namespace test {
namespace {

TEST(HpackVarintEncoderTest, Done) {
  HpackVarintEncoder varint_encoder;
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());
}

// Encode integers that each fit in their respective prefixes.
TEST(HpackVarintEncoderTest, Shorts) {
  HpackVarintEncoder varint_encoder;

  uint8_t high_bits1 = 0b10101000;
  uint8_t prefix_length1 = 3;
  uint64_t varint1 = 6;

  EXPECT_EQ(0b10101110,
            varint_encoder.StartEncoding(high_bits1, prefix_length1, varint1));
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());

  uint8_t high_bits2 = 0b00000000;
  uint8_t prefix_length2 = 7;
  uint64_t varint2 = 91;

  EXPECT_EQ(0b01011011,
            varint_encoder.StartEncoding(high_bits2, prefix_length2, varint2));
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());

  uint8_t high_bits3 = 0b10100000;
  uint8_t prefix_length3 = 4;
  uint64_t varint3 = 13;

  EXPECT_EQ(0b10101101,
            varint_encoder.StartEncoding(high_bits3, prefix_length3, varint3));
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());
}

// Encode integers that not fit in their respective prefixes.
TEST(HpackVarintEncoderTest, Long) {
  HpackVarintEncoder varint_encoder;

  uint8_t high_bits1 = 0b10101000;
  uint8_t prefix_length1 = 3;
  uint64_t varint1 = 13;

  EXPECT_EQ(0b10101111,
            varint_encoder.StartEncoding(high_bits1, prefix_length1, varint1));
  EXPECT_TRUE(varint_encoder.IsEncodingInProgress());

  Http2String output1;
  EXPECT_EQ(1u, varint_encoder.ResumeEncoding(1, &output1));
  ASSERT_EQ(1u, output1.size());
  EXPECT_EQ(0b00000110, output1[0]);
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());

  uint8_t high_bits2 = 0b01000000;
  uint8_t prefix_length2 = 5;
  uint64_t varint2 = 100;

  EXPECT_EQ(0b01011111,
            varint_encoder.StartEncoding(high_bits2, prefix_length2, varint2));
  EXPECT_TRUE(varint_encoder.IsEncodingInProgress());

  Http2String output2;
  EXPECT_EQ(1u, varint_encoder.ResumeEncoding(1, &output2));
  ASSERT_EQ(1u, output2.size());
  EXPECT_EQ(0b01000101, output2[0]);
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());
}

// Make sure that the encoder outputs the last byte even when it is zero.
TEST(HpackVarintEncoderTest, LastByteIsZero) {
  HpackVarintEncoder varint_encoder;

  uint8_t high_bits1 = 0b10101000;
  uint8_t prefix_length1 = 3;
  uint64_t varint1 = 7;

  EXPECT_EQ(0b10101111,
            varint_encoder.StartEncoding(high_bits1, prefix_length1, varint1));
  EXPECT_TRUE(varint_encoder.IsEncodingInProgress());

  Http2String output1;
  EXPECT_EQ(1u, varint_encoder.ResumeEncoding(1, &output1));
  ASSERT_EQ(1u, output1.size());
  EXPECT_EQ(0b00000000, output1[0]);
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());

  uint8_t high_bits2 = 0b01100000;
  uint8_t prefix_length2 = 5;
  uint64_t varint2 = 31;

  EXPECT_EQ(0b01111111,
            varint_encoder.StartEncoding(high_bits2, prefix_length2, varint2));
  EXPECT_TRUE(varint_encoder.IsEncodingInProgress());

  Http2String output2;
  EXPECT_EQ(1u, varint_encoder.ResumeEncoding(1, &output2));
  ASSERT_EQ(1u, output2.size());
  EXPECT_EQ(0b00000000, output2[0]);
  EXPECT_FALSE(varint_encoder.IsEncodingInProgress());
}

}  // namespace
}  // namespace test
}  // namespace http2
