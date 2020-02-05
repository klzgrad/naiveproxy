// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
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
    QUIC_LOG(INFO) << "TestParams: " << *this;
  }

  TestParams(const TestParams& other)
      : version(other.version), perspective(other.perspective) {}

  friend std::ostream& operator<<(std::ostream& os, const TestParams& tp) {
    os << "{ version: " << ParsedQuicVersionToString(tp.version)
       << ", perspective: "
       << (tp.perspective == Perspective::IS_CLIENT ? "client" : "server")
       << "}";
    return os;
  }

  ParsedQuicVersion version;
  Perspective perspective;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& tp) {
  return QuicStrCat(
      ParsedQuicVersionToString(tp.version), "_",
      (tp.perspective == Perspective::IS_CLIENT ? "client" : "server"));
}

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  ParsedQuicVersionVector all_supported_versions = AllSupportedVersions();
  for (const auto& version : AllSupportedVersions()) {
    if (!VersionUsesHttp3(version.transport_version)) {
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
    ON_CALL(session_, WritevData(_, _, _, _, _))
        .WillByDefault(Invoke(MockQuicSession::ConsumeData));
  }

  void Initialize() {
    session_.Initialize();
    send_control_stream_ = QuicSpdySessionPeer::GetSendControlStream(&session_);
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedMaxIncomingUnidirectionalStreams(
        session_.config(), 3);
    session_.OnConfigNegotiated();
  }

  Perspective perspective() const { return GetParam().perspective; }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSpdySession> session_;
  QuicSendControlStream* send_control_stream_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSendControlStreamTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSendControlStreamTest, WriteSettings) {
  session_.set_qpack_maximum_dynamic_table_capacity(255);
  session_.set_qpack_maximum_blocked_streams(16);
  session_.set_max_inbound_header_list_size(1024);

  Initialize();
  testing::InSequence s;

  std::string expected_write_data = QuicTextUtils::HexDecode(
      "00"    // stream type: control stream
      "04"    // frame type: SETTINGS frame
      "08"    // frame length
      "01"    // SETTINGS_QPACK_MAX_TABLE_CAPACITY
      "40ff"  // 255
      "06"    // SETTINGS_MAX_HEADER_LIST_SIZE
      "4400"  // 1024
      "07"    // SETTINGS_QPACK_BLOCKED_STREAMS
      "10");  // 16

  auto buffer = std::make_unique<char[]>(expected_write_data.size());
  QuicDataWriter writer(expected_write_data.size(), buffer.get());

  // A lambda to save and consume stream data when QuicSession::WritevData() is
  // called.
  auto save_write_data = [&writer](QuicStream* stream, QuicStreamId /*id*/,
                                   size_t write_length, QuicStreamOffset offset,
                                   StreamSendingState /*state*/) {
    stream->WriteStreamData(offset, write_length, &writer);
    return QuicConsumedData(/* bytes_consumed = */ write_length,
                            /* fin_consumed = */ false);
  };

  EXPECT_CALL(session_, WritevData(send_control_stream_, _, 1, _, _))
      .WillOnce(Invoke(save_write_data));
  EXPECT_CALL(session_, WritevData(send_control_stream_, _,
                                   expected_write_data.size() - 1, _, _))
      .WillOnce(Invoke(save_write_data));

  send_control_stream_->MaybeSendSettingsFrame();
  EXPECT_EQ(expected_write_data,
            QuicStringPiece(writer.data(), writer.length()));
}

TEST_P(QuicSendControlStreamTest, WriteSettingsOnlyOnce) {
  Initialize();
  testing::InSequence s;

  EXPECT_CALL(session_, WritevData(send_control_stream_, _, 1, _, _));
  EXPECT_CALL(session_, WritevData(send_control_stream_, _, _, _, _));
  send_control_stream_->MaybeSendSettingsFrame();

  // No data should be written the sencond time MaybeSendSettingsFrame() is
  // called.
  send_control_stream_->MaybeSendSettingsFrame();
}

TEST_P(QuicSendControlStreamTest, WritePriorityBeforeSettings) {
  Initialize();
  testing::InSequence s;

  // The first write will trigger the control stream to write stream type and a
  // Settings frame before the Priority frame.
  EXPECT_CALL(session_, WritevData(send_control_stream_, _, _, _, _)).Times(3);
  PriorityFrame frame;
  send_control_stream_->WritePriority(frame);

  EXPECT_CALL(session_, WritevData(send_control_stream_, _, _, _, _));
  send_control_stream_->WritePriority(frame);
}

TEST_P(QuicSendControlStreamTest, ResetControlStream) {
  Initialize();
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId,
                               send_control_stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  send_control_stream_->OnStreamReset(rst_frame);
}

TEST_P(QuicSendControlStreamTest, ReceiveDataOnSendControlStream) {
  Initialize();
  QuicStreamFrame frame(send_control_stream_->id(), false, 0, "test");
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM, _, _));
  send_control_stream_->OnStreamFrame(frame);
}

}  // namespace
}  // namespace test
}  // namespace quic
