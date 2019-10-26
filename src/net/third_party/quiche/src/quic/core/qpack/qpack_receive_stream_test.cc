// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_receive_stream.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

namespace {
using ::testing::_;
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

class QpackReceiveStreamTest : public QuicTestWithParam<TestParams> {
 public:
  QpackReceiveStreamTest()
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
    char type[] = {0x03};
    QuicStreamFrame data1(id, false, 0, QuicStringPiece(type, 1));
    session_.OnStreamFrame(data1);
    qpack_receive_stream_ =
        QuicSpdySessionPeer::GetQpackDecoderReceiveStream(&session_);
  }

  Perspective perspective() const { return GetParam().perspective; }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSpdySession> session_;
  QpackReceiveStream* qpack_receive_stream_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QpackReceiveStreamTest,
                         ::testing::ValuesIn(GetTestParams()));

TEST_P(QpackReceiveStreamTest, ResetQpackReceiveStream) {
  EXPECT_TRUE(qpack_receive_stream_->is_static());
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId,
                               qpack_receive_stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  qpack_receive_stream_->OnStreamReset(rst_frame);
}

}  // namespace
}  // namespace test
}  // namespace quic
