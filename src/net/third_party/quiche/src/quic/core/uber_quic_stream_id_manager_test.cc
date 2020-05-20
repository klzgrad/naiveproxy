// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/uber_quic_stream_id_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

struct TestParams {
  explicit TestParams(ParsedQuicVersion version, Perspective perspective)
      : version(version), perspective(perspective) {}

  ParsedQuicVersion version;
  Perspective perspective;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  return quiche::QuicheStrCat(
      ParsedQuicVersionToString(p.version), "_",
      (p.perspective == Perspective::IS_CLIENT ? "client" : "server"));
}

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (!version.HasIetfQuicFrames()) {
      continue;
    }
    params.push_back(TestParams(version, Perspective::IS_CLIENT));
    params.push_back(TestParams(version, Perspective::IS_SERVER));
  }
  return params;
}

class MockDelegate : public QuicStreamIdManager::DelegateInterface {
 public:
  MOCK_METHOD2(SendMaxStreams,
               void(QuicStreamCount stream_count, bool unidirectional));
};

class UberQuicStreamIdManagerTest : public QuicTestWithParam<TestParams> {
 protected:
  UberQuicStreamIdManagerTest()
      : manager_(perspective(),
                 version(),
                 &delegate_,
                 0,
                 0,
                 kDefaultMaxStreamsPerConnection,
                 kDefaultMaxStreamsPerConnection) {}

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(transport_version(),
                                                    Perspective::IS_CLIENT) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthClientInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(transport_version(),
                                                     Perspective::IS_CLIENT) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(transport_version(),
                                                    Perspective::IS_SERVER) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthServerInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(transport_version(),
                                                     Perspective::IS_SERVER) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthPeerInitiatedBidirectionalStreamId(int n) {
    return ((perspective() == Perspective::IS_SERVER)
                ? GetNthClientInitiatedBidirectionalId(n)
                : GetNthServerInitiatedBidirectionalId(n));
  }
  QuicStreamId GetNthPeerInitiatedUnidirectionalStreamId(int n) {
    return ((perspective() == Perspective::IS_SERVER)
                ? GetNthClientInitiatedUnidirectionalId(n)
                : GetNthServerInitiatedUnidirectionalId(n));
  }
  QuicStreamId GetNthSelfInitiatedBidirectionalStreamId(int n) {
    return ((perspective() == Perspective::IS_CLIENT)
                ? GetNthClientInitiatedBidirectionalId(n)
                : GetNthServerInitiatedBidirectionalId(n));
  }
  QuicStreamId GetNthSelfInitiatedUnidirectionalStreamId(int n) {
    return ((perspective() == Perspective::IS_CLIENT)
                ? GetNthClientInitiatedUnidirectionalId(n)
                : GetNthServerInitiatedUnidirectionalId(n));
  }

  QuicStreamId StreamCountToId(QuicStreamCount stream_count,
                               Perspective perspective,
                               bool bidirectional) {
    return ((bidirectional) ? QuicUtils::GetFirstBidirectionalStreamId(
                                  transport_version(), perspective)
                            : QuicUtils::GetFirstUnidirectionalStreamId(
                                  transport_version(), perspective)) +
           ((stream_count - 1) * QuicUtils::StreamIdDelta(transport_version()));
  }

  ParsedQuicVersion version() { return GetParam().version; }
  QuicTransportVersion transport_version() {
    return version().transport_version;
  }

  Perspective perspective() { return GetParam().perspective; }

  testing::StrictMock<MockDelegate> delegate_;
  UberQuicStreamIdManager manager_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         UberQuicStreamIdManagerTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(UberQuicStreamIdManagerTest, Initialization) {
  EXPECT_EQ(GetNthSelfInitiatedBidirectionalStreamId(0),
            manager_.next_outgoing_bidirectional_stream_id());
  EXPECT_EQ(GetNthSelfInitiatedUnidirectionalStreamId(0),
            manager_.next_outgoing_unidirectional_stream_id());
}

TEST_P(UberQuicStreamIdManagerTest, SetMaxOpenOutgoingStreams) {
  const size_t kNumMaxOutgoingStream = 123;
  // Set the uni- and bi- directional limits to different values to ensure
  // that they are managed separately.
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingBidirectionalStreams(
      kNumMaxOutgoingStream));
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingUnidirectionalStreams(
      kNumMaxOutgoingStream + 1));
  EXPECT_EQ(kNumMaxOutgoingStream,
            manager_.max_outgoing_bidirectional_streams());
  EXPECT_EQ(kNumMaxOutgoingStream + 1,
            manager_.max_outgoing_unidirectional_streams());
  // Check that, for each directionality, we can open the correct number of
  // streams.
  int i = kNumMaxOutgoingStream;
  while (i) {
    EXPECT_TRUE(manager_.CanOpenNextOutgoingBidirectionalStream());
    manager_.GetNextOutgoingBidirectionalStreamId();
    EXPECT_TRUE(manager_.CanOpenNextOutgoingUnidirectionalStream());
    manager_.GetNextOutgoingUnidirectionalStreamId();
    i--;
  }
  // One more unidirectional
  EXPECT_TRUE(manager_.CanOpenNextOutgoingUnidirectionalStream());
  manager_.GetNextOutgoingUnidirectionalStreamId();

  // Both should be exhausted...
  EXPECT_FALSE(manager_.CanOpenNextOutgoingUnidirectionalStream());
  EXPECT_FALSE(manager_.CanOpenNextOutgoingBidirectionalStream());
}

