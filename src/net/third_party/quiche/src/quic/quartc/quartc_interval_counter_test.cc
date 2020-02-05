// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_interval_counter.h"

#include "net/third_party/quiche/src/quic/core/quic_interval.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace {

class QuartcIntervalCounterTest : public QuicTest {
 protected:
  QuartcIntervalCounter<int> counter_;
};

void ExpectCount(const QuartcIntervalCounter<int>& counter,
                 QuicInterval<int> interval,
                 size_t count) {
  for (int i = interval.min(); i < interval.max(); ++i) {
    EXPECT_EQ(counter.Count(i), count) << "i=" << i;
  }
}

TEST_F(QuartcIntervalCounterTest, InitiallyEmpty) {
  EXPECT_EQ(counter_.MaxCount(), 0u);
}

TEST_F(QuartcIntervalCounterTest, SameInterval) {
  counter_.AddInterval(QuicInterval<int>(0, 6));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 6), 1);

  counter_.AddInterval(QuicInterval<int>(0, 6));
  EXPECT_EQ(counter_.MaxCount(), 2u);
  ExpectCount(counter_, QuicInterval<int>(0, 6), 2);
}

TEST_F(QuartcIntervalCounterTest, DisjointIntervals) {
  counter_.AddInterval(QuicInterval<int>(0, 5));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 5), 1);
  ExpectCount(counter_, QuicInterval<int>(5, 10), 0);

  counter_.AddInterval(QuicInterval<int>(5, 10));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 5), 1);
  ExpectCount(counter_, QuicInterval<int>(5, 10), 1);
}

TEST_F(QuartcIntervalCounterTest, OverlappingIntervals) {
  counter_.AddInterval(QuicInterval<int>(0, 6));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 6), 1);
  ExpectCount(counter_, QuicInterval<int>(6, 10), 0);

  counter_.AddInterval(QuicInterval<int>(5, 10));
  EXPECT_EQ(counter_.MaxCount(), 2u);
  ExpectCount(counter_, QuicInterval<int>(0, 5), 1);
  EXPECT_EQ(counter_.Count(5), 2u);
  ExpectCount(counter_, QuicInterval<int>(6, 10), 1);
}

TEST_F(QuartcIntervalCounterTest, IntervalsWithGapThenOverlap) {
  counter_.AddInterval(QuicInterval<int>(0, 4));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 4), 1);
  ExpectCount(counter_, QuicInterval<int>(4, 10), 0);

  counter_.AddInterval(QuicInterval<int>(7, 10));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 4), 1);
  ExpectCount(counter_, QuicInterval<int>(4, 7), 0);
  ExpectCount(counter_, QuicInterval<int>(7, 10), 1);

  counter_.AddInterval(QuicInterval<int>(3, 8));
  EXPECT_EQ(counter_.MaxCount(), 2u);
  ExpectCount(counter_, QuicInterval<int>(0, 3), 1);
  EXPECT_EQ(counter_.Count(3), 2u);
  ExpectCount(counter_, QuicInterval<int>(4, 7), 1);
  EXPECT_EQ(counter_.Count(7), 2u);
  ExpectCount(counter_, QuicInterval<int>(8, 10), 1);
}

TEST_F(QuartcIntervalCounterTest, RemoveIntervals) {
  counter_.AddInterval(QuicInterval<int>(0, 5));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 5), 1);

  counter_.AddInterval(QuicInterval<int>(4, 10));
  EXPECT_EQ(counter_.MaxCount(), 2u);
  ExpectCount(counter_, QuicInterval<int>(0, 4), 1);
  EXPECT_EQ(counter_.Count(4), 2u);
  ExpectCount(counter_, QuicInterval<int>(5, 10), 1);

  counter_.RemoveInterval(QuicInterval<int>(0, 5));
  EXPECT_EQ(counter_.MaxCount(), 1u);
  ExpectCount(counter_, QuicInterval<int>(0, 5), 0);
  ExpectCount(counter_, QuicInterval<int>(5, 10), 1);

  counter_.RemoveInterval(QuicInterval<int>(5, 10));
  EXPECT_EQ(counter_.MaxCount(), 0u);
  ExpectCount(counter_, QuicInterval<int>(0, 10), 0);
}

}  // namespace
}  // namespace quic
