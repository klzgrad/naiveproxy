// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_number.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {

namespace test {

namespace {

TEST(QuicPacketNumberTest, BasicTest) {
  QuicPacketNumber num;
  EXPECT_FALSE(num.IsInitialized());

  QuicPacketNumber num2(10);
  EXPECT_TRUE(num2.IsInitialized());
  EXPECT_EQ(10u, num2.ToUint64());
  EXPECT_EQ(10u, num2.Hash());
  num2.UpdateMax(num);
  EXPECT_EQ(10u, num2.ToUint64());
  num2.UpdateMax(QuicPacketNumber(9));
  EXPECT_EQ(10u, num2.ToUint64());
  num2.UpdateMax(QuicPacketNumber(11));
  EXPECT_EQ(11u, num2.ToUint64());
  num2.Clear();
  EXPECT_FALSE(num2.IsInitialized());
  num2.UpdateMax(QuicPacketNumber(9));
  EXPECT_EQ(9u, num2.ToUint64());

  QuicPacketNumber num4(0);
  EXPECT_TRUE(num4.IsInitialized());
  EXPECT_EQ(0u, num4.ToUint64());
  EXPECT_EQ(0u, num4.Hash());
  num4.Clear();
  EXPECT_FALSE(num4.IsInitialized());
}

TEST(QuicPacketNumberTest, Operators) {
  QuicPacketNumber num(100);
  EXPECT_EQ(QuicPacketNumber(100), num++);
  EXPECT_EQ(QuicPacketNumber(101), num);
  EXPECT_EQ(QuicPacketNumber(101), num--);
  EXPECT_EQ(QuicPacketNumber(100), num);

  EXPECT_EQ(QuicPacketNumber(101), ++num);
  EXPECT_EQ(QuicPacketNumber(100), --num);

  QuicPacketNumber num3(0);
  EXPECT_EQ(QuicPacketNumber(0), num3++);
  EXPECT_EQ(QuicPacketNumber(1), num3);
  EXPECT_EQ(QuicPacketNumber(2), ++num3);

  EXPECT_EQ(QuicPacketNumber(2), num3--);
  EXPECT_EQ(QuicPacketNumber(1), num3);
  EXPECT_EQ(QuicPacketNumber(0), --num3);
}

}  // namespace

}  // namespace test

}  // namespace quic
