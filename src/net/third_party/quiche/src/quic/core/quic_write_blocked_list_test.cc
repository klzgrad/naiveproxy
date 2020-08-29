// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using spdy::kHttp2DefaultStreamWeight;
using spdy::kV3HighestPriority;
using spdy::kV3LowestPriority;

namespace quic {
namespace test {
namespace {

const bool kExclusiveBit = true;

class QuicWriteBlockedListTest : public QuicTestWithParam<bool> {
 public:
  QuicWriteBlockedListTest()
      : write_blocked_list_(AllSupportedVersions()[0].transport_version) {
    if (GetParam()) {
      write_blocked_list_.SwitchWriteScheduler(
          spdy::WriteSchedulerType::HTTP2,
          AllSupportedVersions()[0].transport_version);
    }
  }

 protected:
  QuicWriteBlockedList write_blocked_list_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicWriteBlockedListTest,
                         ::testing::Bool(),
                         ::testing::PrintToStringParamName());

TEST_P(QuicWriteBlockedListTest, PriorityOrder) {
  if (GetParam()) {
    /*
         0
         |
         23
         |
         17
         |
         40
    */
    write_blocked_list_.RegisterStream(
        17, false,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        40, false,
        spdy::SpdyStreamPrecedence(17, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        23, false,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        1, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        3, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    // Mark streams blocked in roughly reverse priority order, and
    // verify that streams are sorted.
    write_blocked_list_.RegisterStream(
        40, false, spdy::SpdyStreamPrecedence(kV3LowestPriority));
    write_blocked_list_.RegisterStream(
        23, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        17, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        1, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        3, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }

  write_blocked_list_.AddStream(40);
  EXPECT_TRUE(write_blocked_list_.IsStreamBlocked(40));
  write_blocked_list_.AddStream(23);
  EXPECT_TRUE(write_blocked_list_.IsStreamBlocked(23));
  write_blocked_list_.AddStream(17);
  EXPECT_TRUE(write_blocked_list_.IsStreamBlocked(17));
  write_blocked_list_.AddStream(3);
  EXPECT_TRUE(write_blocked_list_.IsStreamBlocked(3));
  write_blocked_list_.AddStream(1);
  EXPECT_TRUE(write_blocked_list_.IsStreamBlocked(1));

  EXPECT_EQ(5u, write_blocked_list_.NumBlockedStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedSpecialStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedDataStreams());
  // The Crypto stream is highest priority.
  EXPECT_EQ(1u, write_blocked_list_.PopFront());
  EXPECT_EQ(1u, write_blocked_list_.NumBlockedSpecialStreams());
  EXPECT_FALSE(write_blocked_list_.IsStreamBlocked(1));
  // Followed by the Headers stream.
  EXPECT_EQ(3u, write_blocked_list_.PopFront());
  EXPECT_EQ(0u, write_blocked_list_.NumBlockedSpecialStreams());
  EXPECT_FALSE(write_blocked_list_.IsStreamBlocked(3));
  // Streams with same priority are popped in the order they were inserted.
  EXPECT_EQ(23u, write_blocked_list_.PopFront());
  EXPECT_FALSE(write_blocked_list_.IsStreamBlocked(23));
  EXPECT_EQ(17u, write_blocked_list_.PopFront());
  EXPECT_FALSE(write_blocked_list_.IsStreamBlocked(17));
  // Low priority stream appears last.
  EXPECT_EQ(40u, write_blocked_list_.PopFront());
  EXPECT_FALSE(write_blocked_list_.IsStreamBlocked(40));

  EXPECT_EQ(0u, write_blocked_list_.NumBlockedStreams());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedDataStreams());
}

TEST_P(QuicWriteBlockedListTest, CryptoStream) {
  if (GetParam()) {
    write_blocked_list_.RegisterStream(
        1, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    write_blocked_list_.RegisterStream(
        1, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }
  write_blocked_list_.AddStream(1);

  EXPECT_EQ(1u, write_blocked_list_.NumBlockedStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_EQ(1u, write_blocked_list_.PopFront());
  EXPECT_EQ(0u, write_blocked_list_.NumBlockedStreams());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedSpecialStream());
}

TEST_P(QuicWriteBlockedListTest, HeadersStream) {
  if (GetParam()) {
    write_blocked_list_.RegisterStream(
        3, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    write_blocked_list_.RegisterStream(
        3, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }
  write_blocked_list_.AddStream(3);

  EXPECT_EQ(1u, write_blocked_list_.NumBlockedStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_EQ(3u, write_blocked_list_.PopFront());
  EXPECT_EQ(0u, write_blocked_list_.NumBlockedStreams());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedSpecialStream());
}

TEST_P(QuicWriteBlockedListTest, VerifyHeadersStream) {
  if (GetParam()) {
    write_blocked_list_.RegisterStream(
        5, false,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        3, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    write_blocked_list_.RegisterStream(
        5, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        3, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }
  write_blocked_list_.AddStream(5);
  write_blocked_list_.AddStream(3);

  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedDataStreams());
  // In newer QUIC versions, there is a headers stream which is
  // higher priority than data streams.
  EXPECT_EQ(3u, write_blocked_list_.PopFront());
  EXPECT_EQ(5u, write_blocked_list_.PopFront());
  EXPECT_EQ(0u, write_blocked_list_.NumBlockedStreams());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedSpecialStream());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedDataStreams());
}

TEST_P(QuicWriteBlockedListTest, NoDuplicateEntries) {
  // Test that QuicWriteBlockedList doesn't allow duplicate entries.
  // Try to add a stream to the write blocked list multiple times at the same
  // priority.
  const QuicStreamId kBlockedId = 3 + 2;
  if (GetParam()) {
    write_blocked_list_.RegisterStream(
        kBlockedId, false,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    write_blocked_list_.RegisterStream(
        kBlockedId, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }
  write_blocked_list_.AddStream(kBlockedId);
  write_blocked_list_.AddStream(kBlockedId);
  write_blocked_list_.AddStream(kBlockedId);

  // This should only result in one blocked stream being added.
  EXPECT_EQ(1u, write_blocked_list_.NumBlockedStreams());
  EXPECT_TRUE(write_blocked_list_.HasWriteBlockedDataStreams());

  // There should only be one stream to pop off the front.
  EXPECT_EQ(kBlockedId, write_blocked_list_.PopFront());
  EXPECT_EQ(0u, write_blocked_list_.NumBlockedStreams());
  EXPECT_FALSE(write_blocked_list_.HasWriteBlockedDataStreams());
}

TEST_P(QuicWriteBlockedListTest, BatchingWrites) {
  if (GetParam()) {
    return;
  }
  const QuicStreamId id1 = 3 + 2;
  const QuicStreamId id2 = id1 + 2;
  const QuicStreamId id3 = id2 + 2;
  write_blocked_list_.RegisterStream(
      id1, false, spdy::SpdyStreamPrecedence(kV3LowestPriority));
  write_blocked_list_.RegisterStream(
      id2, false, spdy::SpdyStreamPrecedence(kV3LowestPriority));
  write_blocked_list_.RegisterStream(
      id3, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));

  write_blocked_list_.AddStream(id1);
  write_blocked_list_.AddStream(id2);
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());

  // The first stream we push back should stay at the front until 16k is
  // written.
  EXPECT_EQ(id1, write_blocked_list_.PopFront());
  write_blocked_list_.UpdateBytesForStream(id1, 15999);
  write_blocked_list_.AddStream(id1);
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());
  EXPECT_EQ(id1, write_blocked_list_.PopFront());

  // Once 16k is written the first stream will yield to the next.
  write_blocked_list_.UpdateBytesForStream(id1, 1);
  write_blocked_list_.AddStream(id1);
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());
  EXPECT_EQ(id2, write_blocked_list_.PopFront());

  // Set the new stream to have written all but one byte.
  write_blocked_list_.UpdateBytesForStream(id2, 15999);
  write_blocked_list_.AddStream(id2);
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());

  // Ensure higher priority streams are popped first.
  write_blocked_list_.AddStream(id3);
  EXPECT_EQ(id3, write_blocked_list_.PopFront());

  // Higher priority streams will always be popped first, even if using their
  // byte quota
  write_blocked_list_.UpdateBytesForStream(id3, 20000);
  write_blocked_list_.AddStream(id3);
  EXPECT_EQ(id3, write_blocked_list_.PopFront());

  // Once the higher priority stream is out of the way, id2 will resume its 16k
  // write, with only 1 byte remaining of its guaranteed write allocation.
  EXPECT_EQ(id2, write_blocked_list_.PopFront());
  write_blocked_list_.UpdateBytesForStream(id2, 1);
  write_blocked_list_.AddStream(id2);
  EXPECT_EQ(2u, write_blocked_list_.NumBlockedStreams());
  EXPECT_EQ(id1, write_blocked_list_.PopFront());
}

TEST_P(QuicWriteBlockedListTest, Ceding) {
  if (GetParam()) {
    /*
         0
         |
         15
         |
         16
         |
         5
         |
         4
         |
         7
    */
    write_blocked_list_.RegisterStream(
        15, false,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        16, false,
        spdy::SpdyStreamPrecedence(15, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        4, false,
        spdy::SpdyStreamPrecedence(16, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        5, false,
        spdy::SpdyStreamPrecedence(16, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        7, false,
        spdy::SpdyStreamPrecedence(4, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        1, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
    write_blocked_list_.RegisterStream(
        3, true,
        spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight,
                                   kExclusiveBit));
  } else {
    write_blocked_list_.RegisterStream(
        15, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        16, false, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(5, false, spdy::SpdyStreamPrecedence(5));
    write_blocked_list_.RegisterStream(4, false, spdy::SpdyStreamPrecedence(5));
    write_blocked_list_.RegisterStream(7, false, spdy::SpdyStreamPrecedence(7));
    write_blocked_list_.RegisterStream(
        1, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
    write_blocked_list_.RegisterStream(
        3, true, spdy::SpdyStreamPrecedence(kV3HighestPriority));
  }

  // When nothing is on the list, nothing yields.
  EXPECT_FALSE(write_blocked_list_.ShouldYield(5));

  write_blocked_list_.AddStream(5);
  // 5 should not yield to itself.
  EXPECT_FALSE(write_blocked_list_.ShouldYield(5));
  // 4 and 7 are equal or lower priority and should yield to 5.
  EXPECT_TRUE(write_blocked_list_.ShouldYield(4));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(7));
  // 15, headers and crypto should preempt 5.
  EXPECT_FALSE(write_blocked_list_.ShouldYield(15));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(3));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(1));

  // Block a high priority stream.
  write_blocked_list_.AddStream(15);
  // 16 should yield (same priority) but headers and crypto will still not.
  EXPECT_TRUE(write_blocked_list_.ShouldYield(16));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(3));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(1));

  // Block the headers stream.  All streams but crypto and headers should yield.
  write_blocked_list_.AddStream(3);
  EXPECT_TRUE(write_blocked_list_.ShouldYield(16));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(15));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(3));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(1));

  // Block the crypto stream.  All streams but crypto should yield.
  write_blocked_list_.AddStream(1);
  EXPECT_TRUE(write_blocked_list_.ShouldYield(16));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(15));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(3));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(1));
}

