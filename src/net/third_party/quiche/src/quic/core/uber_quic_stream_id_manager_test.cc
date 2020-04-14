// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/uber_quic_stream_id_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QuicStreamIdManager::DelegateInterface {
 public:
  MOCK_METHOD1(OnCanCreateNewOutgoingStream, void(bool unidirectional));
  MOCK_METHOD2(OnError,
               void(QuicErrorCode error_code, std::string error_details));
  MOCK_METHOD2(SendMaxStreams,
               void(QuicStreamCount stream_count, bool unidirectional));
  MOCK_METHOD2(SendStreamsBlocked,
               void(QuicStreamCount stream_count, bool unidirectional));
};

class UberQuicStreamIdManagerTest : public QuicTestWithParam<Perspective> {
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
    return ((GetParam() == Perspective::IS_SERVER)
                ? GetNthClientInitiatedBidirectionalId(n)
                : GetNthServerInitiatedBidirectionalId(n));
  }
  QuicStreamId GetNthPeerInitiatedUnidirectionalStreamId(int n) {
    return ((GetParam() == Perspective::IS_SERVER)
                ? GetNthClientInitiatedUnidirectionalId(n)
                : GetNthServerInitiatedUnidirectionalId(n));
  }
  QuicStreamId GetNthSelfInitiatedBidirectionalStreamId(int n) {
    return ((GetParam() == Perspective::IS_CLIENT)
                ? GetNthClientInitiatedBidirectionalId(n)
                : GetNthServerInitiatedBidirectionalId(n));
  }
  QuicStreamId GetNthSelfInitiatedUnidirectionalStreamId(int n) {
    return ((GetParam() == Perspective::IS_CLIENT)
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

  ParsedQuicVersion version() { return {PROTOCOL_TLS1_3, transport_version()}; }
  QuicTransportVersion transport_version() { return QUIC_VERSION_99; }

  Perspective perspective() { return GetParam(); }

  testing::StrictMock<MockDelegate> delegate_;
  UberQuicStreamIdManager manager_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         UberQuicStreamIdManagerTest,
                         ::testing::ValuesIn({Perspective::IS_CLIENT,
                                              Perspective::IS_SERVER}),
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
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(false));
  manager_.SetMaxOpenOutgoingBidirectionalStreams(kNumMaxOutgoingStream);
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(true));
  manager_.SetMaxOpenOutgoingUnidirectionalStreams(kNumMaxOutgoingStream + 1);
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
        GetNthPeerInitiatedUnidirectionalStreamId(i)));
    EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
        GetNthPeerInitiatedBidirectionalStreamId(i)));
  }
  // Should be able to open the next bidirectional stream
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedBidirectionalStreamId(i)));

  // We should have exhausted the counts, the next streams should fail
  EXPECT_CALL(delegate_, OnError(QUIC_INVALID_STREAM_ID, _));
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedUnidirectionalStreamId(i)));
  EXPECT_CALL(delegate_, OnError(QUIC_INVALID_STREAM_ID, _));
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedBidirectionalStreamId(i + 1)));
}

TEST_P(UberQuicStreamIdManagerTest, GetNextOutgoingStreamId) {
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(_)).Times(2);
  manager_.SetMaxOpenOutgoingBidirectionalStreams(10);
  manager_.SetMaxOpenOutgoingUnidirectionalStreams(10);
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
      GetNthPeerInitiatedBidirectionalStreamId(3)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedBidirectionalStreamId(1)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedBidirectionalStreamId(2)));

  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(
      GetNthPeerInitiatedUnidirectionalStreamId(3)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedUnidirectionalStreamId(1)));
  EXPECT_TRUE(
      manager_.IsAvailableStream(GetNthPeerInitiatedUnidirectionalStreamId(2)));
}

TEST_P(UberQuicStreamIdManagerTest, MaybeIncreaseLargestPeerStreamId) {
  EXPECT_CALL(delegate_, OnError(_, _)).Times(0);
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(StreamCountToId(
      manager_.max_incoming_bidirectional_streams(),
      QuicUtils::InvertPerspective(perspective()), /* bidirectional=*/true)));
  EXPECT_TRUE(manager_.MaybeIncreaseLargestPeerStreamId(StreamCountToId(
      manager_.max_incoming_bidirectional_streams(),
      QuicUtils::InvertPerspective(perspective()), /* bidirectional=*/false)));

  std::string error_details =
      perspective() == Perspective::IS_SERVER
          ? "Stream id 400 would exceed stream count limit 100"
          : "Stream id 401 would exceed stream count limit 100";

  EXPECT_CALL(delegate_, OnError(QUIC_INVALID_STREAM_ID, error_details));
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(StreamCountToId(
      manager_.max_incoming_bidirectional_streams() + 1,
      QuicUtils::InvertPerspective(perspective()), /* bidirectional=*/true)));
  error_details = perspective() == Perspective::IS_SERVER
                      ? "Stream id 402 would exceed stream count limit 100"
                      : "Stream id 403 would exceed stream count limit 100";
  EXPECT_CALL(delegate_, OnError(QUIC_INVALID_STREAM_ID, error_details));
  EXPECT_FALSE(manager_.MaybeIncreaseLargestPeerStreamId(StreamCountToId(
      manager_.max_incoming_bidirectional_streams() + 1,
      QuicUtils::InvertPerspective(perspective()), /* bidirectional=*/false)));
}

