// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_datagram_queue.h"

#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

using quiche::QuicheOptional;

using testing::_;
using testing::ElementsAre;
using testing::Return;

class EstablishedCryptoStream : public MockQuicCryptoStream {
 public:
  using MockQuicCryptoStream::MockQuicCryptoStream;

  bool encryption_established() const override { return true; }
};

class QuicDatagramQueueTest : public QuicTest {
 public:
  QuicDatagramQueueTest()
      : connection_(new MockQuicConnection(&helper_,
                                           &alarm_factory_,
                                           Perspective::IS_CLIENT)),
        session_(connection_),
        queue_(&session_) {
    session_.SetCryptoStream(new EstablishedCryptoStream(&session_));
  }

  QuicMemSlice CreateMemSlice(quiche::QuicheStringPiece data) {
    QuicUniqueBufferPtr buffer =
        MakeUniqueBuffer(helper_.GetStreamSendBufferAllocator(), data.size());
    memcpy(buffer.get(), data.data(), data.size());
    return QuicMemSlice(std::move(buffer), data.size());
  }

 protected:
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicConnection* connection_;  // Owned by |session_|.
  MockQuicSession session_;
  QuicDatagramQueue queue_;
};

TEST_F(QuicDatagramQueueTest, SendDatagramImmediately) {
  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_SUCCESS));
  MessageStatus status = queue_.SendOrQueueDatagram(CreateMemSlice("test"));
  EXPECT_EQ(MESSAGE_STATUS_SUCCESS, status);
  EXPECT_EQ(0u, queue_.queue_size());
}

TEST_F(QuicDatagramQueueTest, SendDatagramAfterBuffering) {
  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_BLOCKED));
  MessageStatus initial_status =
      queue_.SendOrQueueDatagram(CreateMemSlice("test"));
  EXPECT_EQ(MESSAGE_STATUS_BLOCKED, initial_status);
  EXPECT_EQ(1u, queue_.queue_size());

  // Verify getting write blocked does not remove the datagram from the queue.
  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_BLOCKED));
  QuicheOptional<MessageStatus> status = queue_.TrySendingNextDatagram();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(MESSAGE_STATUS_BLOCKED, *status);
  EXPECT_EQ(1u, queue_.queue_size());

  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_SUCCESS));
  status = queue_.TrySendingNextDatagram();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(MESSAGE_STATUS_SUCCESS, *status);
  EXPECT_EQ(0u, queue_.queue_size());
}

TEST_F(QuicDatagramQueueTest, EmptyBuffer) {
  QuicheOptional<MessageStatus> status = queue_.TrySendingNextDatagram();
  EXPECT_FALSE(status.has_value());

  size_t num_messages = queue_.SendDatagrams();
  EXPECT_EQ(0u, num_messages);
}

TEST_F(QuicDatagramQueueTest, MultipleDatagrams) {
  // Note that SendMessage() is called only once here, since all the remaining
  // messages are automatically queued due to the queue being non-empty.
  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_BLOCKED));
  queue_.SendOrQueueDatagram(CreateMemSlice("a"));
  queue_.SendOrQueueDatagram(CreateMemSlice("b"));
  queue_.SendOrQueueDatagram(CreateMemSlice("c"));
  queue_.SendOrQueueDatagram(CreateMemSlice("d"));
  queue_.SendOrQueueDatagram(CreateMemSlice("e"));

  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .Times(5)
      .WillRepeatedly(Return(MESSAGE_STATUS_SUCCESS));
  size_t num_messages = queue_.SendDatagrams();
  EXPECT_EQ(5u, num_messages);
}

TEST_F(QuicDatagramQueueTest, DefaultMaxTimeInQueue) {
  EXPECT_EQ(QuicTime::Delta::Zero(),
            connection_->sent_packet_manager().GetRttStats()->min_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(4), queue_.GetMaxTimeInQueue());

  RttStats* stats =
      const_cast<RttStats*>(connection_->sent_packet_manager().GetRttStats());
  stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                   QuicTime::Delta::Zero(), helper_.GetClock()->Now());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(125), queue_.GetMaxTimeInQueue());
}

TEST_F(QuicDatagramQueueTest, Expiry) {
  constexpr QuicTime::Delta expiry = QuicTime::Delta::FromMilliseconds(100);
  queue_.SetMaxTimeInQueue(expiry);

  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_BLOCKED));
  queue_.SendOrQueueDatagram(CreateMemSlice("a"));
  helper_.AdvanceTime(0.6 * expiry);
  queue_.SendOrQueueDatagram(CreateMemSlice("b"));
  helper_.AdvanceTime(0.6 * expiry);
  queue_.SendOrQueueDatagram(CreateMemSlice("c"));

  std::vector<std::string> messages;
  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillRepeatedly([&messages](QuicMessageId /*id*/,
                                  QuicMemSliceSpan message, bool /*flush*/) {
        messages.push_back(std::string(message.GetData(0)));
        return MESSAGE_STATUS_SUCCESS;
      });
  EXPECT_EQ(2u, queue_.SendDatagrams());
  EXPECT_THAT(messages, ElementsAre("b", "c"));
}

TEST_F(QuicDatagramQueueTest, ExpireAll) {
  constexpr QuicTime::Delta expiry = QuicTime::Delta::FromMilliseconds(100);
  queue_.SetMaxTimeInQueue(expiry);

  EXPECT_CALL(*connection_, SendMessage(_, _, _))
      .WillOnce(Return(MESSAGE_STATUS_BLOCKED));
  queue_.SendOrQueueDatagram(CreateMemSlice("a"));
  queue_.SendOrQueueDatagram(CreateMemSlice("b"));
  queue_.SendOrQueueDatagram(CreateMemSlice("c"));

  helper_.AdvanceTime(100 * expiry);
  EXPECT_CALL(*connection_, SendMessage(_, _, _)).Times(0);
  EXPECT_EQ(0u, queue_.SendDatagrams());
}

}  // namespace
}  // namespace test
}  // namespace quic
