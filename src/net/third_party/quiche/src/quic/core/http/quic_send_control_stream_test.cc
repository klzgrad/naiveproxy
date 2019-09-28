// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

namespace {
using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

struct TestParams {
  TestParams(const ParsedQuicVersion& version, Perspective perspective)
      : version(version), perspective(perspective) {
    QUIC_LOG(INFO) << "TestParams: version: "
                   << ParsedQuicVersionToString(version)
                   << ", perspective: " << perspective;
  }

  TestParams(const TestParams& other)
      : version(other.version), perspective(other.perspective) {}

  ParsedQuicVersion version;
  Perspective perspective;
};

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  ParsedQuicVersionVector all_supported_versions = AllSupportedVersions();
  for (const auto& version : AllSupportedVersions()) {
    if (!VersionHasStreamType(version.transport_version)) {
      continue;
    }
    for (Perspective p : {Perspective::IS_SERVER, Perspective::IS_CLIENT}) {
      params.emplace_back(version, p);
    }
  }
  return params;
}

class QuicSendControlStreamTest : public QuicTestWithParam<TestParams> {
 public:
  QuicSendControlStreamTest()
      : connection_(new StrictMock<MockQuicConnection>(
            &helper_,
            &alarm_factory_,
            perspective(),
            SupportedVersions(GetParam().version))),
        session_(connection_) {
    session_.Initialize();
    send_control_stream_ = QuicMakeUnique<QuicSendControlStream>(
        QuicSpdySessionPeer::GetNextOutgoingUnidirectionalStreamId(&session_),
        &session_, /* max_inbound_header_list_size = */ 100);
    ON_CALL(session_, WritevData(_, _, _, _, _))
        .WillByDefault(Invoke(MockQuicSession::ConsumeData));
  }

  Perspective perspective() const { return GetParam().perspective; }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSpdySession> session_;
  HttpEncoder encoder_;
  std::unique_ptr<QuicSendControlStream> send_control_stream_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSendControlStreamTest,
                         ::testing::ValuesIn(GetTestParams()));

TEST_P(QuicSendControlStreamTest, WriteSettingsOnlyForOnce) {
  if (GetParam().version.handshake_protocol == PROTOCOL_TLS1_3) {
    // TODO(nharper, b/112643533): Figure out why this test fails when TLS is
    // enabled and fix it.
    return;
  }
  testing::InSequence s;

  EXPECT_CALL(session_, WritevData(_, _, 1, _, _));
  EXPECT_CALL(session_, WritevData(_, _, _, _, _));
  send_control_stream_->SendSettingsFrame();

  // No data should be written the sencond time SendSettingsFrame() is called.
  send_control_stream_->SendSettingsFrame();
}

TEST_P(QuicSendControlStreamTest, WritePriorityBeforeSettings) {
  if (GetParam().version.handshake_protocol == PROTOCOL_TLS1_3) {
    // TODO(nharper, b/112643533): Figure out why this test fails when TLS is
    // enabled and fix it.
    return;
  }

  testing::InSequence s;

  // The first write will trigger the control stream to write stream type and a
  // Settings frame before the Priority frame.
  EXPECT_CALL(session_, WritevData(_, send_control_stream_->id(), _, _, _))
      .Times(3);
  PriorityFrame frame;
  send_control_stream_->WritePriority(frame);

  EXPECT_CALL(session_, WritevData(_, send_control_stream_->id(), _, _, _));
  send_control_stream_->WritePriority(frame);
}

TEST_P(QuicSendControlStreamTest, ResetControlStream) {
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId,
                               send_control_stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  send_control_stream_->OnStreamReset(rst_frame);
}

TEST_P(QuicSendControlStreamTest, ReceiveDataOnSendControlStream) {
  QuicStreamFrame frame(send_control_stream_->id(), false, 0, "test");
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM, _, _));
  send_control_stream_->OnStreamFrame(frame);
}

}  // namespace
}  // namespace test
}  // namespace quic
