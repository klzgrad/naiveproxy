// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"

#include <limits>

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class QuicBandwidthTest : public QuicTest {};

TEST_F(QuicBandwidthTest, FromTo) {
  EXPECT_EQ(QuicBandwidth::FromKBitsPerSecond(1),
            QuicBandwidth::FromBitsPerSecond(1000));
  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(1),
            QuicBandwidth::FromBytesPerSecond(1000));
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(8000),
            QuicBandwidth::FromBytesPerSecond(1000));
  EXPECT_EQ(QuicBandwidth::FromKBitsPerSecond(8),
            QuicBandwidth::FromKBytesPerSecond(1));

  EXPECT_EQ(0, QuicBandwidth::Zero().ToBitsPerSecond());
  EXPECT_EQ(0, QuicBandwidth::Zero().ToKBitsPerSecond());
  EXPECT_EQ(0, QuicBandwidth::Zero().ToBytesPerSecond());
  EXPECT_EQ(0, QuicBandwidth::Zero().ToKBytesPerSecond());

  EXPECT_EQ(1, QuicBandwidth::FromBitsPerSecond(1000).ToKBitsPerSecond());
  EXPECT_EQ(1000, QuicBandwidth::FromKBitsPerSecond(1).ToBitsPerSecond());
  EXPECT_EQ(1, QuicBandwidth::FromBytesPerSecond(1000).ToKBytesPerSecond());
  EXPECT_EQ(1000, QuicBandwidth::FromKBytesPerSecond(1).ToBytesPerSecond());
}

TEST_F(QuicBandwidthTest, Add) {
  QuicBandwidth bandwidht_1 = QuicBandwidth::FromKBitsPerSecond(1);
  QuicBandwidth bandwidht_2 = QuicBandwidth::FromKBytesPerSecond(1);

  EXPECT_EQ(9000, (bandwidht_1 + bandwidht_2).ToBitsPerSecond());
  EXPECT_EQ(9000, (bandwidht_2 + bandwidht_1).ToBitsPerSecond());
}

TEST_F(QuicBandwidthTest, Subtract) {
  QuicBandwidth bandwidht_1 = QuicBandwidth::FromKBitsPerSecond(1);
  QuicBandwidth bandwidht_2 = QuicBandwidth::FromKBytesPerSecond(1);

  EXPECT_EQ(7000, (bandwidht_2 - bandwidht_1).ToBitsPerSecond());
}

TEST_F(QuicBandwidthTest, TimeDelta) {
  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(1000),
            QuicBandwidth::FromBytesAndTimeDelta(
                1000, QuicTime::Delta::FromMilliseconds(1)));

  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(10),
            QuicBandwidth::FromBytesAndTimeDelta(
                1000, QuicTime::Delta::FromMilliseconds(100)));
}

TEST_F(QuicBandwidthTest, Scale) {
  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(500),
            QuicBandwidth::FromKBytesPerSecond(1000) * 0.5f);
  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(750),
            0.75f * QuicBandwidth::FromKBytesPerSecond(1000));
  EXPECT_EQ(QuicBandwidth::FromKBytesPerSecond(1250),
            QuicBandwidth::FromKBytesPerSecond(1000) * 1.25f);

  // Ensure we are rounding correctly within a 1bps level of precision.
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(5),
            QuicBandwidth::FromBitsPerSecond(9) * 0.5f);
  EXPECT_EQ(QuicBandwidth::FromBitsPerSecond(2),
            QuicBandwidth::FromBitsPerSecond(12) * 0.2f);
}

TEST_F(QuicBandwidthTest, BytesPerPeriod) {
  EXPECT_EQ(2000u, QuicBandwidth::FromKBytesPerSecond(2000).ToBytesPerPeriod(
                       QuicTime::Delta::FromMilliseconds(1)));
  EXPECT_EQ(2u, QuicBandwidth::FromKBytesPerSecond(2000).ToKBytesPerPeriod(
                    QuicTime::Delta::FromMilliseconds(1)));
  EXPECT_EQ(200000u, QuicBandwidth::FromKBytesPerSecond(2000).ToBytesPerPeriod(
                         QuicTime::Delta::FromMilliseconds(100)));
  EXPECT_EQ(200u, QuicBandwidth::FromKBytesPerSecond(2000).ToKBytesPerPeriod(
                      QuicTime::Delta::FromMilliseconds(100)));

  // 1599 * 1001 = 1600599 bits/ms = 200.074875 bytes/s.
  EXPECT_EQ(200u, QuicBandwidth::FromBitsPerSecond(1599).ToBytesPerPeriod(
                      QuicTime::Delta::FromMilliseconds(1001)));

  EXPECT_EQ(200u, QuicBandwidth::FromBitsPerSecond(1599).ToKBytesPerPeriod(
                      QuicTime::Delta::FromSeconds(1001)));
}

TEST_F(QuicBandwidthTest, TransferTime) {
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1),
            QuicBandwidth::FromKBytesPerSecond(1).TransferTime(1000));
  EXPECT_EQ(QuicTime::Delta::Zero(), QuicBandwidth::Zero().TransferTime(1000));
}

TEST_F(QuicBandwidthTest, RelOps) {
  const QuicBandwidth b1 = QuicBandwidth::FromKBitsPerSecond(1);
  const QuicBandwidth b2 = QuicBandwidth::FromKBytesPerSecond(2);
  EXPECT_EQ(b1, b1);
  EXPECT_NE(b1, b2);
  EXPECT_LT(b1, b2);
  EXPECT_GT(b2, b1);
  EXPECT_LE(b1, b1);
  EXPECT_LE(b1, b2);
  EXPECT_GE(b1, b1);
  EXPECT_GE(b2, b1);
}

TEST_F(QuicBandwidthTest, DebuggingValue) {
  EXPECT_EQ("128 bits/s (16 bytes/s)",
            QuicBandwidth::FromBytesPerSecond(16).ToDebuggingValue());
  EXPECT_EQ("4096 bits/s (512 bytes/s)",
            QuicBandwidth::FromBytesPerSecond(512).ToDebuggingValue());

  QuicBandwidth bandwidth = QuicBandwidth::FromBytesPerSecond(1000 * 50);
  EXPECT_EQ("400.00 kbits/s (50.00 kbytes/s)", bandwidth.ToDebuggingValue());

  bandwidth = bandwidth * 1000;
  EXPECT_EQ("400.00 Mbits/s (50.00 Mbytes/s)", bandwidth.ToDebuggingValue());

  bandwidth = bandwidth * 1000;
  EXPECT_EQ("400.00 Gbits/s (50.00 Gbytes/s)", bandwidth.ToDebuggingValue());
}

TEST_F(QuicBandwidthTest, SpecialValues) {
  EXPECT_EQ(0, QuicBandwidth::Zero().ToBitsPerSecond());
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            QuicBandwidth::Infinite().ToBitsPerSecond());

  EXPECT_TRUE(QuicBandwidth::Zero().IsZero());
  EXPECT_FALSE(QuicBandwidth::Zero().IsInfinite());

  EXPECT_TRUE(QuicBandwidth::Infinite().IsInfinite());
  EXPECT_FALSE(QuicBandwidth::Infinite().IsZero());
}

}  // namespace test
}  // namespace quic
