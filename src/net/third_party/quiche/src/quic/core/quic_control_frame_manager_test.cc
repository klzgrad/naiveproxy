// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_control_frame_manager.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {

class QuicControlFrameManagerPeer {
 public:
  static size_t QueueSize(QuicControlFrameManager* manager) {
    return manager->control_frames_.size();
  }
};

namespace {

const QuicStreamId kTestStreamId = 5;
const QuicStreamId kTestStopSendingCode = 321;

class QuicControlFrameManagerTest : public QuicTest {
 public:
  bool SaveControlFrame(const QuicFrame& frame) {
    frame_ = frame;
    return true;
  }

 protected:
  // Pre-fills the control frame queue with the following frames:
  //  ID Type
  //  1  RST_STREAM
  //  2  GO_AWAY
  //  3  WINDOW_UPDATE
  //  4  BLOCKED
  //  5  STOP_SENDING
  // This is verified. The tests then perform manipulations on these.
  void Initialize() {
    connection_ = new MockQuicConnection(&helper_, &alarm_factory_,
                                         Perspective::IS_SERVER);
    session_ = std::make_unique<StrictMock<MockQuicSession>>(connection_);
    manager_ = std::make_unique<QuicControlFrameManager>(session_.get());
    EXPECT_EQ(0u, QuicControlFrameManagerPeer::QueueSize(manager_.get()));
    EXPECT_FALSE(manager_->HasPendingRetransmission());
    EXPECT_FALSE(manager_->WillingToWrite());

    EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
    manager_->WriteOrBufferRstStream(kTestStreamId, QUIC_STREAM_CANCELLED, 0);
    manager_->WriteOrBufferGoAway(QUIC_PEER_GOING_AWAY, kTestStreamId,
                                  "Going away.");
    manager_->WriteOrBufferWindowUpdate(kTestStreamId, 100);
    manager_->WriteOrBufferBlocked(kTestStreamId);
    manager_->WriteOrBufferStopSending(kTestStopSendingCode, kTestStreamId);
    number_of_frames_ = 5u;
    ping_frame_id_ = 6u;
    EXPECT_EQ(number_of_frames_,
              QuicControlFrameManagerPeer::QueueSize(manager_.get()));
    EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&rst_stream_)));
    EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&goaway_)));
    EXPECT_TRUE(
        manager_->IsControlFrameOutstanding(QuicFrame(&window_update_)));
    EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&blocked_)));
    EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&stop_sending_)));
    EXPECT_FALSE(manager_->IsControlFrameOutstanding(
        QuicFrame(QuicPingFrame(ping_frame_id_))));

    EXPECT_FALSE(manager_->HasPendingRetransmission());
    EXPECT_TRUE(manager_->WillingToWrite());
  }

  QuicRstStreamFrame rst_stream_ = {1, kTestStreamId, QUIC_STREAM_CANCELLED, 0};
  QuicGoAwayFrame goaway_ = {2, QUIC_PEER_GOING_AWAY, kTestStreamId,
                             "Going away."};
  QuicWindowUpdateFrame window_update_ = {3, kTestStreamId, 100};
  QuicBlockedFrame blocked_ = {4, kTestStreamId};
  QuicStopSendingFrame stop_sending_ = {5, kTestStreamId, kTestStopSendingCode};
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicConnection* connection_;
  std::unique_ptr<StrictMock<MockQuicSession>> session_;
  std::unique_ptr<QuicControlFrameManager> manager_;
  QuicFrame frame_;
  size_t number_of_frames_;
  int ping_frame_id_;
};

TEST_F(QuicControlFrameManagerTest, OnControlFrameAcked) {
  Initialize();
  InSequence s;
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(3)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
  // Send control frames 1, 2, 3.
  manager_->OnCanWrite();
  EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&rst_stream_)));
  EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&goaway_)));
  EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&window_update_)));
  EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&blocked_)));
  EXPECT_TRUE(manager_->IsControlFrameOutstanding(QuicFrame(&stop_sending_)));

  EXPECT_FALSE(manager_->IsControlFrameOutstanding(
      QuicFrame(QuicPingFrame(ping_frame_id_))));
  EXPECT_TRUE(manager_->OnControlFrameAcked(QuicFrame(&window_update_)));
  EXPECT_FALSE(manager_->IsControlFrameOutstanding(QuicFrame(&window_update_)));
  EXPECT_EQ(number_of_frames_,
            QuicControlFrameManagerPeer::QueueSize(manager_.get()));

  EXPECT_TRUE(manager_->OnControlFrameAcked(QuicFrame(&goaway_)));
  EXPECT_FALSE(manager_->IsControlFrameOutstanding(QuicFrame(&goaway_)));
  EXPECT_EQ(number_of_frames_,
            QuicControlFrameManagerPeer::QueueSize(manager_.get()));
  EXPECT_TRUE(manager_->OnControlFrameAcked(QuicFrame(&rst_stream_)));
  EXPECT_FALSE(manager_->IsControlFrameOutstanding(QuicFrame(&rst_stream_)));
  // Only after the first frame in the queue is acked do the frames get
  // removed ... now see that the length has been reduced by 3.
  EXPECT_EQ(number_of_frames_ - 3u,
            QuicControlFrameManagerPeer::QueueSize(manager_.get()));
  // Duplicate ack.
  EXPECT_FALSE(manager_->OnControlFrameAcked(QuicFrame(&goaway_)));

  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_TRUE(manager_->WillingToWrite());

  // Send control frames 4, 5.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();
  manager_->WritePing();
  EXPECT_FALSE(manager_->WillingToWrite());
}

