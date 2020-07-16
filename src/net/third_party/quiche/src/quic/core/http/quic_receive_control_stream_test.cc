// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_encoder_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

class QpackEncoder;

namespace test {

namespace {
using ::testing::_;
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

class TestStream : public QuicSpdyStream {
 public:
  TestStream(QuicStreamId id, QuicSpdySession* session)
      : QuicSpdyStream(id, session, BIDIRECTIONAL) {}
  ~TestStream() override = default;

  void OnBodyAvailable() override {}
};

class QuicReceiveControlStreamTest : public QuicTestWithParam<TestParams> {
 public:
  QuicReceiveControlStreamTest()
      : connection_(new StrictMock<MockQuicConnection>(
            &helper_,
            &alarm_factory_,
            perspective(),
            SupportedVersions(GetParam().version))),
        session_(connection_) {
    session_.Initialize();
    QuicStreamId id = perspective() == Perspective::IS_SERVER
                          ? GetNthClientInitiatedUnidirectionalStreamId(
                                session_.transport_version(), 3)
                          : GetNthServerInitiatedUnidirectionalStreamId(
                                session_.transport_version(), 3);
    char type[] = {kControlStream};

    QuicStreamFrame data1(id, false, 0, quiche::QuicheStringPiece(type, 1));
    session_.OnStreamFrame(data1);

    receive_control_stream_ =
        QuicSpdySessionPeer::GetReceiveControlStream(&session_);

    stream_ = new TestStream(GetNthClientInitiatedBidirectionalStreamId(
                                 GetParam().version.transport_version, 0),
                             &session_);
    session_.ActivateStream(QuicWrapUnique(stream_));
  }

  Perspective perspective() const { return GetParam().perspective; }

  std::string EncodeSettings(const SettingsFrame& settings) {
    std::unique_ptr<char[]> buffer;
    QuicByteCount settings_frame_length =
        HttpEncoder::SerializeSettingsFrame(settings, &buffer);
    return std::string(buffer.get(), settings_frame_length);
  }

  std::string SerializePriorityUpdateFrame(
      const PriorityUpdateFrame& priority_update) {
    std::unique_ptr<char[]> priority_buffer;
    QuicByteCount priority_frame_length =
        HttpEncoder::SerializePriorityUpdateFrame(priority_update,
                                                  &priority_buffer);
    return std::string(priority_buffer.get(), priority_frame_length);
  }

  QuicStreamOffset NumBytesConsumed() {
    return QuicStreamPeer::sequencer(receive_control_stream_)
        ->NumBytesConsumed();
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSpdySession> session_;
  QuicReceiveControlStream* receive_control_stream_;
  TestStream* stream_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicReceiveControlStreamTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicReceiveControlStreamTest, ResetControlStream) {
  EXPECT_TRUE(receive_control_stream_->is_static());
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId,
                               receive_control_stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM, _, _));
  receive_control_stream_->OnStreamReset(rst_frame);
}

TEST_P(QuicReceiveControlStreamTest, ReceiveSettings) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] = 5;
  settings.values[SETTINGS_QPACK_BLOCKED_STREAMS] = 12;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 37;
  std::string data = EncodeSettings(settings);
  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data);

  QpackEncoder* qpack_encoder = session_.qpack_encoder();
  QpackHeaderTable* header_table =
      QpackEncoderPeer::header_table(qpack_encoder);
  EXPECT_EQ(std::numeric_limits<size_t>::max(),
            session_.max_outbound_header_list_size());
  EXPECT_EQ(0u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));
  EXPECT_EQ(0u, header_table->maximum_dynamic_table_capacity());

  receive_control_stream_->OnStreamFrame(frame);

  EXPECT_EQ(5u, session_.max_outbound_header_list_size());
  EXPECT_EQ(12u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));
  EXPECT_EQ(37u, header_table->maximum_dynamic_table_capacity());
}

// Regression test for https://crbug.com/982648.
// QuicReceiveControlStream::OnDataAvailable() must stop processing input as
// soon as OnSettingsFrameStart() is called by HttpDecoder for the second frame.
TEST_P(QuicReceiveControlStreamTest, ReceiveSettingsTwice) {
  SettingsFrame settings;
  // Reserved identifiers, must be ignored.
  settings.values[0x21] = 100;
  settings.values[0x40] = 200;

  std::string settings_frame = EncodeSettings(settings);

  QuicStreamOffset offset = 1;
  EXPECT_EQ(offset, NumBytesConsumed());

  // Receive first SETTINGS frame.
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false, offset,
                      settings_frame));
  offset += settings_frame.length();

  // First SETTINGS frame is consumed.
  EXPECT_EQ(offset, NumBytesConsumed());

  // Second SETTINGS frame causes the connection to be closed.
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_CONTROL_STREAM,
                      "Settings frames are received twice.", _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));

  // Receive second SETTINGS frame.
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false, offset,
                      settings_frame));

  // Frame header of second SETTINGS frame is consumed, but not frame payload.
  QuicByteCount settings_frame_header_length = 2;
  EXPECT_EQ(offset + settings_frame_header_length, NumBytesConsumed());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveSettingsFragments) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] = 5;
  std::string data = EncodeSettings(settings);
  std::string data1 = data.substr(0, 1);
  std::string data2 = data.substr(1, data.length() - 1);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data1);
  QuicStreamFrame frame2(receive_control_stream_->id(), false, 2, data2);
  EXPECT_NE(5u, session_.max_outbound_header_list_size());
  receive_control_stream_->OnStreamFrame(frame);
  receive_control_stream_->OnStreamFrame(frame2);
  EXPECT_EQ(5u, session_.max_outbound_header_list_size());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveWrongFrame) {
  // DATA frame header without payload.
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(/* payload_length = */ 2, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data);
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM, _, _));
  receive_control_stream_->OnStreamFrame(frame);
}

