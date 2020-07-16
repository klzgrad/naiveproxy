// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {

namespace {

using ::testing::_;
using ::testing::AnyNumber;
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
  return quiche::QuicheStrCat(
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
    ON_CALL(session_, WritevData(_, _, _, _, _, _))
        .WillByDefault(Invoke(&session_, &MockQuicSpdySession::ConsumeData));
  }

  void Initialize() {
    session_.Initialize();
    send_control_stream_ = QuicSpdySessionPeer::GetSendControlStream(&session_);
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(session_.config(), 3);
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
  SetQuicFlag(FLAGS_quic_enable_http3_grease_randomness, false);
  session_.set_qpack_maximum_dynamic_table_capacity(255);
  session_.set_qpack_maximum_blocked_streams(16);
  session_.set_max_inbound_header_list_size(1024);

  Initialize();
  testing::InSequence s;

  std::string expected_write_data = quiche::QuicheTextUtils::HexDecode(
      "00"    // stream type: control stream
      "04"    // frame type: SETTINGS frame
      "0b"    // frame length
      "01"    // SETTINGS_QPACK_MAX_TABLE_CAPACITY
      "40ff"  // 255
      "06"    // SETTINGS_MAX_HEADER_LIST_SIZE
      "4400"  // 1024
      "07"    // SETTINGS_QPACK_BLOCKED_STREAMS
      "10"    // 16
      "4040"  // 0x40 as the reserved settings id
      "14"    // 20
      "4040"  // 0x40 as the reserved frame type
      "01"    // 1 byte frame length
      "61");  //  payload "a"

  auto buffer = std::make_unique<char[]>(expected_write_data.size());
  QuicDataWriter writer(expected_write_data.size(), buffer.get());

  // A lambda to save and consume stream data when QuicSession::WritevData() is
  // called.
  auto save_write_data =
      [&writer, this](QuicStreamId /*id*/, size_t write_length,
                      QuicStreamOffset offset, StreamSendingState /*state*/,
                      TransmissionType /*type*/,
                      quiche::QuicheOptional<EncryptionLevel> /*level*/) {
        send_control_stream_->WriteStreamData(offset, write_length, &writer);
        return QuicConsumedData(/* bytes_consumed = */ write_length,
                                /* fin_consumed = */ false);
      };

  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), 1, _, _, _, _))
      .WillOnce(Invoke(save_write_data));
  EXPECT_CALL(session_, WritevData(send_control_stream_->id(),
                                   expected_write_data.size() - 5, _, _, _, _))
      .WillOnce(Invoke(save_write_data));
  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), 4, _, _, _, _))
      .WillOnce(Invoke(save_write_data));

  send_control_stream_->MaybeSendSettingsFrame();
  EXPECT_EQ(expected_write_data,
            quiche::QuicheStringPiece(writer.data(), writer.length()));
}

TEST_P(QuicSendControlStreamTest, WriteSettingsOnlyOnce) {
  Initialize();
  testing::InSequence s;

  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), 1, _, _, _, _));
  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), _, _, _, _, _))
      .Times(2);
  send_control_stream_->MaybeSendSettingsFrame();

  // No data should be written the second time MaybeSendSettingsFrame() is
  // called.
  send_control_stream_->MaybeSendSettingsFrame();
}

// Send stream type and SETTINGS frame if WritePriorityUpdate() is called first.
TEST_P(QuicSendControlStreamTest, WritePriorityBeforeSettings) {
  Initialize();
  testing::InSequence s;

  // The first write will trigger the control stream to write stream type, a
  // SETTINGS frame, and a greased frame before the PRIORITY_UPDATE frame.
  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), _, _, _, _, _))
      .Times(4);
  PriorityUpdateFrame frame;
  send_control_stream_->WritePriorityUpdate(frame);

  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), _, _, _, _, _));
  send_control_stream_->WritePriorityUpdate(frame);
}

TEST_P(QuicSendControlStreamTest, CloseControlStream) {
  Initialize();
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM, _, _));
  send_control_stream_->OnStopSending(QUIC_STREAM_CANCELLED);
}

TEST_P(QuicSendControlStreamTest, ReceiveDataOnSendControlStream) {
  Initialize();
  QuicStreamFrame frame(send_control_stream_->id(), false, 0, "test");
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM, _, _));
  send_control_stream_->OnStreamFrame(frame);
}

TEST_P(QuicSendControlStreamTest, SendGoAway) {
  Initialize();

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  QuicStreamId stream_id = 4;

  EXPECT_CALL(session_, WritevData(send_control_stream_->id(), _, _, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(debug_visitor, OnSettingsFrameSent(_));
  EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(stream_id));

  send_control_stream_->SendGoAway(stream_id);
}

}  // namespace
}  // namespace test
}  // namespace quic
