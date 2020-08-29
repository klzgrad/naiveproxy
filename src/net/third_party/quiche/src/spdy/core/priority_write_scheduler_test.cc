// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/priority_write_scheduler.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/third_party/quiche/src/spdy/core/spdy_test_utils.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_test_helpers.h"

namespace spdy {
namespace test {

template <typename StreamIdType>
class PriorityWriteSchedulerPeer {
 public:
  explicit PriorityWriteSchedulerPeer(
      PriorityWriteScheduler<StreamIdType>* scheduler)
      : scheduler_(scheduler) {}

  size_t NumReadyStreams(SpdyPriority priority) const {
    return scheduler_->priority_infos_[priority].ready_list.size();
  }

 private:
  PriorityWriteScheduler<StreamIdType>* scheduler_;
};

namespace {

class PriorityWriteSchedulerTest : public QuicheTest {
 public:
  PriorityWriteSchedulerTest() : peer_(&scheduler_) {}

  PriorityWriteScheduler<SpdyStreamId> scheduler_;
  PriorityWriteSchedulerPeer<SpdyStreamId> peer_;
};

TEST_F(PriorityWriteSchedulerTest, RegisterUnregisterStreams) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_FALSE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(0u, scheduler_.NumRegisteredStreams());
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(1));
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(1u, scheduler_.NumRegisteredStreams());

  // Root stream counts as already registered.
  EXPECT_SPDY_BUG(
      scheduler_.RegisterStream(kHttp2RootStreamId, SpdyStreamPrecedence(1)),
      "Stream 0 already registered");

  // Try redundant registrations.
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(1, SpdyStreamPrecedence(1)),
                  "Stream 1 already registered");
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(1, SpdyStreamPrecedence(2)),
                  "Stream 1 already registered");

  scheduler_.RegisterStream(2, SpdyStreamPrecedence(3));
  EXPECT_EQ(2u, scheduler_.NumRegisteredStreams());

  // Verify registration != ready.
  EXPECT_FALSE(scheduler_.HasReadyStreams());

  scheduler_.UnregisterStream(1);
  EXPECT_EQ(1u, scheduler_.NumRegisteredStreams());
  scheduler_.UnregisterStream(2);
  EXPECT_EQ(0u, scheduler_.NumRegisteredStreams());

  // Try redundant unregistration.
  EXPECT_SPDY_BUG(scheduler_.UnregisterStream(1), "Stream 1 not registered");
  EXPECT_SPDY_BUG(scheduler_.UnregisterStream(2), "Stream 2 not registered");
}

TEST_F(PriorityWriteSchedulerTest, RegisterStreamWithHttp2StreamDependency) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_FALSE(scheduler_.StreamRegistered(1));
  scheduler_.RegisterStream(
      1, SpdyStreamPrecedence(kHttp2RootStreamId, 123, false));
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_TRUE(scheduler_.GetStreamPrecedence(1).is_spdy3_priority());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());
  EXPECT_FALSE(scheduler_.HasReadyStreams());

  EXPECT_SPDY_BUG(scheduler_.RegisterStream(
                      1, SpdyStreamPrecedence(kHttp2RootStreamId, 256, false)),
                  "Stream 1 already registered");
  EXPECT_TRUE(scheduler_.GetStreamPrecedence(1).is_spdy3_priority());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  // Registering stream with a non-existent parent stream is permissible, per
  // b/15676312, but parent stream will always be reset to 0.
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(3, 123, false));
  EXPECT_TRUE(scheduler_.StreamRegistered(2));
  EXPECT_FALSE(scheduler_.StreamRegistered(3));
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamPrecedence(2).parent_id());
}

TEST_F(PriorityWriteSchedulerTest, GetStreamPrecedence) {
  // Unknown streams tolerated due to b/15676312. However, return lowest
  // priority.
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(1).spdy3_priority());

  scheduler_.RegisterStream(1, SpdyStreamPrecedence(3));
  EXPECT_TRUE(scheduler_.GetStreamPrecedence(1).is_spdy3_priority());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  // Redundant registration shouldn't change stream priority.
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(1, SpdyStreamPrecedence(4)),
                  "Stream 1 already registered");
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(5));
  EXPECT_EQ(5, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  // Toggling ready state shouldn't change stream priority.
  scheduler_.MarkStreamReady(1, true);
  EXPECT_EQ(5, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  // Test changing priority of ready stream.
  EXPECT_EQ(1u, peer_.NumReadyStreams(5));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(6));
  EXPECT_EQ(6, scheduler_.GetStreamPrecedence(1).spdy3_priority());
  EXPECT_EQ(0u, peer_.NumReadyStreams(5));
  EXPECT_EQ(1u, peer_.NumReadyStreams(6));

  EXPECT_EQ(1u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(6, scheduler_.GetStreamPrecedence(1).spdy3_priority());

  scheduler_.UnregisterStream(1);
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(1).spdy3_priority());
}

