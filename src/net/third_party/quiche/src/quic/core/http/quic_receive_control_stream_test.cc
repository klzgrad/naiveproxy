// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
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

    QuicStreamFrame data1(id, false, 0, QuicStringPiece(type, 1));
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

  std::string PriorityFrame(const PriorityFrame& frame) {
    std::unique_ptr<char[]> priority_buffer;
    QuicByteCount priority_frame_length =
        HttpEncoder::SerializePriorityFrame(frame, &priority_buffer);
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
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  receive_control_stream_->OnStreamReset(rst_frame);
}

TEST_P(QuicReceiveControlStreamTest, ReceiveSettings) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] = 5;
  std::string data = EncodeSettings(settings);
  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data);
  EXPECT_NE(5u, session_.max_outbound_header_list_size());
  receive_control_stream_->OnStreamFrame(frame);
  EXPECT_EQ(5u, session_.max_outbound_header_list_size());
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

  EXPECT_EQ(1u, NumBytesConsumed());

  // Receive first SETTINGS frame.
  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false,
                      /* offset = */ 1, settings_frame));

  // First SETTINGS frame is consumed.
  EXPECT_EQ(settings_frame.size() + 1, NumBytesConsumed());

  // Second SETTINGS frame causes the connection to be closed.
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID,
                              "Settings frames are received twice.", _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));

  // Receive second SETTINGS frame.
  receive_control_stream_->OnStreamFrame(QuicStreamFrame(
      receive_control_stream_->id(), /* fin = */ false,
      /* offset = */ settings_frame.size() + 1, settings_frame));

  // Frame header of second SETTINGS frame is consumed, but not frame payload.
  QuicByteCount settings_frame_header_length = 2;
  EXPECT_EQ(settings_frame.size() + settings_frame_header_length + 1,
            NumBytesConsumed());
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
  DuplicatePushFrame dup;
  dup.push_id = 0x1;
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDuplicatePushFrame(dup, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_DECODER_ERROR, _, _));
  receive_control_stream_->OnStreamFrame(frame);
}

TEST_P(QuicReceiveControlStreamTest, ReceivePriorityFrame) {
  if (perspective() == Perspective::IS_CLIENT) {
    return;
  }
  SetQuicFlag(FLAGS_quic_allow_http3_priority, true);
  struct PriorityFrame frame;
  frame.prioritized_type = REQUEST_STREAM;
  frame.dependency_type = ROOT_OF_TREE;
  frame.prioritized_element_id = stream_->id();
  frame.weight = 1;
  std::string serialized_frame = PriorityFrame(frame);
  QuicStreamFrame data(receive_control_stream_->id(), false, 1,
                       serialized_frame);

  EXPECT_EQ(3u, stream_->precedence().spdy3_priority());
  receive_control_stream_->OnStreamFrame(data);
  EXPECT_EQ(1u, stream_->precedence().spdy3_priority());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveGoAwayFrame) {
  GoAwayFrame goaway;
  goaway.stream_id = 0x00;

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeGoAwayFrame(goaway, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 1, data);
  EXPECT_FALSE(session_.http3_goaway_received());

  if (perspective() == Perspective::IS_SERVER) {
    EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_DECODER_ERROR, _, _));
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
  // TODO(lassey) Check for HTTP_WRONG_STREAM error code.
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_DECODER_ERROR, _, _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(session_, OnConnectionClosed(_, _));
  receive_control_stream_->OnStreamFrame(frame);
}

// Regression test for b/137554973: unknown frames should be consumed.
TEST_P(QuicReceiveControlStreamTest, ConsumeUnknownFrame) {
  std::string unknown_frame = QuicTextUtils::HexDecode(
      "21"        // reserved frame type
      "03"        // payload length
      "666f6f");  // payload "foo"

  EXPECT_EQ(1u, NumBytesConsumed());

  receive_control_stream_->OnStreamFrame(
      QuicStreamFrame(receive_control_stream_->id(), /* fin = */ false,
                      /* offset = */ 1, unknown_frame));

  EXPECT_EQ(unknown_frame.size() + 1, NumBytesConsumed());
}

}  // namespace
}  // namespace test
}  // namespace quic
