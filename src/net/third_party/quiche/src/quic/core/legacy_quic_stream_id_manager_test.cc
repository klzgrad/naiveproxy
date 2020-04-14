// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/legacy_quic_stream_id_manager.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {
namespace test {
namespace {

using testing::_;
using testing::StrictMock;

struct TestParams {
  TestParams(ParsedQuicVersion version, Perspective perspective)
      : version(version), perspective(perspective) {}

  ParsedQuicVersion version;
  Perspective perspective;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  return quiche::QuicheStrCat(
      ParsedQuicVersionToString(p.version),
      (p.perspective == Perspective::IS_CLIENT ? "Client" : "Server"));
}

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  for (ParsedQuicVersion version : AllSupportedVersions()) {
    for (auto perspective : {Perspective::IS_CLIENT, Perspective::IS_SERVER}) {
      // LegacyQuicStreamIdManager is only used when IETF QUIC frames are not
      // presented.
      if (!VersionHasIetfQuicFrames(version.transport_version)) {
        params.push_back(TestParams(version, perspective));
      }
    }
  }
  return params;
}

class LegacyQuicStreamIdManagerTest : public QuicTestWithParam<TestParams> {
 public:
  LegacyQuicStreamIdManagerTest()
      : manager_(GetParam().perspective,
                 GetParam().version.transport_version,
                 kDefaultMaxStreamsPerConnection,
                 kDefaultMaxStreamsPerConnection) {}

 protected:
  QuicStreamId GetNthPeerInitiatedId(int n) {
    if (GetParam().perspective == Perspective::IS_SERVER) {
      return QuicUtils::GetFirstBidirectionalStreamId(
                 GetParam().version.transport_version, Perspective::IS_CLIENT) +
             2 * n;
    } else {
      return 2 + 2 * n;
    }
  }

  LegacyQuicStreamIdManager manager_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         LegacyQuicStreamIdManagerTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(LegacyQuicStreamIdManagerTest, CanOpenNextOutgoingStream) {
  EXPECT_TRUE(manager_.CanOpenNextOutgoingStream(
      manager_.max_open_outgoing_streams() - 1));
  EXPECT_FALSE(
      manager_.CanOpenNextOutgoingStream(manager_.max_open_outgoing_streams()));
}

TEST_P(LegacyQuicStreamIdManagerTest, CanOpenIncomingStream) {
  EXPECT_TRUE(
      manager_.CanOpenIncomingStream(manager_.max_open_incoming_streams() - 1));
  EXPECT_FALSE(
      manager_.CanOpenIncomingStream(manager_.max_open_incoming_streams()));
}

TEST_P(LegacyQuicStreamIdManagerTest, AvailableStreams) {
  ASSERT_TRUE(
      manager_.MaybeIncreaseLargestPeerStreamId(GetNthPeerInitiatedId(3)));
  EXPECT_TRUE(manager_.IsAvailableStream(GetNthPeerInitiatedId(1)));
  EXPECT_TRUE(manager_.IsAvailableStream(GetNthPeerInitiatedId(2)));
  ASSERT_TRUE(
      manager_.MaybeIncreaseLargestPeerStreamId(GetNthPeerInitiatedId(2)));
  ASSERT_TRUE(
      manager_.MaybeIncreaseLargestPeerStreamId(GetNthPeerInitiatedId(1)));
}

TEST_P(LegacyQuicStreamIdManagerTest, MaxAvailableStreams) {
  // Test that the server closes the connection if a client makes too many data
  // streams available.  The server accepts slightly more than the negotiated
  // stream limit to deal with rare cases where a client FIN/RST is lost.
  const size_t kMaxStreamsForTest = 10;
  const size_t kAvailableStreamLimit = manager_.MaxAvailableStreams();
  EXPECT_EQ(
      manager_.max_open_incoming_streams() * kMaxAvailableStreamsMultiplier,
      manager_.MaxAvailableStreams());
  // The protocol specification requires that there can be at least 10 times
  // as many available streams as the connection's maximum open streams.
  EXPECT_LE(10 * kMaxStreamsForTest, kAvailableStreamLimit);

  EXPECT_TRUE(
      manager_.MaybeIncreaseLargestPeerStreamId(GetNthPeerInitiatedId(0)));

  // Establish available streams up to the server's limit.
  const int kLimitingStreamId =
      GetNthPeerInitiatedId(kAvailableStreamLimit + 1);
  // This exceeds the stream limit. In versions other than 99
  // this is allowed. Version 99 hews to the IETF spec and does
  // not allow it.
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(kLimitingStreamId));

  // This forces stream kLimitingStreamId + 2 to become available, which
  // violates the quota.
  EXPECT_FALSE(
      manager_.MaybeIncreaseLargestPeerStreamId(kLimitingStreamId + 2 * 2));
}

TEST_P(LegacyQuicStreamIdManagerTest, MaximumAvailableOpenedStreams) {
  QuicStreamId stream_id = GetNthPeerInitiatedId(0);
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(stream_id));

  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      stream_id + 2 * (manager_.max_open_incoming_streams() - 1)));
}

TEST_P(LegacyQuicStreamIdManagerTest, TooManyAvailableStreams) {
  QuicStreamId stream_id = GetNthPeerInitiatedId(0);
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(stream_id));

  // A stream ID which is too large to create.
  QuicStreamId stream_id2 =
      GetNthPeerInitiatedId(2 * manager_.MaxAvailableStreams() + 4);
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(stream_id2));
}

TEST_P(LegacyQuicStreamIdManagerTest, ManyAvailableStreams) {
  // When max_open_streams_ is 200, should be able to create 200 streams
  // out-of-order, that is, creating the one with the largest stream ID first.
  manager_.set_max_open_incoming_streams(200);
  QuicStreamId stream_id = GetNthPeerInitiatedId(0);
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(stream_id));

  // Create the largest stream ID of a threatened total of 200 streams.
  // GetNth... starts at 0, so for 200 streams, get the 199th.
  EXPECT_TRUE(
      manager_.MaybeIncreaseLargestPeerStreamId(GetNthPeerInitiatedId(199)));
}

TEST_P(LegacyQuicStreamIdManagerTest,
       TestMaxIncomingAndOutgoingStreamsAllowed) {
  EXPECT_EQ(manager_.max_open_incoming_streams(),
            kDefaultMaxStreamsPerConnection);
  EXPECT_EQ(manager_.max_open_outgoing_streams(),
            kDefaultMaxStreamsPerConnection);
}

}  // namespace
}  // namespace test
}  // namespace quic
