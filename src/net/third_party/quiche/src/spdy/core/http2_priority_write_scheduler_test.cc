// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/http2_priority_write_scheduler.h"

#include <initializer_list>

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_test_helpers.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

namespace spdy {

namespace test {

template <typename StreamIdType>
class Http2PriorityWriteSchedulerPeer {
 public:
  explicit Http2PriorityWriteSchedulerPeer(
      Http2PriorityWriteScheduler<StreamIdType>* scheduler)
      : scheduler_(scheduler) {}

  int TotalChildWeights(StreamIdType stream_id) const {
    return scheduler_->FindStream(stream_id)->total_child_weights;
  }

  bool ValidateInvariants() const {
    return scheduler_->ValidateInvariantsForTests();
  }

 private:
  Http2PriorityWriteScheduler<StreamIdType>* scheduler_;
};

class Http2PriorityWriteSchedulerTest : public QuicheTest {
 protected:
  typedef uint32_t SpdyStreamId;

  Http2PriorityWriteSchedulerTest() : peer_(&scheduler_) {}

  Http2PriorityWriteScheduler<SpdyStreamId> scheduler_;
  Http2PriorityWriteSchedulerPeer<SpdyStreamId> peer_;
};

TEST_F(Http2PriorityWriteSchedulerTest, RegisterAndUnregisterStreams) {
  EXPECT_EQ(1u, scheduler_.NumRegisteredStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(0));
  EXPECT_FALSE(scheduler_.StreamRegistered(1));

  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  EXPECT_EQ(2u, scheduler_.NumRegisteredStreams());
  ASSERT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(100, scheduler_.GetStreamPrecedence(1).weight());
  EXPECT_FALSE(scheduler_.StreamRegistered(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));

  scheduler_.RegisterStream(5, SpdyStreamPrecedence(0, 50, false));
  // Should not be able to add a stream with an id that already exists.
  EXPECT_SPDY_BUG(
      scheduler_.RegisterStream(5, SpdyStreamPrecedence(1, 50, false)),
      "Stream 5 already registered");
  EXPECT_EQ(3u, scheduler_.NumRegisteredStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  ASSERT_TRUE(scheduler_.StreamRegistered(5));
  EXPECT_EQ(50, scheduler_.GetStreamPrecedence(5).weight());
  EXPECT_FALSE(scheduler_.StreamRegistered(13));

  scheduler_.RegisterStream(13, SpdyStreamPrecedence(5, 130, true));
  EXPECT_EQ(4u, scheduler_.NumRegisteredStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_TRUE(scheduler_.StreamRegistered(5));
  ASSERT_TRUE(scheduler_.StreamRegistered(13));
  EXPECT_EQ(130, scheduler_.GetStreamPrecedence(13).weight());
  EXPECT_EQ(5u, scheduler_.GetStreamPrecedence(13).parent_id());

  scheduler_.UnregisterStream(5);
  // Cannot remove a stream that has already been removed.
  EXPECT_SPDY_BUG(scheduler_.UnregisterStream(5), "Stream 5 not registered");
  EXPECT_EQ(3u, scheduler_.NumRegisteredStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_FALSE(scheduler_.StreamRegistered(5));
  EXPECT_TRUE(scheduler_.StreamRegistered(13));
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamPrecedence(13).parent_id());

  // The parent stream 19 doesn't exist, so this should use 0 as parent stream:
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(19, 70, false));
  EXPECT_TRUE(scheduler_.StreamRegistered(7));
  EXPECT_EQ(0u, scheduler_.GetStreamPrecedence(7).parent_id());
  // Now stream 7 already exists, so this should fail:
  EXPECT_SPDY_BUG(
      scheduler_.RegisterStream(7, SpdyStreamPrecedence(1, 70, false)),
      "Stream 7 already registered");
  // Try adding a second child to stream 13:
  scheduler_.RegisterStream(17, SpdyStreamPrecedence(13, 170, false));

  scheduler_.UpdateStreamPrecedence(17, SpdyStreamPrecedence(13, 150, false));
  EXPECT_EQ(150, scheduler_.GetStreamPrecedence(17).weight());

  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, RegisterStreamWithSpdy3Priority) {
  EXPECT_FALSE(scheduler_.StreamRegistered(1));
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(3));
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());
  EXPECT_EQ(147, scheduler_.GetStreamPrecedence(1).weight());
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamPrecedence(1).parent_id());
  EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());

  EXPECT_SPDY_BUG(scheduler_.RegisterStream(1, SpdyStreamPrecedence(4)),
                  "Stream 1 already registered");
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(1).spdy3_priority());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamWeight) {
  // Unknown streams tolerated due to b/15676312.
  EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamPrecedence(3).weight());
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 130, true));
  EXPECT_EQ(130, scheduler_.GetStreamPrecedence(3).weight());
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 50, true));
  EXPECT_EQ(50, scheduler_.GetStreamPrecedence(3).weight());
  scheduler_.UnregisterStream(3);
  EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamPrecedence(3).weight());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamPriority) {
  // Unknown streams tolerated due to b/15676312.
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(3).spdy3_priority());
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 130, true));
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(3).spdy3_priority());
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 50, true));
  EXPECT_EQ(5, scheduler_.GetStreamPrecedence(3).spdy3_priority());
  scheduler_.UnregisterStream(3);
  EXPECT_EQ(kV3LowestPriority,
            scheduler_.GetStreamPrecedence(3).spdy3_priority());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamParent) {
  // Unknown streams tolerated due to b/15676312.
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamPrecedence(3).parent_id());
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(2, 30, false));
  EXPECT_EQ(2u, scheduler_.GetStreamPrecedence(3).parent_id());
  scheduler_.UnregisterStream(3);
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamPrecedence(3).parent_id());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamChildren) {
  EXPECT_SPDY_BUG(EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty()),
                  "Stream 7 not registered");
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(0, 70, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty());
  scheduler_.RegisterStream(9, SpdyStreamPrecedence(7, 90, false));
  scheduler_.RegisterStream(15, SpdyStreamPrecedence(7, 150, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(7), UnorderedElementsAre(9, 15));
  scheduler_.UnregisterStream(7);
  EXPECT_SPDY_BUG(EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty()),
                  "Stream 7 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamWeight) {
  EXPECT_SPDY_BUG(
      scheduler_.UpdateStreamPrecedence(0, SpdyStreamPrecedence(0, 10, false)),
      "Cannot set precedence of root stream");

  // For the moment, updating stream precedence on a non-registered stream
  // should have no effect. In the future, it will lazily cause the stream to
  // be registered (b/15676312).
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 10, false));
  EXPECT_FALSE(scheduler_.StreamRegistered(3));

  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 10, false));
  scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 20, false));
  EXPECT_EQ(20, scheduler_.GetStreamPrecedence(3).weight());
  ASSERT_TRUE(peer_.ValidateInvariants());

  EXPECT_SPDY_BUG(
      scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 500, false)),
      "Invalid weight: 500");
  EXPECT_EQ(kHttp2MaxStreamWeight, scheduler_.GetStreamPrecedence(3).weight());
  EXPECT_SPDY_BUG(
      scheduler_.UpdateStreamPrecedence(3, SpdyStreamPrecedence(0, 0, false)),
      "Invalid weight: 0");
  EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamPrecedence(3).weight());
  ASSERT_TRUE(peer_.ValidateInvariants());

  scheduler_.UnregisterStream(3);
}