TEST_P(QuicWriteBlockedListTest, UpdateStreamPriority) {
  if (!GetParam()) {
    return;
  }
  /*
       0
       |
       5
       |
       7
       |
       9
       |
       11
  */
  write_blocked_list_.RegisterStream(
      5, false,
      spdy::SpdyStreamPrecedence(0, kHttp2DefaultStreamWeight, kExclusiveBit));
  write_blocked_list_.RegisterStream(
      7, false,
      spdy::SpdyStreamPrecedence(5, kHttp2DefaultStreamWeight, kExclusiveBit));
  write_blocked_list_.RegisterStream(
      9, false,
      spdy::SpdyStreamPrecedence(7, kHttp2DefaultStreamWeight, kExclusiveBit));
  write_blocked_list_.RegisterStream(
      11, false,
      spdy::SpdyStreamPrecedence(9, kHttp2DefaultStreamWeight, kExclusiveBit));

  write_blocked_list_.AddStream(7);
  EXPECT_FALSE(write_blocked_list_.ShouldYield(5));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(9));
  EXPECT_TRUE(write_blocked_list_.ShouldYield(11));

  // Update 9's priority.
  if (GetParam()) {
    /*
         0
         |
         5
        / \
       7   9
           |
           11
    */
    write_blocked_list_.UpdateStreamPriority(
        9, spdy::SpdyStreamPrecedence(5, kHttp2DefaultStreamWeight,
                                      kExclusiveBit));
  } else {
    write_blocked_list_.UpdateStreamPriority(9, spdy::SpdyStreamPrecedence(1));
  }
  EXPECT_FALSE(write_blocked_list_.ShouldYield(5));
  // Verify 9 now does not yield to 7.
  EXPECT_FALSE(write_blocked_list_.ShouldYield(9));
  EXPECT_FALSE(write_blocked_list_.ShouldYield(11));

  write_blocked_list_.AddStream(9);
  // Verify 11 yield to 9.
  EXPECT_TRUE(write_blocked_list_.ShouldYield(11));
}

}  // namespace
}  // namespace test
}  // namespace quic