TEST_F(QuicControlFrameManagerTest, OnControlFrameLost) {
  Initialize();
  InSequence s;
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(3)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
  // Send control frames 1, 2, 3.
  manager_->OnCanWrite();

  // Lost control frames 1, 2, 3.
  manager_->OnControlFrameLost(QuicFrame(&rst_stream_));
  manager_->OnControlFrameLost(QuicFrame(&goaway_));
  manager_->OnControlFrameLost(QuicFrame(&window_update_));
  EXPECT_TRUE(manager_->HasPendingRetransmission());

  // Ack control frame 2.
  manager_->OnControlFrameAcked(QuicFrame(&goaway_));

  // Retransmit control frames 1, 3.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();
  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_TRUE(manager_->WillingToWrite());

  // Send control frames 4, 5, and 6.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(number_of_frames_ - 2u)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();
  manager_->WritePing();
  EXPECT_FALSE(manager_->WillingToWrite());
}

TEST_F(QuicControlFrameManagerTest, RetransmitControlFrame) {
  Initialize();
  InSequence s;
  // Send control frames 1, 2, 3, 4.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(number_of_frames_)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();

  // Ack control frame 2.
  manager_->OnControlFrameAcked(QuicFrame(&goaway_));
  // Do not retransmit an acked frame.
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  EXPECT_TRUE(manager_->RetransmitControlFrame(QuicFrame(&goaway_),
                                               PTO_RETRANSMISSION));

  // Retransmit control frame 3.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_TRUE(manager_->RetransmitControlFrame(QuicFrame(&window_update_),
                                               PTO_RETRANSMISSION));

  // Retransmit control frame 4, and connection is write blocked.
  EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
  EXPECT_FALSE(manager_->RetransmitControlFrame(QuicFrame(&window_update_),
                                                PTO_RETRANSMISSION));
}

TEST_F(QuicControlFrameManagerTest, DonotSendPingWithBufferedFrames) {
  Initialize();
  InSequence s;
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
  // Send control frame 1.
  manager_->OnCanWrite();
  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_TRUE(manager_->WillingToWrite());

  // Send PING when there is buffered frames.
  manager_->WritePing();
  // Verify only the buffered frames are sent.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(number_of_frames_ - 1)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();
  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_FALSE(manager_->WillingToWrite());
}

TEST_F(QuicControlFrameManagerTest, DonotRetransmitOldWindowUpdates) {
  Initialize();
  // Send two more window updates of the same stream.
  manager_->WriteOrBufferWindowUpdate(kTestStreamId, 200);
  QuicWindowUpdateFrame window_update2(number_of_frames_ + 1, kTestStreamId,
                                       200);

  manager_->WriteOrBufferWindowUpdate(kTestStreamId, 300);
  QuicWindowUpdateFrame window_update3(number_of_frames_ + 2, kTestStreamId,
                                       300);
  InSequence s;
  // Flush all buffered control frames.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();

  // Mark all 3 window updates as lost.
  manager_->OnControlFrameLost(QuicFrame(&window_update_));
  manager_->OnControlFrameLost(QuicFrame(&window_update2));
  manager_->OnControlFrameLost(QuicFrame(&window_update3));
  EXPECT_TRUE(manager_->HasPendingRetransmission());
  EXPECT_TRUE(manager_->WillingToWrite());

  // Verify only the latest window update gets retransmitted.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(this, &QuicControlFrameManagerTest::SaveControlFrame));
  manager_->OnCanWrite();
  EXPECT_EQ(number_of_frames_ + 2u,
            frame_.window_update_frame->control_frame_id);
  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_FALSE(manager_->WillingToWrite());
  DeleteFrame(&frame_);
}

TEST_F(QuicControlFrameManagerTest, RetransmitWindowUpdateOfDifferentStreams) {
  Initialize();
  // Send two more window updates of different streams.
  manager_->WriteOrBufferWindowUpdate(kTestStreamId + 2, 200);
  QuicWindowUpdateFrame window_update2(5, kTestStreamId + 2, 200);

  manager_->WriteOrBufferWindowUpdate(kTestStreamId + 4, 300);
  QuicWindowUpdateFrame window_update3(6, kTestStreamId + 4, 300);
  InSequence s;
  // Flush all buffered control frames.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();

  // Mark all 3 window updates as lost.
  manager_->OnControlFrameLost(QuicFrame(&window_update_));
  manager_->OnControlFrameLost(QuicFrame(&window_update2));
  manager_->OnControlFrameLost(QuicFrame(&window_update3));
  EXPECT_TRUE(manager_->HasPendingRetransmission());
  EXPECT_TRUE(manager_->WillingToWrite());

  // Verify all 3 window updates get retransmitted.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(3)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  manager_->OnCanWrite();
  EXPECT_FALSE(manager_->HasPendingRetransmission());
  EXPECT_FALSE(manager_->WillingToWrite());
}

TEST_F(QuicControlFrameManagerTest, TooManyBufferedControlFrames) {
  Initialize();
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(5)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  // Flush buffered frames.
  manager_->OnCanWrite();
  // Write 995 control frames.
  EXPECT_CALL(*connection_, SendControlFrame(_)).WillOnce(Return(false));
  for (size_t i = 0; i < 995; ++i) {
    manager_->WriteOrBufferRstStream(kTestStreamId, QUIC_STREAM_CANCELLED, 0);
  }
  // Verify write one more control frame causes connection close.
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES, _,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  manager_->WriteOrBufferRstStream(kTestStreamId, QUIC_STREAM_CANCELLED, 0);
}

}  // namespace
}  // namespace test
}  // namespace quic
