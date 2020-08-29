// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_time_accumulator.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {

TEST(QuicTimeAccumulator, DefaultConstruct) {
  MockClock clock;
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));

  QuicTimeAccumulator acc;
  EXPECT_FALSE(acc.IsRunning());

  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  EXPECT_EQ(QuicTime::Delta::Zero(), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::Zero(), acc.GetTotalElapsedTime(clock.Now()));
}

TEST(QuicTimeAccumulator, StartStop) {
  MockClock clock;
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));

  QuicTimeAccumulator acc;
  acc.Start(clock.Now());
  EXPECT_TRUE(acc.IsRunning());

  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  acc.Stop(clock.Now());
  EXPECT_FALSE(acc.IsRunning());

  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10),
            acc.GetTotalElapsedTime(clock.Now()));

  acc.Start(clock.Now());
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(15),
            acc.GetTotalElapsedTime(clock.Now()));

  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20),
            acc.GetTotalElapsedTime(clock.Now()));

  acc.Stop(clock.Now());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(20),
            acc.GetTotalElapsedTime(clock.Now()));
}

TEST(QuicTimeAccumulator, ClockStepBackwards) {
  MockClock clock;
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(100));

  QuicTimeAccumulator acc;
  acc.Start(clock.Now());

  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(-10));
  acc.Stop(clock.Now());
  EXPECT_EQ(QuicTime::Delta::Zero(), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::Zero(), acc.GetTotalElapsedTime(clock.Now()));

  acc.Start(clock.Now());
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(50));
  acc.Stop(clock.Now());

  acc.Start(clock.Now());
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(-80));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(50), acc.GetTotalElapsedTime());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(50),
            acc.GetTotalElapsedTime(clock.Now()));
}

}  // namespace test
}  // namespace quic