TEST_F(PriorityWriteSchedulerTest, PopNextReadyStreamAndPrecedence) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(1, true);
  EXPECT_EQ(std::make_tuple(1u, SpdyStreamPrecedence(3)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.UnregisterStream(1);
}

TEST_F(PriorityWriteSchedulerTest, UpdateStreamPrecedence) {
  // For the moment, updating stream precedence on a non-registered stream
  // should have no effect. In the future, it will lazily cause the stream to
  // be registered (b/15676312).
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(3).spdy3_priority());
  EXPECT_FALSE(scheduler_.StreamRegistered(3));
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(1));
  EXPECT_FALSE(scheduler_.StreamRegistered(3));
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(3).spdy3_priority());

  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1));
  EXPECT_EQ(1, scheduler_.GetStreamPrecedence(3).spdy3_priority());
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(2));
  EXPECT_EQ(2, scheduler_.GetStreamPrecedence(3).spdy3_priority());

  // Updating priority of stream to current priority value is valid, but has no
  // effect.
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(2));
  EXPECT_EQ(2, scheduler_.GetStreamPrecedence(3).spdy3_priority());

  // Even though stream 4 is marked ready after stream 5, it should be returned
  // first by PopNextReadyStream() since it has higher priority.
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1));
  scheduler_.MarkStreamReady(3, false);  // priority 2
  EXPECT_TRUE(scheduler_.IsStreamReady(3));
  scheduler_.MarkStreamReady(4, false);  // priority 1
  EXPECT_TRUE(scheduler_.IsStreamReady(4));
  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  EXPECT_FALSE(scheduler_.IsStreamReady(4));
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  EXPECT_FALSE(scheduler_.IsStreamReady(3));

  // Verify that lowering priority of stream 4 causes it to be returned later
  // by PopNextReadyStream().
  scheduler_.MarkStreamReady(3, false);  // priority 2
  scheduler_.MarkStreamReady(4, false);  // priority 1
  scheduler_.UpdateStreamPrecedence(4, SpdyStreamPrecedence(3));
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());

  scheduler_.UnregisterStream(3);
}

TEST_F(PriorityWriteSchedulerTest,
       UpdateStreamPrecedenceWithHttp2StreamDependency) {
  // Unknown streams tolerated due to b/15676312, but should have no effect.
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 100, false));
  EXPECT_FALSE(scheduler_.StreamRegistered(3));

  scheduler_.RegisterStream(3, SpdyStreamPrecedence(3));
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 100, false));
  EXPECT_TRUE(scheduler_.GetStreamPrecedence(3).is_spdy3_priority());
  EXPECT_EQ(4, scheduler_.GetStreamPrecedence(3).spdy3_priority());

  scheduler_.UnregisterStream(3);
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 100, false));
  EXPECT_FALSE(scheduler_.StreamRegistered(3));
}

TEST_F(PriorityWriteSchedulerTest, MarkStreamReadyBack) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_SPDY_BUG(scheduler_.MarkStreamReady(1, false),
                  "Stream 1 not registered");
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");

  // Add a bunch of ready streams to tail of per-priority lists.
  // Expected order: (P2) 4, (P3) 1, 2, 3, (P5) 5.
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(1, false);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(2, false);
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(3, false);
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2));
  scheduler_.MarkStreamReady(4, false);
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(5));
  scheduler_.MarkStreamReady(5, false);

  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(1u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");
}

TEST_F(PriorityWriteSchedulerTest, MarkStreamReadyFront) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_SPDY_BUG(scheduler_.MarkStreamReady(1, true),
                  "Stream 1 not registered");
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");

  // Add a bunch of ready streams to head of per-priority lists.
  // Expected order: (P2) 4, (P3) 3, 2, 1, (P5) 5
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(1, true);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(2, true);
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(3));
  scheduler_.MarkStreamReady(3, true);
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2));
  scheduler_.MarkStreamReady(4, true);
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(5));
  scheduler_.MarkStreamReady(5, true);

  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(1u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");
}