TEST_P(UberQuicStreamIdManagerTest, OnMaxStreamsFrame) {
  QuicStreamCount max_outgoing_bidirectional_stream_count =
      manager_.max_outgoing_bidirectional_streams();

  QuicStreamCount max_outgoing_unidirectional_stream_count =
      manager_.max_outgoing_unidirectional_streams();

  // Inject a MAX_STREAMS frame that does not increase the limit and then
  // check that there are no changes. First try the bidirectional manager.
  QuicMaxStreamsFrame frame(kInvalidControlFrameId,
                            max_outgoing_bidirectional_stream_count,
                            /*unidirectional=*/false);
  EXPECT_TRUE(manager_.OnMaxStreamsFrame(frame));
  EXPECT_EQ(max_outgoing_bidirectional_stream_count,
            manager_.max_outgoing_bidirectional_streams());

  // Now try the unidirectioanl manager
  frame.stream_count = max_outgoing_unidirectional_stream_count;
  frame.unidirectional = true;
  EXPECT_TRUE(manager_.OnMaxStreamsFrame(frame));
  EXPECT_EQ(max_outgoing_unidirectional_stream_count,
            manager_.max_outgoing_unidirectional_streams());

  // Now try to increase the bidirectional stream count.
  frame.stream_count = max_outgoing_bidirectional_stream_count + 1;
  frame.unidirectional = false;
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(frame.unidirectional));
  EXPECT_TRUE(manager_.OnMaxStreamsFrame(frame));
  EXPECT_EQ(max_outgoing_bidirectional_stream_count + 1,
            manager_.max_outgoing_bidirectional_streams());
  // Make sure that the unidirectional state does not change.
  EXPECT_EQ(max_outgoing_unidirectional_stream_count,
            manager_.max_outgoing_unidirectional_streams());

  // Now check that a MAX_STREAMS for the unidirectional manager increases
  // just the unidirectiomal manager's state.
  frame.stream_count = max_outgoing_unidirectional_stream_count + 1;
  frame.unidirectional = true;
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(frame.unidirectional));
  EXPECT_TRUE(manager_.OnMaxStreamsFrame(frame));
  EXPECT_EQ(max_outgoing_bidirectional_stream_count + 1,
            manager_.max_outgoing_bidirectional_streams());
  EXPECT_EQ(max_outgoing_unidirectional_stream_count + 1,
            manager_.max_outgoing_unidirectional_streams());
}

TEST_P(UberQuicStreamIdManagerTest, OnStreamsBlockedFrame) {
  // Allow MAX_STREAMS frame transmission
  manager_.OnConfigNegotiated();

  QuicStreamCount stream_count =
      manager_.advertised_max_incoming_bidirectional_streams() - 1;

  QuicStreamsBlockedFrame frame(kInvalidControlFrameId, stream_count,
                                /*unidirectional=*/false);
  EXPECT_CALL(delegate_,
              SendMaxStreams(manager_.max_incoming_bidirectional_streams(),
                             frame.unidirectional));
  manager_.OnStreamsBlockedFrame(frame);

  stream_count = manager_.advertised_max_incoming_unidirectional_streams() - 1;
  frame.stream_count = stream_count;
  frame.unidirectional = true;

  EXPECT_CALL(delegate_,
              SendMaxStreams(manager_.max_incoming_unidirectional_streams(),
                             frame.unidirectional));
  manager_.OnStreamsBlockedFrame(frame);
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
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(false));
  manager_.SetMaxOpenOutgoingBidirectionalStreams(kNumMaxOutgoingStream);
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(true));
  manager_.SetMaxOpenOutgoingUnidirectionalStreams(kNumMaxOutgoingStream + 1);
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

  // Now cons a MAX STREAMS frame for unidirectional streams to raise
  // the limit.
  QuicMaxStreamsFrame frame(1, kNumMaxOutgoingStream + 10,
                            /*unidirectional=*/true);
  EXPECT_CALL(delegate_, OnCanCreateNewOutgoingStream(frame.unidirectional));
  manager_.OnMaxStreamsFrame(frame);
  // We now should be able to get another uni- stream, but not a bi.
  EXPECT_TRUE(manager_.CanOpenNextOutgoingUnidirectionalStream());
  EXPECT_FALSE(manager_.CanOpenNextOutgoingBidirectionalStream());
}

}  // namespace
}  // namespace test
}  // namespace quic