// Basic case of reparenting a subtree.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentBasicNonExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \
    3   4
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(2, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// Basic case of reparenting a subtree.  Result here is the same as the
// non-exclusive case.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentBasicExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \
    3   4
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(2, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// We can't set the parent of a nonexistent stream, or set the parent to a
// nonexistent stream.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentNonexistent) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  for (bool exclusive : {true, false}) {
    // For the moment, updating stream precedence on a non-registered stream or
    // attempting to set parent to a nonexistent stream should have no
    // effect. In the future, it will lazily cause the stream(s) to be
    // registered (b/15676312).

    // No-op: parent stream 3 not registered
    scheduler_.UpdateStreamPrecedence(1,
                                      SpdyStreamPrecedence(3, 100, exclusive));

    // No-op: stream 4 not registered
    scheduler_.UpdateStreamPrecedence(4,
                                      SpdyStreamPrecedence(2, 100, exclusive));

    // No-op: stream 3 not registered
    scheduler_.UpdateStreamPrecedence(3,
                                      SpdyStreamPrecedence(4, 100, exclusive));

    EXPECT_THAT(scheduler_.GetStreamChildren(0), UnorderedElementsAre(1, 2));
    EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());
    EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
    EXPECT_FALSE(scheduler_.StreamRegistered(3));
    EXPECT_FALSE(scheduler_.StreamRegistered(4));
  }
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// We should be able to add multiple children to streams.
TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentMultipleChildrenNonExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \   \
    3   4   5
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(2, 100, false));
  scheduler_.UpdateStreamPrecedence(2, SpdyStreamPrecedence(1, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentMultipleChildrenExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \   \
    3   4   5
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(2, 100, false));
  scheduler_.UpdateStreamPrecedence(2, SpdyStreamPrecedence(1, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), UnorderedElementsAre(3, 4, 5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToChildNonExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
      |
      4
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(2, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), ElementsAre(3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), UnorderedElementsAre(1, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToChildExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
      |
      4
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(2, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentToGrandchildNonExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
     / \
    4   5
    |
    6
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(4, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(4, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), UnorderedElementsAre(1, 6));
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentToGrandchildExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
     / \
    4   5
    |
    6
   */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(4, 100, false));
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(4, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3, 6));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, RegisterStreamParentExclusive) {
  /*  0
     / \
    1   2
 */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  /*  0
      |
      3
     / \
    1   2
  */
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(3));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), UnorderedElementsAre(1, 2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentExclusive) {
  /*  0
     /|\
    1 2 3
 */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, false));
  /*  0
      |
      1
     / \
    2   3
  */
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(0, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentExclusive2) {
  /*   0
       |
       1
      / \
     2   3
        / \
       4   5
       |
       6
 */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(3, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(3, 100, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(4, 100, false));
  // Update stream 1's parent to 4 exclusive.
  /*  0
      |
      4
      |
      1
     /|\
    2 3 6
      |
      5
  */
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(4, 100, true));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(4), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3, 6));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(3), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentNonExclusive) {
  /*   0
       |
       1
      / \
     2   3
        / \
       4   5
       |
       6
 */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(3, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(3, 100, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(4, 100, false));
  // Update stream 1's parent to 4.
  /*  0
      |
      4
     / \
    6   1
       / \
      2   3
          |
          5
  */
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(4, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(4), UnorderedElementsAre(6, 1));
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(3), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToParent) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(1, 100, false));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  for (bool exclusive : {true, false}) {
    scheduler_.UpdateStreamPrecedence(2,
                                      SpdyStreamPrecedence(1, 100, exclusive));
    EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
    EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2));
    EXPECT_THAT(scheduler_.GetStreamChildren(2), UnorderedElementsAre(3));
    EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  }
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToSelf) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  EXPECT_SPDY_BUG(
      scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(1, 100, false)),
      "Cannot set stream to be its own parent");
  EXPECT_SPDY_BUG(
      scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(1, 100, true)),
      "Cannot set stream to be its own parent");
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, BlockAndUnblock) {
  /* Create the tree.

             0
           / | \
          /  |  \
         1   2   3
        / \   \   \
       4   5   6   7
      /|  / \  |   |\
     8 9 10 11 12 13 14
    / \
   15 16

  */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(8, SpdyStreamPrecedence(4, 100, false));
  scheduler_.RegisterStream(9, SpdyStreamPrecedence(4, 100, false));
  scheduler_.RegisterStream(10, SpdyStreamPrecedence(5, 100, false));
  scheduler_.RegisterStream(11, SpdyStreamPrecedence(5, 100, false));
  scheduler_.RegisterStream(15, SpdyStreamPrecedence(8, 100, false));
  scheduler_.RegisterStream(16, SpdyStreamPrecedence(8, 100, false));
  scheduler_.RegisterStream(12, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(2, 100, true));
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(13, SpdyStreamPrecedence(7, 100, true));
  scheduler_.RegisterStream(14, SpdyStreamPrecedence(7, 100, false));
  scheduler_.UpdateStreamPrecedence(7, SpdyStreamPrecedence(3, 100, false));
  EXPECT_EQ(0u, scheduler_.GetStreamPrecedence(1).parent_id());
  EXPECT_EQ(0u, scheduler_.GetStreamPrecedence(2).parent_id());
  EXPECT_EQ(0u, scheduler_.GetStreamPrecedence(3).parent_id());
  EXPECT_EQ(1u, scheduler_.GetStreamPrecedence(4).parent_id());
  EXPECT_EQ(1u, scheduler_.GetStreamPrecedence(5).parent_id());
  EXPECT_EQ(2u, scheduler_.GetStreamPrecedence(6).parent_id());
  EXPECT_EQ(3u, scheduler_.GetStreamPrecedence(7).parent_id());
  EXPECT_EQ(4u, scheduler_.GetStreamPrecedence(8).parent_id());
  EXPECT_EQ(4u, scheduler_.GetStreamPrecedence(9).parent_id());
  EXPECT_EQ(5u, scheduler_.GetStreamPrecedence(10).parent_id());
  EXPECT_EQ(5u, scheduler_.GetStreamPrecedence(11).parent_id());
  EXPECT_EQ(6u, scheduler_.GetStreamPrecedence(12).parent_id());
  EXPECT_EQ(7u, scheduler_.GetStreamPrecedence(13).parent_id());
  EXPECT_EQ(7u, scheduler_.GetStreamPrecedence(14).parent_id());
  EXPECT_EQ(8u, scheduler_.GetStreamPrecedence(15).parent_id());
  EXPECT_EQ(8u, scheduler_.GetStreamPrecedence(16).parent_id());
  ASSERT_TRUE(peer_.ValidateInvariants());

  EXPECT_EQ(peer_.TotalChildWeights(0),
            scheduler_.GetStreamPrecedence(1).weight() +
                scheduler_.GetStreamPrecedence(2).weight() +
                scheduler_.GetStreamPrecedence(3).weight());
  EXPECT_EQ(peer_.TotalChildWeights(3),
            scheduler_.GetStreamPrecedence(7).weight());
  EXPECT_EQ(peer_.TotalChildWeights(7),
            scheduler_.GetStreamPrecedence(13).weight() +
                scheduler_.GetStreamPrecedence(14).weight());
  EXPECT_EQ(peer_.TotalChildWeights(13), 0);
  EXPECT_EQ(peer_.TotalChildWeights(14), 0);

  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, HasReadyStreams) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 10, false));
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  scheduler_.MarkStreamReady(1, false);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  EXPECT_TRUE(scheduler_.IsStreamReady(1));
  scheduler_.MarkStreamNotReady(1);
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  EXPECT_FALSE(scheduler_.IsStreamReady(1));
  scheduler_.MarkStreamReady(1, true);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  EXPECT_TRUE(scheduler_.IsStreamReady(1));
  scheduler_.UnregisterStream(1);
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  ASSERT_TRUE(peer_.ValidateInvariants());
  EXPECT_SPDY_BUG(scheduler_.IsStreamReady(1), "Stream 1 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, CalculateRoundedWeights) {
  /* Create the tree.

           0
          / \
         1   2
       /| |\  |\
      8 3 4 5 6 7
  */
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 10, true));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 5, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(2, 1, false));
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(2, 1, false));
  scheduler_.RegisterStream(8, SpdyStreamPrecedence(1, 1, false));

  // Remove higher-level streams.
  scheduler_.UnregisterStream(1);
  scheduler_.UnregisterStream(2);

  // 3.3 rounded down = 3.
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(3).weight());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(4).weight());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(5).weight());
  // 2.5 rounded up = 3.
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(6).weight());
  EXPECT_EQ(3, scheduler_.GetStreamPrecedence(7).weight());
  // 0 is not a valid weight, so round up to 1.
  EXPECT_EQ(1, scheduler_.GetStreamPrecedence(8).weight());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetLatestEventWithPrecedence) {
  EXPECT_SPDY_BUG(scheduler_.RecordStreamEventTime(3, 5),
                  "Stream 3 not registered");
  EXPECT_SPDY_BUG(EXPECT_EQ(0, scheduler_.GetLatestEventWithPrecedence(4)),
                  "Stream 4 not registered");

  for (int i = 1; i < 5; ++i) {
    int weight = SpdyStreamPrecedence(i).weight();
    scheduler_.RegisterStream(i, SpdyStreamPrecedence(0, weight, false));
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

// Add ready streams at front and back.
TEST_F(Http2PriorityWriteSchedulerTest, MarkReadyFrontAndBack) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 10, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(0, 30, false));

  for (int i = 1; i < 6; ++i) {
    scheduler_.MarkStreamReady(i, false);
  }
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(2, false);
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(3, false);
  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(4, false);
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(5, false);
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
}