TEST_F(PriorityWriteSchedulerTest, MarkStreamReadyBackAndFront) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(4));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(3));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(3));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(3));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(4));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(1));

  // Add a bunch of ready streams to per-priority lists, with variety of adding
  // at head and tail.
  // Expected order: (P1) 6, (P3) 4, 2, 3, (P4) 1, 5
  scheduler_.MarkStreamReady(1, true);
  scheduler_.MarkStreamReady(2, true);
  scheduler_.MarkStreamReady(3, false);
  scheduler_.MarkStreamReady(4, true);
  scheduler_.MarkStreamReady(5, false);
  scheduler_.MarkStreamReady(6, true);

  EXPECT_EQ(6u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(1u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");
}

TEST_F(PriorityWriteSchedulerTest, MarkStreamNotReady) {
  // Verify ready state reflected in NumReadyStreams().
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(1));
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());
  scheduler_.MarkStreamReady(1, false);
  EXPECT_EQ(1u, scheduler_.NumReadyStreams());
  scheduler_.MarkStreamNotReady(1);
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());

  // Empty pop should fail.
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");

  // Tolerate redundant marking of a stream as not ready.
  scheduler_.MarkStreamNotReady(1);
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());

  // Should only be able to mark registered streams.
  EXPECT_SPDY_BUG(scheduler_.MarkStreamNotReady(3), "Stream 3 not registered");
}

TEST_F(PriorityWriteSchedulerTest, UnregisterRemovesStream) {
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(4));
  scheduler_.MarkStreamReady(3, false);
  EXPECT_EQ(1u, scheduler_.NumReadyStreams());

  // Unregistering a stream should remove it from set of ready streams.
  scheduler_.UnregisterStream(3);
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());
  EXPECT_SPDY_BUG(EXPECT_EQ(0u, scheduler_.PopNextReadyStream()),
                  "No ready streams available");
}

TEST_F(PriorityWriteSchedulerTest, ShouldYield) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(1));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(4));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(4));
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(7));

  // Make sure we don't yield when the list is empty.
  EXPECT_FALSE(scheduler_.ShouldYield(1));

  // Add a low priority stream.
  scheduler_.MarkStreamReady(4, false);
  // 4 should not yield to itself.
  EXPECT_FALSE(scheduler_.ShouldYield(4));
  // 7 should yield as 4 is blocked and a higher priority.
  EXPECT_TRUE(scheduler_.ShouldYield(7));
  // 5 should yield to 4 as they are the same priority.
  EXPECT_TRUE(scheduler_.ShouldYield(5));
  // 1 should not yield as 1 is higher priority.
  EXPECT_FALSE(scheduler_.ShouldYield(1));

  // Add a second stream in that priority class.
  scheduler_.MarkStreamReady(5, false);
  // 4 and 5 are both blocked, but 4 is at the front so should not yield.
  EXPECT_FALSE(scheduler_.ShouldYield(4));
  EXPECT_TRUE(scheduler_.ShouldYield(5));
}

TEST_F(PriorityWriteSchedulerTest, GetLatestEventWithPrecedence) {
  EXPECT_SPDY_BUG(scheduler_.RecordStreamEventTime(3, 5),
                  "Stream 3 not registered");
  EXPECT_SPDY_BUG(EXPECT_EQ(0, scheduler_.GetLatestEventWithPrecedence(4)),
                  "Stream 4 not registered");

  for (int i = 1; i < 5; ++i) {
    scheduler_.RegisterStream(i, SpdyStreamPrecedence(i));
  }
  for (int i = 1; i < 5; ++i) {
    EXPECT_EQ(0, scheduler_.GetLatestEventWithPrecedence(i));
  }
  for (int i = 1; i < 5; ++i) {
    scheduler_.RecordStreamEventTime(i, i * 100);
  }
  for (int i = 1; i < 5; ++i) {
    EXPECT_EQ((i - 1) * 100, scheduler_.GetLatestEventWithPrecedence(i));
  }
}

}  // namespace
}  // namespace test
}  // namespace spdy
