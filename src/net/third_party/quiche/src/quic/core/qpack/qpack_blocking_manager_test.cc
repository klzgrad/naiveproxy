// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_blocking_manager.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QpackBlockingManagerTest : public QuicTest {
 protected:
  QpackBlockingManagerTest() = default;
  ~QpackBlockingManagerTest() override = default;

  QpackBlockingManager manager_;
};

TEST_F(QpackBlockingManagerTest, Empty) {
  EXPECT_EQ(0u, manager_.blocked_stream_count());
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  EXPECT_FALSE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_FALSE(manager_.OnHeaderAcknowledgement(1));
}

TEST_F(QpackBlockingManagerTest, NotBlockedByInsertCountIncrement) {
  manager_.OnInsertCountIncrement(2);

  // Stream 0 is not blocked, because it only references entries that are
  // already acknowledged by an Insert Count Increment instruction.
  manager_.OnHeaderBlockSent(0, {1, 0});
  EXPECT_EQ(0u, manager_.blocked_stream_count());
}

TEST_F(QpackBlockingManagerTest, UnblockedByInsertCountIncrement) {
  manager_.OnHeaderBlockSent(0, {1, 0});
  EXPECT_EQ(1u, manager_.blocked_stream_count());

  manager_.OnInsertCountIncrement(2);
  EXPECT_EQ(0u, manager_.blocked_stream_count());
}

