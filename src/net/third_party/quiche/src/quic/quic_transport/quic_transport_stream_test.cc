// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"

#include <memory>

#include "net/third_party/quiche/src/quic/core/frames/quic_window_update_frame.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_session_interface.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_transport_test_tools.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

using testing::_;
using testing::Return;

ParsedQuicVersionVector GetVersions() {
  return {DefaultVersionForQuicTransport()};
}

class MockQuicTransportSessionInterface : public QuicTransportSessionInterface {
 public:
  MOCK_CONST_METHOD0(IsSessionReady, bool());
};

class QuicTransportStreamTest : public QuicTest {
 public:
  QuicTransportStreamTest()
      : connection_(new MockQuicConnection(&helper_,
                                           &alarm_factory_,
                                           Perspective::IS_CLIENT,
                                           GetVersions())),
        session_(connection_) {
    QuicEnableVersion(DefaultVersionForQuicTransport());
    session_.Initialize();

    stream_ = new QuicTransportStream(0, &session_, &interface_);
    session_.ActivateStream(QuicWrapUnique(stream_));

    auto visitor = std::make_unique<MockStreamVisitor>();
    visitor_ = visitor.get();
    stream_->set_visitor(std::move(visitor));
  }

  void ReceiveStreamData(quiche::QuicheStringPiece data,
                         QuicStreamOffset offset) {
    QuicStreamFrame frame(0, false, offset, data);
    stream_->OnStreamFrame(frame);
  }

 protected:
  MockAlarmFactory alarm_factory_;
  MockQuicConnectionHelper helper_;

  MockQuicConnection* connection_;  // Owned by |session_|.
  MockQuicSession session_;
  MockQuicTransportSessionInterface interface_;
  QuicTransportStream* stream_;  // Owned by |session_|.
  MockStreamVisitor* visitor_;   // Owned by |stream_|.
};

TEST_F(QuicTransportStreamTest, NotReady) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(false));
  ReceiveStreamData("test", 0);
  EXPECT_EQ(stream_->ReadableBytes(), 0u);
  EXPECT_FALSE(stream_->CanWrite());
}

TEST_F(QuicTransportStreamTest, ReadWhenNotReady) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(false));
  ReceiveStreamData("test", 0);
  char buffer[4];
  QuicByteCount bytes_read = stream_->Read(buffer, sizeof(buffer));
  EXPECT_EQ(bytes_read, 0u);
}

TEST_F(QuicTransportStreamTest, WriteWhenNotReady) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(false));
  EXPECT_FALSE(stream_->Write("test"));
}

TEST_F(QuicTransportStreamTest, Ready) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));
  ReceiveStreamData("test", 0);
  EXPECT_EQ(stream_->ReadableBytes(), 4u);
  EXPECT_TRUE(stream_->CanWrite());
  EXPECT_TRUE(stream_->Write("test"));
}

TEST_F(QuicTransportStreamTest, ReceiveData) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));
  EXPECT_CALL(*visitor_, OnCanRead());
  ReceiveStreamData("test", 0);
}

TEST_F(QuicTransportStreamTest, FinReadWithNoDataPending) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));
  EXPECT_CALL(*visitor_, OnFinRead());
  QuicStreamFrame frame(0, true, 0, "");
  stream_->OnStreamFrame(frame);
}

TEST_F(QuicTransportStreamTest, FinReadWithDataPending) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));

  EXPECT_CALL(*visitor_, OnCanRead());
  EXPECT_CALL(*visitor_, OnFinRead()).Times(0);
  QuicStreamFrame frame(0, true, 0, "test");
  stream_->OnStreamFrame(frame);

  EXPECT_CALL(*visitor_, OnFinRead()).Times(1);
  std::string buffer;
  ASSERT_EQ(stream_->Read(&buffer), 4u);
}

TEST_F(QuicTransportStreamTest, WritingTooMuchData) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));
  ASSERT_TRUE(stream_->CanWrite());

  std::string a_little_bit_of_data(128, 'A');
  std::string a_lot_of_data(GetQuicFlag(FLAGS_quic_buffered_data_threshold) * 2,
                            'a');

  EXPECT_TRUE(stream_->Write(a_little_bit_of_data));
  EXPECT_TRUE(stream_->Write(a_little_bit_of_data));
  EXPECT_TRUE(stream_->Write(a_little_bit_of_data));

  EXPECT_TRUE(stream_->Write(a_lot_of_data));
  EXPECT_FALSE(stream_->Write(a_lot_of_data));
}

TEST_F(QuicTransportStreamTest, CannotSendFinTwice) {
  EXPECT_CALL(interface_, IsSessionReady()).WillRepeatedly(Return(true));
  ASSERT_TRUE(stream_->CanWrite());

  EXPECT_CALL(session_, WritevData(stream_->id(), _, _, _, _, _))
      .WillOnce(Return(QuicConsumedData(0, /*fin_consumed=*/true)));
  EXPECT_TRUE(stream_->SendFin());
  EXPECT_FALSE(stream_->CanWrite());
}

}  // namespace
}  // namespace test
}  // namespace quic
