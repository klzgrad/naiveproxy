// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_server_stream_base.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;

namespace quic {
namespace test {
namespace {

class TestQuicSpdyServerStream : public QuicSpdyServerStreamBase {
 public:
  TestQuicSpdyServerStream(QuicStreamId id,
                           QuicSpdySession* session,
                           StreamType type)
      : QuicSpdyServerStreamBase(id, session, type) {}

  void OnBodyAvailable() override {}
};

class QuicSpdyServerStreamBaseTest : public QuicTest {
 protected:
  QuicSpdyServerStreamBaseTest()
      : session_(new MockQuicConnection(&helper_,
                                        &alarm_factory_,
                                        Perspective::IS_SERVER)) {
    session_.Initialize();
    stream_ =
        new TestQuicSpdyServerStream(GetNthClientInitiatedBidirectionalStreamId(
                                         session_.transport_version(), 0),
                                     &session_, BIDIRECTIONAL);
    session_.ActivateStream(QuicWrapUnique(stream_));
    helper_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }

  QuicSpdyServerStreamBase* stream_ = nullptr;
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicSpdySession session_;
};

TEST_F(QuicSpdyServerStreamBaseTest,
       SendQuicRstStreamNoErrorWithEarlyResponse) {
  stream_->StopReading();
  EXPECT_CALL(session_, SendRstStream(_, QUIC_STREAM_NO_ERROR, _, _)).Times(1);
  QuicStreamPeer::SetFinSent(stream_);
  stream_->CloseWriteSide();
}

TEST_F(QuicSpdyServerStreamBaseTest,
       DoNotSendQuicRstStreamNoErrorWithRstReceived) {
  EXPECT_FALSE(stream_->reading_stopped());

  EXPECT_CALL(session_, SendRstStream(_, QUIC_STREAM_NO_ERROR, _, _)).Times(0);

  EXPECT_CALL(
      session_,
      SendRstStream(_,
                    VersionHasIetfQuicFrames(session_.transport_version())
                        ? QUIC_STREAM_CANCELLED
                        : QUIC_RST_ACKNOWLEDGEMENT,
                    _, _))
      .Times(1);
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  stream_->OnStreamReset(rst_frame);
  if (VersionHasIetfQuicFrames(session_.transport_version())) {
    // Create and inject a STOP SENDING frame to complete the close
    // of the stream. This is only needed for version 99/IETF QUIC.
    QuicStopSendingFrame stop_sending(kInvalidControlFrameId, stream_->id(),
                                      QUIC_STREAM_CANCELLED);
    session_.OnStopSendingFrame(stop_sending);
  }

  EXPECT_TRUE(stream_->reading_stopped());
  EXPECT_TRUE(stream_->write_side_closed());
}

}  // namespace
}  // namespace test
}  // namespace quic