TEST_P(UberQuicStreamIdManagerTest, SetMaxOpenIncomingStreams) {
  const size_t kNumMaxIncomingStreams = 456;
  manager_.SetMaxOpenIncomingUnidirectionalStreams(kNumMaxIncomingStreams);
  // Do +1 for bidirectional to ensure that uni- and bi- get properly set.
  manager_.SetMaxOpenIncomingBidirectionalStreams(kNumMaxIncomingStreams + 1);
  EXPECT_EQ(kNumMaxIncomingStreams + 1,
            manager_.GetMaxAllowdIncomingBidirectionalStreams());
  EXPECT_EQ(kNumMaxIncomingStreams,
            manager_.GetMaxAllowdIncomingUnidirectionalStreams());
  EXPECT_EQ(manager_.max_incoming_bidirectional_streams(),
            manager_.advertised_max_incoming_bidirectional_streams());
  EXPECT_EQ(manager_.max_incoming_unidirectional_streams(),
            manager_.advertised_max_incoming_unidirectional_streams());
  // Make sure that we can create kNumMaxIncomingStreams incoming unidirectional
  // streams and kNumMaxIncomingStreams+1 incoming bidirectional streams.
  size_t i;
  for (i = 0; i < kNumMaxIncomingStreams; i++) {
    EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
        GetNthPeerInitiatedUnidirectionalStreamId(i), nullptr));
    EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
        GetNthPeerInitiatedBidirectionalStreamId(i), nullptr));
  }
  // Should be able to open the next bidirectional stream
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedBidirectionalStreamId(i), nullptr));

  // We should have exhausted the counts, the next streams should fail
  std::string error_details;
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedUnidirectionalStreamId(i), &error_details));
  EXPECT_EQ(error_details,
            quiche::QuicheStrCat(
                "Stream id ", GetNthPeerInitiatedUnidirectionalStreamId(i),
                " would exceed stream count limit ", kNumMaxIncomingStreams));
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedBidirectionalStreamId(i + 1), &error_details));
  EXPECT_EQ(
      error_details,
      quiche::QuicheStrCat(
          "Stream id ", GetNthPeerInitiatedBidirectionalStreamId(i + 1),
          " would exceed stream count limit ", kNumMaxIncomingStreams + 1));
}

TEST_P(UberQuicStreamIdManagerTest, GetNextOutgoingStreamId) {
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingBidirectionalStreams(10));
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingUnidirectionalStreams(10));
  EXPECT_EQ(GetNthSelfInitiatedBidirectionalStreamId(0),
            manager_.GetNextOutgoingBidirectionalStreamId());
  EXPECT_EQ(GetNthSelfInitiatedBidirectionalStreamId(1),
            manager_.GetNextOutgoingBidirectionalStreamId());
  EXPECT_EQ(GetNthSelfInitiatedUnidirectionalStreamId(0),
            manager_.GetNextOutgoingUnidirectionalStreamId());
  EXPECT_EQ(GetNthSelfInitiatedUnidirectionalStreamId(1),
            manager_.GetNextOutgoingUnidirectionalStreamId());
}

TEST_P(UberQuicStreamIdManagerTest, AvailableStreams) {
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedBidirectionalStreamId(3), nullptr));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedBidirectionalStreamId(1)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedBidirectionalStreamId(2)));

  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedUnidirectionalStreamId(3), nullptr));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedUnidirectionalStreamId(1)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedUnidirectionalStreamId(2)));
}