// Add ready streams at front and back and pop them with
// PopNextReadyStreamAndPrecedence.
TEST_F(Http2PriorityWriteSchedulerTest, PopNextReadyStreamAndPrecedence) {
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 10, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(0, 20, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(0, 30, false));

  for (int i = 1; i < 6; ++i) {
    scheduler_.MarkStreamReady(i, false);
  }
  EXPECT_EQ(std::make_tuple(5, SpdyStreamPrecedence(0, 30, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  EXPECT_EQ(std::make_tuple(2, SpdyStreamPrecedence(0, 20, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.MarkStreamReady(2, false);
  EXPECT_EQ(std::make_tuple(3, SpdyStreamPrecedence(0, 20, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.MarkStreamReady(3, false);
  EXPECT_EQ(std::make_tuple(4, SpdyStreamPrecedence(0, 20, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.MarkStreamReady(4, false);
  EXPECT_EQ(std::make_tuple(2, SpdyStreamPrecedence(0, 20, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(std::make_tuple(2, SpdyStreamPrecedence(0, 20, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
  scheduler_.MarkStreamReady(5, false);
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(std::make_tuple(5, SpdyStreamPrecedence(0, 30, false)),
            scheduler_.PopNextReadyStreamAndPrecedence());
}

TEST_F(Http2PriorityWriteSchedulerTest, ShouldYield) {
  /*
         0
        /|\
       1 2 3
      /|\ \
     4 5 6 7
       |
       8

  */
  scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, false));
  scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
  scheduler_.RegisterStream(5, SpdyStreamPrecedence(1, 200, false));
  scheduler_.RegisterStream(6, SpdyStreamPrecedence(1, 255, false));
  scheduler_.RegisterStream(7, SpdyStreamPrecedence(2, 100, false));
  scheduler_.RegisterStream(8, SpdyStreamPrecedence(5, 100, false));

  scheduler_.MarkStreamReady(5, false);

  for (int i = 1; i <= 8; ++i) {
    // Verify only 4 and 8 should yield to 5.
    if (i == 4 || i == 8) {
      EXPECT_TRUE(scheduler_.ShouldYield(i)) << "stream_id: " << i;
    } else {
      EXPECT_FALSE(scheduler_.ShouldYield(i)) << "stream_id: " << i;
    }
  }

  // Marks streams 1 and 2 ready.
  scheduler_.MarkStreamReady(1, false);
  scheduler_.MarkStreamReady(2, false);
  // 1 should not yield.
  EXPECT_FALSE(scheduler_.ShouldYield(1));
  // Verify 2 should yield to 1.
  EXPECT_TRUE(scheduler_.ShouldYield(2));
}

class PopNextReadyStreamTest : public Http2PriorityWriteSchedulerTest {
 protected:
  void SetUp() override {
    /* Create the tree.

             0
            /|\
           1 2 3
          /| |\
         4 5 6 7
        /
       8

    */
    scheduler_.RegisterStream(1, SpdyStreamPrecedence(0, 100, false));
    scheduler_.RegisterStream(2, SpdyStreamPrecedence(0, 100, false));
    scheduler_.RegisterStream(3, SpdyStreamPrecedence(0, 100, false));
    scheduler_.RegisterStream(4, SpdyStreamPrecedence(1, 100, false));
    scheduler_.RegisterStream(5, SpdyStreamPrecedence(1, 100, false));
    scheduler_.RegisterStream(6, SpdyStreamPrecedence(2, 100, false));
    scheduler_.RegisterStream(7, SpdyStreamPrecedence(2, 100, false));
    scheduler_.RegisterStream(8, SpdyStreamPrecedence(4, 100, false));

    // Set all nodes ready to write.
    for (SpdyStreamId id = 1; id <= 8; ++id) {
      scheduler_.MarkStreamReady(id, false);
    }
  }

  AssertionResult PopNextReturnsCycle(
      std::initializer_list<SpdyStreamId> stream_ids) {
    int count = 0;
    const int kNumCyclesToCheck = 2;
    for (int i = 0; i < kNumCyclesToCheck; i++) {
      for (SpdyStreamId expected_id : stream_ids) {
        SpdyStreamId next_id = scheduler_.PopNextReadyStream();
        scheduler_.MarkStreamReady(next_id, false);
        if (next_id != expected_id) {
          return AssertionFailure() << "Pick " << count << ": expected stream "
                                    << expected_id << " instead of " << next_id;
        }
        if (!peer_.ValidateInvariants()) {
          return AssertionFailure() << "ValidateInvariants failed";
        }
        ++count;
      }
    }
    return AssertionSuccess();
  }
};

// When all streams are schedulable, only top-level streams should be returned.
TEST_F(PopNextReadyStreamTest, NoneBlocked) {
  EXPECT_TRUE(PopNextReturnsCycle({1, 2, 3}));
}

// When a parent stream is blocked, its children should be scheduled, if
// priorities allow.
TEST_F(PopNextReadyStreamTest, SingleStreamBlocked) {
  scheduler_.MarkStreamNotReady(1);

  // Round-robin only across 2 and 3, since children of 1 have lower priority.
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make children of 1 have equal priority as 2 and 3, after which they should
  // be returned as well.
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(0, 200, false));
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 2, 3}));
}

// Block multiple levels of streams.
TEST_F(PopNextReadyStreamTest, MultiLevelBlocked) {
  for (SpdyStreamId stream_id : {1, 4, 5}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  // Round-robin only across 2 and 3, since children of 1 have lower priority.
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make 8 have equal priority as 2 and 3.
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(0, 200, false));
  EXPECT_TRUE(PopNextReturnsCycle({8, 2, 3}));
}

// A removed stream shouldn't be scheduled.
TEST_F(PopNextReadyStreamTest, RemoveStream) {
  scheduler_.UnregisterStream(1);

  // Round-robin only across 2 and 3, since previous children of 1 have lower
  // priority (the weight of 4 and 5 is scaled down when they are elevated to
  // siblings of 2 and 3).
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make previous children of 1 have equal priority as 2 and 3.
  scheduler_.UpdateStreamPrecedence(4, SpdyStreamPrecedence(0, 100, false));
  scheduler_.UpdateStreamPrecedence(5, SpdyStreamPrecedence(0, 100, false));
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 2, 3}));
}

// Block an entire subtree.
TEST_F(PopNextReadyStreamTest, SubtreeBlocked) {
  for (SpdyStreamId stream_id : {1, 4, 5, 8}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));
}

// If all parent streams are blocked, children should be returned.
TEST_F(PopNextReadyStreamTest, ParentsBlocked) {
  for (SpdyStreamId stream_id : {1, 2, 3}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 6, 7}));
}

// Unblocking streams should make them schedulable.
TEST_F(PopNextReadyStreamTest, BlockAndUnblock) {
  EXPECT_TRUE(PopNextReturnsCycle({1, 2, 3}));
  scheduler_.MarkStreamNotReady(2);
  EXPECT_TRUE(PopNextReturnsCycle({1, 3}));
  scheduler_.MarkStreamReady(2, false);
  // Cycle order permuted since 2 effectively appended at tail.
  EXPECT_TRUE(PopNextReturnsCycle({1, 3, 2}));
}

// Block nodes in multiple subtrees.
TEST_F(PopNextReadyStreamTest, ScatteredBlocked) {
  for (SpdyStreamId stream_id : {1, 2, 6, 7}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  // Only 3 returned, since of remaining streams it has highest priority.
  EXPECT_TRUE(PopNextReturnsCycle({3}));

  // Make children of 1 have priority equal to 3.
  scheduler_.UpdateStreamPrecedence(1, SpdyStreamPrecedence(0, 200, false));
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 3}));

  // When 4 is blocked, its child 8 should take its place, since it has same
  // priority.
  scheduler_.MarkStreamNotReady(4);
  EXPECT_TRUE(PopNextReturnsCycle({8, 5, 3}));
}

}  // namespace test
}  // namespace spdy
