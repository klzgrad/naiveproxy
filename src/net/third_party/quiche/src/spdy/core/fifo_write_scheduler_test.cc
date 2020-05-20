// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/fifo_write_scheduler.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_test_helpers.h"

namespace spdy {

namespace test {

TEST(FifoWriteSchedulerTest, BasicTest) {
  FifoWriteScheduler<SpdyStreamId> fifo;
  EXPECT_FALSE(fifo.HasReadyStreams());
  EXPECT_SPDY_BUG(
      EXPECT_EQ(0u, std::get<0>(fifo.PopNextReadyStreamAndPrecedence())),
      "No ready streams available");
  EXPECT_SPDY_BUG(fifo.MarkStreamReady(9, true), "Stream 9 is not registered");
  EXPECT_SPDY_BUG(fifo.IsStreamReady(9), "Stream 9 is not registered");

  SpdyStreamPrecedence precedence(1);
  fifo.RegisterStream(3, precedence);
  EXPECT_FALSE(fifo.IsStreamReady(3));
  fifo.RegisterStream(9, precedence);
  fifo.RegisterStream(7, precedence);
  fifo.RegisterStream(11, precedence);
  fifo.RegisterStream(13, precedence);
  fifo.RegisterStream(15, precedence);
  fifo.RegisterStream(17, precedence);
  EXPECT_EQ(7u, fifo.NumRegisteredStreams());
  EXPECT_FALSE(fifo.HasReadyStreams());

  fifo.MarkStreamReady(9, true);
  EXPECT_TRUE(fifo.IsStreamReady(9));
  EXPECT_TRUE(fifo.HasReadyStreams());
  fifo.MarkStreamReady(15, true);
  fifo.MarkStreamReady(7, true);
  fifo.MarkStreamReady(13, true);
  fifo.MarkStreamReady(11, true);
  fifo.MarkStreamReady(3, true);
  fifo.MarkStreamReady(17, true);
  EXPECT_EQ(7u, fifo.NumReadyStreams());

  EXPECT_EQ(3u, fifo.PopNextReadyStream());
  EXPECT_EQ(7u, std::get<0>(fifo.PopNextReadyStreamAndPrecedence()));
  EXPECT_EQ(5u, fifo.NumReadyStreams());

  EXPECT_FALSE(fifo.ShouldYield(3));
  EXPECT_FALSE(fifo.ShouldYield(9));
  EXPECT_TRUE(fifo.ShouldYield(13));
  EXPECT_TRUE(fifo.ShouldYield(10));

  fifo.MarkStreamNotReady(9);
  EXPECT_EQ(4u, fifo.NumReadyStreams());
  EXPECT_FALSE(fifo.ShouldYield(10));
  EXPECT_TRUE(fifo.ShouldYield(12));
}

TEST(FifoWriteSchedulerTest, GetLatestEventTest) {
  FifoWriteScheduler<SpdyStreamId> fifo;

  SpdyStreamPrecedence precedence(1);
  fifo.RegisterStream(1, precedence);
  fifo.RegisterStream(3, precedence);
  fifo.RegisterStream(5, precedence);
  fifo.RegisterStream(7, precedence);
  fifo.RegisterStream(9, precedence);
  fifo.RecordStreamEventTime(1, 3);
  fifo.RecordStreamEventTime(3, 2);
  fifo.RecordStreamEventTime(5, 4);
  fifo.RecordStreamEventTime(7, 8);
  fifo.RecordStreamEventTime(9, 1);

  EXPECT_EQ(8, fifo.GetLatestEventWithPrecedence(9));
  EXPECT_EQ(4, fifo.GetLatestEventWithPrecedence(7));
  EXPECT_EQ(3, fifo.GetLatestEventWithPrecedence(5));
  EXPECT_EQ(3, fifo.GetLatestEventWithPrecedence(3));
  EXPECT_EQ(0, fifo.GetLatestEventWithPrecedence(1));
}

}  // namespace test

}  // namespace spdy
