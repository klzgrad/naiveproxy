// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

namespace {
using testing::_;
using testing::StrictMock;

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
    receive_control_stream_ = QuicMakeUnique<QuicReceiveControlStream>(
        QuicUtils::GetFirstUnidirectionalStreamId(
            GetParam().version.transport_version,
            perspective() == Perspective::IS_CLIENT ? Perspective::IS_SERVER
                                                    : Perspective::IS_CLIENT),
        &session_);
  }

  Perspective perspective() const { return GetParam().perspective; }

  std::string EncodeSettings(const SettingsFrame& settings) {
    HttpEncoder encoder;
    std::unique_ptr<char[]> buffer;
    auto header_length = encoder.SerializeSettingsFrame(settings, &buffer);
    return std::string(buffer.get(), header_length);
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSpdySession> session_;
  HttpDecoder decoder_;
  std::unique_ptr<QuicReceiveControlStream> receive_control_stream_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicReceiveControlStreamTest,
                         ::testing::ValuesIn(GetTestParams()));

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
  settings.values[6] = 5;
  std::string data = EncodeSettings(settings);
  QuicStreamFrame frame(receive_control_stream_->id(), false, 0,
                        QuicStringPiece(data));
  EXPECT_NE(5u, session_.max_inbound_header_list_size());
  receive_control_stream_->OnStreamFrame(frame);
  EXPECT_EQ(5u, session_.max_inbound_header_list_size());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveSettingsTwice) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[6] = 5;
  std::string data = EncodeSettings(settings);
  QuicStreamFrame frame(receive_control_stream_->id(), false, 0,
                        QuicStringPiece(data));
  QuicStreamFrame frame2(receive_control_stream_->id(), false, data.length(),
                         QuicStringPiece(data));
  receive_control_stream_->OnStreamFrame(frame);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID,
                              "Settings frames are received twice.", _));
  receive_control_stream_->OnStreamFrame(frame2);
}

TEST_P(QuicReceiveControlStreamTest, ReceiveSettingsFragments) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[6] = 5;
  std::string data = EncodeSettings(settings);
  std::string data1 = data.substr(0, 1);
  std::string data2 = data.substr(1, data.length() - 1);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 0,
                        QuicStringPiece(data.data(), 1));
  QuicStreamFrame frame2(receive_control_stream_->id(), false, 1,
                         QuicStringPiece(data.data() + 1, data.length() - 1));
  EXPECT_NE(5u, session_.max_inbound_header_list_size());
  receive_control_stream_->OnStreamFrame(frame);
  receive_control_stream_->OnStreamFrame(frame2);
  EXPECT_EQ(5u, session_.max_inbound_header_list_size());
}

TEST_P(QuicReceiveControlStreamTest, ReceiveWrongFrame) {
  GoAwayFrame goaway;
  goaway.stream_id = 0x1;
  HttpEncoder encoder;
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length = encoder.SerializeGoAwayFrame(goaway, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  QuicStreamFrame frame(receive_control_stream_->id(), false, 0,
                        QuicStringPiece(data));
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_DECODER_ERROR, _, _));
  receive_control_stream_->OnStreamFrame(frame);
}

}  // namespace
}  // namespace test
}  // namespace quic
