// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

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
using testing::Invoke;
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
        &session_);
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

TEST_P(QuicSendControlStreamTest, WriteSettingsOnStartUp) {
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[6] = 5;
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializeSettingsFrame(settings, &buffer);

  EXPECT_CALL(session_, WritevData(_, _, frame_length, _, _));
  send_control_stream_->SendSettingsFrame(settings);
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
