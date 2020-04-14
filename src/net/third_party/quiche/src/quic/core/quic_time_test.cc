// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {

class QuicTimeDeltaTest : public QuicTest {};

TEST_F(QuicTimeDeltaTest, Zero) {
  EXPECT_TRUE(QuicTime::Delta::Zero().IsZero());
  EXPECT_FALSE(QuicTime::Delta::Zero().IsInfinite());
  EXPECT_FALSE(QuicTime::Delta::FromMilliseconds(1).IsZero());
}

TEST_F(QuicTimeDeltaTest, Infinite) {
  EXPECT_TRUE(QuicTime::Delta::Infinite().IsInfinite());
  EXPECT_FALSE(QuicTime::Delta::Zero().IsInfinite());
  EXPECT_FALSE(QuicTime::Delta::FromMilliseconds(1).IsInfinite());
}

TEST_F(QuicTimeDeltaTest, FromTo) {
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(1),
            QuicTime::Delta::FromMicroseconds(1000));
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1),
            QuicTime::Delta::FromMilliseconds(1000));
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1),
            QuicTime::Delta::FromMicroseconds(1000000));

  EXPECT_EQ(1, QuicTime::Delta::FromMicroseconds(1000).ToMilliseconds());
  EXPECT_EQ(2, QuicTime::Delta::FromMilliseconds(2000).ToSeconds());
  EXPECT_EQ(1000, QuicTime::Delta::FromMilliseconds(1).ToMicroseconds());
  EXPECT_EQ(1, QuicTime::Delta::FromMicroseconds(1000).ToMilliseconds());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(2000).ToMicroseconds(),
            QuicTime::Delta::FromSeconds(2).ToMicroseconds());
}

TEST_F(QuicTimeDeltaTest, Add) {
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(2000),
            QuicTime::Delta::Zero() + QuicTime::Delta::FromMilliseconds(2));
}

TEST_F(QuicTimeDeltaTest, Subtract) {
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(1000),
            QuicTime::Delta::FromMilliseconds(2) -
                QuicTime::Delta::FromMilliseconds(1));
}

TEST_F(QuicTimeDeltaTest, Multiply) {
  int i = 2;
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(4000),
            QuicTime::Delta::FromMilliseconds(2) * i);
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(4000),
            i * QuicTime::Delta::FromMilliseconds(2));
  double d = 2;
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(4000),
            QuicTime::Delta::FromMilliseconds(2) * d);
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(4000),
            d * QuicTime::Delta::FromMilliseconds(2));

  // Ensure we are rounding correctly within a single-bit level of precision.
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(5),
            QuicTime::Delta::FromMicroseconds(9) * 0.5);
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(2),
            QuicTime::Delta::FromMicroseconds(12) * 0.2);
}

TEST_F(QuicTimeDeltaTest, Max) {
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(2000),
            std::max(QuicTime::Delta::FromMicroseconds(1000),
                     QuicTime::Delta::FromMicroseconds(2000)));
}

TEST_F(QuicTimeDeltaTest, NotEqual) {
  EXPECT_TRUE(QuicTime::Delta::FromSeconds(0) !=
              QuicTime::Delta::FromSeconds(1));
  EXPECT_FALSE(QuicTime::Delta::FromSeconds(0) !=
               QuicTime::Delta::FromSeconds(0));
}

TEST_F(QuicTimeDeltaTest, DebuggingValue) {
  const QuicTime::Delta one_us = QuicTime::Delta::FromMicroseconds(1);
  const QuicTime::Delta one_ms = QuicTime::Delta::FromMilliseconds(1);
  const QuicTime::Delta one_s = QuicTime::Delta::FromSeconds(1);

  EXPECT_EQ("3s", (3 * one_s).ToDebuggingValue());
  EXPECT_EQ("3ms", (3 * one_ms).ToDebuggingValue());
  EXPECT_EQ("3us", (3 * one_us).ToDebuggingValue());

  EXPECT_EQ("3001us", (3 * one_ms + one_us).ToDebuggingValue());
  EXPECT_EQ("3001ms", (3 * one_s + one_ms).ToDebuggingValue());
  EXPECT_EQ("3000001us", (3 * one_s + one_us).ToDebuggingValue());
}

class QuicTimeTest : public QuicTest {
 protected:
  MockClock clock_;
};

TEST_F(QuicTimeTest, Initialized) {
  EXPECT_FALSE(QuicTime::Zero().IsInitialized());
  EXPECT_TRUE((QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(1))
                  .IsInitialized());
}

TEST_F(QuicTimeTest, CopyConstruct) {
  QuicTime time_1 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1234);
  EXPECT_NE(time_1, QuicTime(QuicTime::Zero()));
  EXPECT_EQ(time_1, QuicTime(time_1));
}

TEST_F(QuicTimeTest, CopyAssignment) {
  QuicTime time_1 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1234);
  QuicTime time_2 = QuicTime::Zero();
  EXPECT_NE(time_1, time_2);
  time_2 = time_1;
  EXPECT_EQ(time_1, time_2);
}

TEST_F(QuicTimeTest, Add) {
  QuicTime time_1 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1);
  QuicTime time_2 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(2);

  QuicTime::Delta diff = time_2 - time_1;

  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(1), diff);
  EXPECT_EQ(1000, diff.ToMicroseconds());
  EXPECT_EQ(1, diff.ToMilliseconds());
}

TEST_F(QuicTimeTest, Subtract) {
  QuicTime time_1 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1);
  QuicTime time_2 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(2);

  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(1), time_2 - time_1);
}

TEST_F(QuicTimeTest, SubtractDelta) {
  QuicTime time = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(2);
  EXPECT_EQ(QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1),
            time - QuicTime::Delta::FromMilliseconds(1));
}

TEST_F(QuicTimeTest, Max) {
  QuicTime time_1 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1);
  QuicTime time_2 = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(2);

  EXPECT_EQ(time_2, std::max(time_1, time_2));
}

TEST_F(QuicTimeTest, MockClock) {
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));

  QuicTime now = clock_.ApproximateNow();
  QuicTime time = QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(1000);

  EXPECT_EQ(now, time);

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  now = clock_.ApproximateNow();

  EXPECT_NE(now, time);

  time = time + QuicTime::Delta::FromMilliseconds(1);
  EXPECT_EQ(now, time);
}

TEST_F(QuicTimeTest, LE) {
  const QuicTime zero = QuicTime::Zero();
  const QuicTime one = zero + QuicTime::Delta::FromSeconds(1);
  EXPECT_TRUE(zero <= zero);
  EXPECT_TRUE(zero <= one);
  EXPECT_TRUE(one <= one);
  EXPECT_FALSE(one <= zero);
}

}  // namespace test
}  // namespace quic