TEST_P(QuicReceiveControlStreamTest,
       ReceivePriorityUpdateFrameBeforeSettingsFrame) {
  std::string serialized_frame = SerializePriorityUpdateFrame({});
  QuicStreamFrame data(receive_control_stream_->id(), /* fin = */ false,
                       /* offset = */ 1, serialized_frame);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_MISSING_SETTINGS_FRAME,
                      "PRIORITY_UPDATE frame received before SETTINGS.", _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));

  receive_control_stream_->OnStreamFrame(data);
}

TEST_P(QuicReceiveControlStreamTest, ReceiveGoAwayFrame) {
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  QuicStreamOffset offset = 1;

  // Receive SETTINGS frame.
  SettingsFrame settings;
  std::string settings_frame = EncodeSettings(settings);
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(settings));
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false, offset,
                      settings_frame));
  offset += settings_frame.length();

  GoAwayFrame goaway{/* stream_id = */ 0};

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeGoAwayFrame(goaway, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  QuicStreamFrame frame(receive_control_stream_->id(), false, offset, data);
  EXPECT_FALSE(session_.http3_goaway_received());

  EXPECT_CALL(debug_visitor, OnGoAwayFrameReceived(goaway));

  if (perspective() == Perspective::IS_SERVER) {
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM, _, _));
  }

  receive_control_stream_->OnStreamFrame(frame);
  if (perspective() == Perspective::IS_CLIENT) {
    EXPECT_TRUE(session_.http3_goaway_received());
  }
}

TEST_P(QuicReceiveControlStreamTest, PushPromiseOnControlStreamShouldClose) {
  PushPromiseFrame push_promise;
  push_promise.push_id = 0x01;
  push_promise.headers = "Headers";
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializePushPromiseFrameWithOnlyPushId(
      push_promise, &buffer);
  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, buffer.get(),
                        length);
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM, _, _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));
  receive_control_stream_->OnStreamFrame(frame);
}

// Regression test for b/137554973: unknown frames should be consumed.
TEST_P(QuicReceiveControlStreamTest, ConsumeUnknownFrame) {
  EXPECT_EQ(1u, NumBytesConsumed());

  QuicStreamOffset offset = 1;

  // Receive SETTINGS frame.
  std::string settings_frame = EncodeSettings({});
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false, offset,
                      settings_frame));
  offset += settings_frame.length();

  // SETTINGS frame is consumed.
  EXPECT_EQ(offset, NumBytesConsumed());

  // Receive unknown frame.
  std::string unknown_frame = quiche::QuicheTextUtils::HexDecode(
      "21"        // reserved frame type
      "03"        // payload length
      "666f6f");  // payload "foo"

  receive_control_stream_->OnStreamFrame(QuicStreamFrame(
      receive_control_stream_->id(), /* fin = */ false, offset, unknown_frame));
  offset += unknown_frame.size();

  // Unknown frame is consumed.
  EXPECT_EQ(offset, NumBytesConsumed());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveUnknownFrame) {
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  const QuicStreamId id = receive_control_stream_->id();
  QuicStreamOffset offset = 1;

  // Receive SETTINGS frame.
  SettingsFrame settings;
  std::string settings_frame = EncodeSettings(settings);
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(settings));
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(id, /* fin = */ false, offset, settings_frame));
  offset += settings_frame.length();

  // Receive unknown frame.
  std::string unknown_frame = quiche::QuicheTextUtils::HexDecode(
      "21"        // reserved frame type
      "03"        // payload length
      "666f6f");  // payload "foo"

  EXPECT_CALL(debug_visitor, OnUnknownFrameReceived(id, /* frame_type = */ 0x21,
                                                    /* payload_length = */ 3));
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(id, /* fin = */ false, offset, unknown_frame));
}

TEST_P(QuicReceiveControlStreamTest, CancelPushFrameBeforeSettings) {
  std::string cancel_push_frame = quiche::QuicheTextUtils::HexDecode(
      "03"    // type CANCEL_PUSH
      "01"    // payload length
      "01");  // push ID

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_MISSING_SETTINGS_FRAME,
                              "CANCEL_PUSH frame received before SETTINGS.", _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));

  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false,
                      /* offset = */ 1, cancel_push_frame));
}

TEST_P(QuicReceiveControlStreamTest, UnknownFrameBeforeSettings) {
  std::string unknown_frame = quiche::QuicheTextUtils::HexDecode(
      "21"        // reserved frame type
      "03"        // payload length
      "666f6f");  // payload "foo"

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_MISSING_SETTINGS_FRAME,
                              "Unknown frame received before SETTINGS.", _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));

  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false,
                      /* offset = */ 1, unknown_frame));
}

}  // namespace
}  // namespace test
}  // namespace quic