TEST_F(QpackBlockingManagerTest, NotBlockedByHeaderAcknowledgement) {
  manager_.OnHeaderBlockSent(0, {2, 1, 1});
  EXPECT_EQ(1u, manager_.blocked_stream_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(0u, manager_.blocked_stream_count());

  // Stream 1 is not blocked, because it only references entries that are
  // already acknowledged by a Header Acknowledgement instruction.
  manager_.OnHeaderBlockSent(1, {2, 2});
  EXPECT_EQ(0u, manager_.blocked_stream_count());
}

TEST_F(QpackBlockingManagerTest, UnblockedByHeaderAcknowledgement) {
  manager_.OnHeaderBlockSent(0, {2, 1, 1});
  manager_.OnHeaderBlockSent(1, {2, 2});
  EXPECT_EQ(2u, manager_.blocked_stream_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(0u, manager_.blocked_stream_count());
}

TEST_F(QpackBlockingManagerTest, KnownReceivedCount) {
  EXPECT_EQ(0u, manager_.known_received_count());

  // Sending a header block does not change Known Received Count.
  manager_.OnHeaderBlockSent(0, {0});
  EXPECT_EQ(0u, manager_.known_received_count());

  manager_.OnHeaderBlockSent(1, {1});
  EXPECT_EQ(0u, manager_.known_received_count());

  // Header Acknowledgement might increase Known Received Count.
  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(1u, manager_.known_received_count());

  manager_.OnHeaderBlockSent(2, {5});
  EXPECT_EQ(1u, manager_.known_received_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(1));
  EXPECT_EQ(2u, manager_.known_received_count());

  // Insert Count Increment increases Known Received Count.
  manager_.OnInsertCountIncrement(2);
  EXPECT_EQ(4u, manager_.known_received_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(2));
  EXPECT_EQ(6u, manager_.known_received_count());

  // Stream Cancellation does not change Known Received Count.
  manager_.OnStreamCancellation(0);
  EXPECT_EQ(6u, manager_.known_received_count());

  // Header Acknowledgement of a block with smaller Required Insert Count does
  // not increase Known Received Count.
  manager_.OnHeaderBlockSent(0, {3});
  EXPECT_EQ(6u, manager_.known_received_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(6u, manager_.known_received_count());

  // Header Acknowledgement of a block with equal Required Insert Count does not
  // increase Known Received Count.
  manager_.OnHeaderBlockSent(1, {5});
  EXPECT_EQ(6u, manager_.known_received_count());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(1));
  EXPECT_EQ(6u, manager_.known_received_count());
}

TEST_F(QpackBlockingManagerTest, SmallestBlockingIndex) {
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(0, {0});
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(1, {2});
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(2u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(1, {1});
  EXPECT_EQ(1u, manager_.smallest_blocking_index());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(1));
  EXPECT_EQ(1u, manager_.smallest_blocking_index());

  // Insert Count Increment does not change smallest blocking index.
  manager_.OnInsertCountIncrement(2);
  EXPECT_EQ(1u, manager_.smallest_blocking_index());

  manager_.OnStreamCancellation(1);
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());
}

TEST_F(QpackBlockingManagerTest, HeaderAcknowledgementsOnSingleStream) {
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.blocked_stream_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(0, {2, 1, 1});
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(1u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(0, {1, 0});
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(3u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.blocked_stream_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(0, {3});
  EXPECT_EQ(3u, manager_.known_received_count());
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(3u, manager_.known_received_count());
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(3u, manager_.smallest_blocking_index());

  EXPECT_TRUE(manager_.OnHeaderAcknowledgement(0));
  EXPECT_EQ(4u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.blocked_stream_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  EXPECT_FALSE(manager_.OnHeaderAcknowledgement(0));
}

TEST_F(QpackBlockingManagerTest, CancelStream) {
  manager_.OnHeaderBlockSent(0, {3});
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(3u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(0, {2});
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(2u, manager_.smallest_blocking_index());

  manager_.OnHeaderBlockSent(1, {4});
  EXPECT_EQ(2u, manager_.blocked_stream_count());
  EXPECT_EQ(2u, manager_.smallest_blocking_index());

  manager_.OnStreamCancellation(0);
  EXPECT_EQ(1u, manager_.blocked_stream_count());
  EXPECT_EQ(4u, manager_.smallest_blocking_index());

  manager_.OnStreamCancellation(1);
  EXPECT_EQ(0u, manager_.blocked_stream_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());
}

TEST_F(QpackBlockingManagerTest,
       ReferenceOnEncoderStreamUnblockedByInsertCountIncrement) {
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  // Entry 1 refers to entry 0.
  manager_.OnReferenceSentOnEncoderStream(1, 0);
  // Entry 2 also refers to entry 0.
  manager_.OnReferenceSentOnEncoderStream(2, 0);

  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  // Acknowledging entry 1 still leaves one unacknowledged reference to entry 0.
  manager_.OnInsertCountIncrement(2);

  EXPECT_EQ(2u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  // Entry 3 also refers to entry 2.
  manager_.OnReferenceSentOnEncoderStream(3, 2);

  EXPECT_EQ(2u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  // Acknowledging entry 2 removes last reference to entry 0.
  manager_.OnInsertCountIncrement(1);

  EXPECT_EQ(3u, manager_.known_received_count());
  EXPECT_EQ(2u, manager_.smallest_blocking_index());

  // Acknowledging entry 4 (and implicitly 3) removes reference to entry 2.
  manager_.OnInsertCountIncrement(2);

  EXPECT_EQ(5u, manager_.known_received_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());
}

TEST_F(QpackBlockingManagerTest,
       ReferenceOnEncoderStreamUnblockedByHeaderAcknowledgement) {
  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());

  // Entry 1 refers to entry 0.
  manager_.OnReferenceSentOnEncoderStream(1, 0);
  // Entry 2 also refers to entry 0.
  manager_.OnReferenceSentOnEncoderStream(2, 0);

  EXPECT_EQ(0u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  // Acknowledging a header block with entries up to 1 still leave one
  // unacknowledged reference to entry 0.
  manager_.OnHeaderBlockSent(/* stream_id = */ 0, {0, 1});
  manager_.OnHeaderAcknowledgement(/* stream_id = */ 0);

  EXPECT_EQ(2u, manager_.known_received_count());
  EXPECT_EQ(0u, manager_.smallest_blocking_index());

  // Entry 3 also refers to entry 2.
  manager_.OnReferenceSentOnEncoderStream(3, 2);

  // Acknowledging a header block with entries up to 2 removes last reference to
  // entry 0.
  manager_.OnHeaderBlockSent(/* stream_id = */ 0, {2, 0, 2});
  manager_.OnHeaderAcknowledgement(/* stream_id = */ 0);

  EXPECT_EQ(3u, manager_.known_received_count());
  EXPECT_EQ(2u, manager_.smallest_blocking_index());

  // Acknowledging entry 4 (and implicitly 3) removes reference to entry 2.
  manager_.OnHeaderBlockSent(/* stream_id = */ 0, {1, 4, 2, 0});
  manager_.OnHeaderAcknowledgement(/* stream_id = */ 0);

  EXPECT_EQ(5u, manager_.known_received_count());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            manager_.smallest_blocking_index());
}

}  // namespace
}  // namespace test
}  // namespace quic