TEST_P(UberQuicStreamIdManagerTest, MaybeIncreaseLargestPeerStreamId) {
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      StreamCountToId(manager_.max_incoming_bidirectional_streams(),
                      QuicUtils::InvertPerspective(perspective()),
                      /* bidirectional=*/true),
      nullptr));
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      StreamCountToId(manager_.max_incoming_bidirectional_streams(),
                      QuicUtils::InvertPerspective(perspective()),
                      /* bidirectional=*/false),
      nullptr));

  std::string expected_error_details =
      perspective() == Perspective::IS_SERVER
          ? "Stream id 400 would exceed stream count limit 100"
          : "Stream id 401 would exceed stream count limit 100";
  std::string error_details;

  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      StreamCountToId(manager_.max_incoming_bidirectional_streams() + 1,
                      QuicUtils::InvertPerspective(perspective()),
                      /* bidirectional=*/true),
      &error_details));
  EXPECT_EQ(expected_error_details, error_details);
  expected_error_details =
      perspective() == Perspective::IS_SERVER
          ? "Stream id 402 would exceed stream count limit 100"
          : "Stream id 403 would exceed stream count limit 100";

  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      StreamCountToId(manager_.max_incoming_bidirectional_streams() + 1,
                      QuicUtils::InvertPerspective(perspective()),
                      /* bidirectional=*/false),
      &error_details));
  EXPECT_EQ(expected_error_details, error_details);
}

TEST_P(UberQuicStreamIdManagerTest, OnStreamsBlockedFrame) {
  QuicStreamCount stream_count =
      manager_.advertised_max_incoming_bidirectional_streams() - 1;

  QuicStreamsBlockedFrame frame(kInvalidControlFrameId, stream_count,
                                /*unidirectional=*/false);
  EXPECT_CALL(delegate_,
              SendMaxStreams(manager_.max_incoming_bidirectional_streams(),
                             frame.unidirectional));
  EXPECT_TRUE(manager_.OnStreamsBlockedFrame(frame, nullptr));

  stream_count = manager_.advertised_max_incoming_unidirectional_streams() - 1;
  frame.stream_count = stream_count;
  frame.unidirectional = true;

  EXPECT_CALL(delegate_,
              SendMaxStreams(manager_.max_incoming_unidirectional_streams(),
                             frame.unidirectional));
  EXPECT_TRUE(manager_.OnStreamsBlockedFrame(frame, nullptr));
}

TEST_P(UberQuicStreamIdManagerTest, IsIncomingStream) {
  EXPECT_TRUE(
      manager_.IsIncomingStream(GetNthPeerInitiatedBidirectionalStreamId(0)));
  EXPECT_TRUE(
      manager_.IsIncomingStream(GetNthPeerInitiatedUnidirectionalStreamId(0)));
  EXPECT_FALSE(
      manager_.IsIncomingStream(GetNthSelfInitiatedBidirectionalStreamId(0)));
  EXPECT_FALSE(
      manager_.IsIncomingStream(GetNthSelfInitiatedUnidirectionalStreamId(0)));
}

TEST_P(UberQuicStreamIdManagerTest, SetMaxOpenOutgoingStreamsPlusFrame) {
  const size_t kNumMaxOutgoingStream = 123;
  // Set the uni- and bi- directional limits to different values to ensure
  // that they are managed separately.
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingBidirectionalStreams(
      kNumMaxOutgoingStream));
  EXPECT_TRUE(manager_.MaybeAllowNewOutgoingUnidirectionalStreams(
      kNumMaxOutgoingStream + 1));
  EXPECT_EQ(kNumMaxOutgoingStream,
            manager_.max_outgoing_bidirectional_streams());
  EXPECT_EQ(kNumMaxOutgoingStream + 1,
            manager_.max_outgoing_unidirectional_streams());
  // Check that, for each directionality, we can open the correct number of
  // streams.
  int i = kNumMaxOutgoingStream;
  while (i) {
    EXPECT_TRUE(manager_.CanOpenNextOutgoingBidirectionalStream());
    manager_.GetNextOutgoingBidirectionalStreamId();
    EXPECT_TRUE(manager_.CanOpenNextOutgoingUnidirectionalStream());
    manager_.GetNextOutgoingUnidirectionalStreamId();
    i--;
  }
  // One more unidirectional
  EXPECT_TRUE(manager_.CanOpenNextOutgoingUnidirectionalStream());
  manager_.GetNextOutgoingUnidirectionalStreamId();

  // Both should be exhausted...
  EXPECT_FALSE(manager_.CanOpenNextOutgoingUnidirectionalStream());
  EXPECT_FALSE(manager_.CanOpenNextOutgoingBidirectionalStream());
}

}  // namespace
}  // namespace test
}  // namespace quic
