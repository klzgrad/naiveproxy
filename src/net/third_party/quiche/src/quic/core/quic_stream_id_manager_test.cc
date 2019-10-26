// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quiche/src/quic/core/quic_stream_id_manager.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class TestQuicStream : public QuicStream {
  // TestQuicStream exists simply as a place to hang OnDataAvailable().
 public:
  TestQuicStream(QuicStreamId id, QuicSession* session, StreamType type)
      : QuicStream(id, session, /*is_static=*/false, type) {}

  void OnDataAvailable() override {}
};

class TestQuicSession : public MockQuicSession {
 public:
  TestQuicSession(QuicConnection* connection)
      : MockQuicSession(connection, /*create_mock_crypto_stream=*/true) {
    Initialize();
  }

  TestQuicStream* CreateIncomingStream(QuicStreamId id) override {
    TestQuicStream* stream = new TestQuicStream(
        id, this,
        DetermineStreamType(id, connection()->transport_version(),
                            perspective(),
                            /*is_incoming=*/true, BIDIRECTIONAL));
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  bool SaveFrame(const QuicFrame& frame) {
    save_frame_ = frame;
    DeleteFrame(&const_cast<QuicFrame&>(frame));
    return true;
  }

  const QuicFrame& save_frame() { return save_frame_; }

  TestQuicStream* CreateOutgoingBidirectionalStream() {
    if (!CanOpenNextOutgoingBidirectionalStream()) {
      return nullptr;
    }
    QuicStreamId id = GetNextOutgoingBidirectionalStreamId();
    TestQuicStream* stream = new TestQuicStream(id, this, BIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  TestQuicStream* CreateOutgoingUnidirectionalStream() {
    if (!CanOpenNextOutgoingUnidirectionalStream()) {
      return nullptr;
    }
    QuicStreamId id = GetNextOutgoingUnidirectionalStreamId();
    TestQuicStream* stream = new TestQuicStream(id, this, WRITE_UNIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

 private:
  QuicFrame save_frame_;
};

class QuicStreamIdManagerTestBase : public QuicTestWithParam<bool> {
 protected:
  explicit QuicStreamIdManagerTestBase(Perspective perspective)
      : connection_(new StrictMock<MockQuicConnection>(
            &helper_,
            &alarm_factory_,
            perspective,
            ParsedQuicVersionVector(
                {{PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_99}}))) {
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
    session_ = QuicMakeUnique<TestQuicSession>(connection_);
    stream_id_manager_ =
        IsBidi() ? QuicSessionPeer::v99_bidirectional_stream_id_manager(
                       session_.get())
                 : QuicSessionPeer::v99_unidirectional_stream_id_manager(
                       session_.get());
  }

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  void CloseStream(QuicStreamId id) { session_->CloseStream(id); }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_CLIENT) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthClientInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_CLIENT) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_SERVER) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId GetNthServerInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_SERVER) +
           kV99StreamIdIncrement * n;
  }

  QuicStreamId StreamCountToId(QuicStreamCount stream_count,
                               Perspective perspective) {
    // Calculate and build up stream ID rather than use
    // GetFirst... because the tests that rely on this method
    // needs to do the stream count where #1 is 0/1/2/3, and not
    // take into account that stream 0 is special.
    QuicStreamId id =
        ((stream_count - 1) * QuicUtils::StreamIdDelta(transport_version()));
    if (IsUnidi()) {
      id |= 0x2;
    }
    if (perspective == Perspective::IS_SERVER) {
      id |= 0x1;
    }
    return id;
  }

  // GetParam returns true if the test is for bidirectional streams
  bool IsUnidi() { return GetParam() ? false : true; }
  bool IsBidi() { return GetParam(); }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  std::unique_ptr<TestQuicSession> session_;
  QuicStreamIdManager* stream_id_manager_;
};

// Following tests are either client-specific (they depend, in some way, on
// client-specific attributes, such as the initial stream ID) or are
// server/client independent (arbitrarily all such tests have been placed here).

class QuicStreamIdManagerTestClient : public QuicStreamIdManagerTestBase {
 protected:
  QuicStreamIdManagerTestClient()
      : QuicStreamIdManagerTestBase(Perspective::IS_CLIENT) {}
};

INSTANTIATE_TEST_SUITE_P(Tests, QuicStreamIdManagerTestClient, testing::Bool());

// Check that the parameters used by the stream ID manager are properly
// initialized.
TEST_P(QuicStreamIdManagerTestClient, StreamIdManagerClientInitialization) {
  // These fields are inited via the QuicSession constructor to default
  // values defined as a constant.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->outgoing_max_streams());

  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->incoming_advertised_max_streams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->incoming_initial_max_open_streams());

  // The window for advertising updates to the MAX STREAM ID is half the number
  // of streams allowed.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection / kMaxStreamsWindowDivisor,
            stream_id_manager_->max_streams_window());
}

// This test checks that the stream advertisement window is set to 1
// if the number of stream ids is 1. This is a special case in the code.
TEST_P(QuicStreamIdManagerTestClient, CheckMaxStreamsWindow1) {
  stream_id_manager_->SetMaxOpenIncomingStreams(1);
  EXPECT_EQ(1u, stream_id_manager_->incoming_initial_max_open_streams());
  EXPECT_EQ(1u, stream_id_manager_->incoming_actual_max_streams());
  // If streamid_count/2==0 (integer math) force it to 1.
  EXPECT_EQ(1u, stream_id_manager_->max_streams_window());
}

// Now check that setting to a value larger than the maximum fails.
TEST_P(QuicStreamIdManagerTestClient,
       CheckMaxStreamsBadValuesOverMaxFailsOutgoing) {
  QuicStreamCount implementation_max =
      QuicUtils::GetMaxStreamCount(!GetParam(), /* GetParam==true for bidi */
                                   Perspective::IS_CLIENT);
  // Ensure that the limit is less than the implementation maximum.
  EXPECT_LT(stream_id_manager_->outgoing_max_streams(), implementation_max);

  // Try to go over.
  stream_id_manager_->SetMaxOpenOutgoingStreams(implementation_max + 1);
  // Should be pegged at the max.
  EXPECT_EQ(implementation_max, stream_id_manager_->outgoing_max_streams());
}

// Now do the same for the incoming streams
TEST_P(QuicStreamIdManagerTestClient, CheckMaxStreamsBadValuesIncoming) {
  QuicStreamCount implementation_max =
      QuicUtils::GetMaxStreamCount(!GetParam(), /* GetParam==true for bidi */
                                   Perspective::IS_CLIENT);
  stream_id_manager_->SetMaxOpenIncomingStreams(implementation_max - 1u);
  EXPECT_EQ(implementation_max - 1u,
            stream_id_manager_->incoming_initial_max_open_streams());
  EXPECT_EQ(implementation_max - 1u,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ((implementation_max - 1u) / 2u,
            stream_id_manager_->max_streams_window());

  stream_id_manager_->SetMaxOpenIncomingStreams(implementation_max);
  EXPECT_EQ(implementation_max,
            stream_id_manager_->incoming_initial_max_open_streams());
  EXPECT_EQ(implementation_max,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(implementation_max / 2, stream_id_manager_->max_streams_window());

  // Reset to 1 so that we can detect the change.
  stream_id_manager_->SetMaxOpenIncomingStreams(1u);
  EXPECT_EQ(1u, stream_id_manager_->incoming_initial_max_open_streams());
  EXPECT_EQ(1u, stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(1u, stream_id_manager_->max_streams_window());
  // Now try to exceed the max, without wrapping.
  stream_id_manager_->SetMaxOpenIncomingStreams(implementation_max + 1);
  EXPECT_EQ(implementation_max,
            stream_id_manager_->incoming_initial_max_open_streams());
  EXPECT_EQ(implementation_max,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(implementation_max / 2u, stream_id_manager_->max_streams_window());
}

// Check the case of the stream count in a STREAMS_BLOCKED frame is less than
// the count most recently advertised in a MAX_STREAMS frame. This should cause
// a MAX_STREAMS frame with the most recently advertised count to be sent.
TEST_P(QuicStreamIdManagerTestClient, ProcessStreamsBlockedOk) {
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  QuicStreamCount stream_count =
      stream_id_manager_->incoming_initial_max_open_streams() - 1;
  QuicStreamsBlockedFrame frame(0, stream_count, /*unidirectional=*/false);
  session_->OnStreamsBlockedFrame(frame);

  // We should see a MAX_STREAMS frame.
  EXPECT_EQ(MAX_STREAMS_FRAME, session_->save_frame().type);

  // and it should advertise the current max-allowed value.
  EXPECT_EQ(stream_id_manager_->incoming_initial_max_open_streams(),
            session_->save_frame().max_streams_frame.stream_count);
}

// Check the case of the stream count in a STREAMS_BLOCKED frame is equal to the
// count most recently advertised in a MAX_STREAMS frame. No MAX_STREAMS
// should be generated.
TEST_P(QuicStreamIdManagerTestClient, ProcessStreamsBlockedNoOp) {
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  QuicStreamCount stream_count =
      stream_id_manager_->incoming_initial_max_open_streams();
  QuicStreamsBlockedFrame frame(0, stream_count, /*unidirectional=*/false);
  session_->OnStreamsBlockedFrame(frame);
}

// Check the case of the stream count in a STREAMS_BLOCKED frame is greater than
// the count most recently advertised in a MAX_STREAMS frame. Expect a
// connection close with an error.
TEST_P(QuicStreamIdManagerTestClient, ProcessStreamsBlockedTooBig) {
  EXPECT_CALL(*connection_, CloseConnection(QUIC_STREAMS_BLOCKED_ERROR, _, _));
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  QuicStreamCount stream_count =
      stream_id_manager_->incoming_initial_max_open_streams() + 1;
  QuicStreamsBlockedFrame frame(0, stream_count, /*unidirectional=*/false);
  session_->OnStreamsBlockedFrame(frame);
}

// Same basic tests as above, but calls
// QuicStreamIdManager::MaybeIncreaseLargestPeerStreamId directly, avoiding the
// call chain. The intent is that if there is a problem, the following tests
// will point to either the stream ID manager or the call chain. They also
// provide specific, small scale, tests of a public QuicStreamIdManager method.
// First test make sure that streams with ids below the limit are accepted.
TEST_P(QuicStreamIdManagerTestClient, IsIncomingStreamIdValidBelowLimit) {
  QuicStreamId stream_id =
      StreamCountToId(stream_id_manager_->incoming_actual_max_streams() - 1,
                      Perspective::IS_CLIENT);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_TRUE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(stream_id));
}

// Accept a stream with an ID that equals the limit.
TEST_P(QuicStreamIdManagerTestClient, IsIncomingStreamIdValidAtLimit) {
  QuicStreamId stream_id =
      StreamCountToId(stream_id_manager_->incoming_actual_max_streams(),
                      Perspective::IS_CLIENT);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_TRUE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(stream_id));
}

// Close the connection if the id exceeds the limit.
TEST_P(QuicStreamIdManagerTestClient, IsIncomingStreamIdInValidAboveLimit) {
  QuicStreamId stream_id = StreamCountToId(
      stream_id_manager_->incoming_actual_max_streams() + 1,
      Perspective::IS_SERVER);  // This node is a client, incoming
                                // stream ids must be server-originated.
  std::string error_details =
      GetParam() ? "Stream id 401 would exceed stream count limit 100"
                 : "Stream id 403 would exceed stream count limit 100";
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID, error_details, _));
  EXPECT_FALSE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(stream_id));
}

// Test functionality for reception of a MAX_STREAMS frame. This code is
// client/server-agnostic.
TEST_P(QuicStreamIdManagerTestClient, StreamIdManagerClientOnMaxStreamsFrame) {
  // Get the current maximum allowed outgoing stream count.
  QuicStreamCount initial_stream_count =
      // need to know the number of request/response streams.
      // This is the total number of outgoing streams (which includes both
      // req/resp and statics).
      stream_id_manager_->outgoing_max_streams();

  QuicMaxStreamsFrame frame;

  // Even though the stream count in the frame is < the initial maximum,
  // it shouldn't be ignored since the initial max was set via
  // the constructor (an educated guess) but the MAX STREAMS frame
  // is authoritative.
  frame.stream_count = initial_stream_count - 1;

  frame.unidirectional = IsUnidi();
  EXPECT_TRUE(stream_id_manager_->OnMaxStreamsFrame(frame));
  EXPECT_EQ(initial_stream_count - 1u,
            stream_id_manager_->outgoing_max_streams());

  QuicStreamCount save_outgoing_max_streams =
      stream_id_manager_->outgoing_max_streams();
  // Now that there has been one MAX STREAMS frame, we should not
  // accept a MAX_STREAMS that reduces the limit...
  frame.stream_count = initial_stream_count - 2;
  frame.unidirectional = IsUnidi();
  EXPECT_TRUE(stream_id_manager_->OnMaxStreamsFrame(frame));
  // should not change from previous setting.
  EXPECT_EQ(save_outgoing_max_streams,
            stream_id_manager_->outgoing_max_streams());

  // A stream count greater than the current limit should increase the limit.
  frame.stream_count = initial_stream_count + 1;
  EXPECT_TRUE(stream_id_manager_->OnMaxStreamsFrame(frame));

  EXPECT_EQ(initial_stream_count + 1u,
            stream_id_manager_->outgoing_max_streams());
}

// Test functionality for reception of a STREAMS_BLOCKED frame.
// This code is client/server-agnostic.
TEST_P(QuicStreamIdManagerTestClient, StreamIdManagerOnStreamsBlockedFrame) {
  // Get the current maximum allowed incoming stream count.
  QuicStreamCount advertised_stream_count =
      stream_id_manager_->incoming_advertised_max_streams();
  QuicStreamsBlockedFrame frame;

  frame.unidirectional = IsUnidi();

  // If the peer is saying it's blocked on the stream count that
  // we've advertised, it's a noop since the peer has the correct information.
  frame.stream_count = advertised_stream_count;
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  EXPECT_TRUE(stream_id_manager_->OnStreamsBlockedFrame(frame));

  // If the peer is saying it's blocked on a stream count that is larger
  // than what we've advertised, the connection should get closed.
  frame.stream_count = advertised_stream_count + 1;
  EXPECT_CALL(*connection_, CloseConnection(QUIC_STREAMS_BLOCKED_ERROR, _, _));
  EXPECT_FALSE(stream_id_manager_->OnStreamsBlockedFrame(frame));

  // If the peer is saying it's blocked on a count that is less than
  // our actual count, we send a MAX_STREAMS frame and update
  // the advertised value.
  // First, need to bump up the actual max so there is room for the MAX
  // STREAMS frame to send a larger ID.
  QuicStreamCount actual_stream_count =
      stream_id_manager_->incoming_actual_max_streams();

  // Closing a stream will result in the ability to initiate one more
  // stream
  stream_id_manager_->OnStreamClosed(
      QuicStreamIdManagerPeer::GetFirstIncomingStreamId(stream_id_manager_));
  EXPECT_EQ(actual_stream_count + 1u,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(stream_id_manager_->incoming_actual_max_streams(),
            stream_id_manager_->incoming_advertised_max_streams() + 1u);

  // Now simulate receiving a STREAMS_BLOCKED frame...
  // Changing the actual maximum, above, forces a MAX_STREAMS frame to be
  // sent, so the logic for that (SendMaxStreamsFrame(), etc) is tested.

  // The STREAMS_BLOCKED frame contains the previous advertised count,
  // not the one that the peer would have received as a result of the
  // MAX_STREAMS sent earler.
  frame.stream_count = advertised_stream_count;

  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));

  EXPECT_TRUE(stream_id_manager_->OnStreamsBlockedFrame(frame));
  // Check that the saved frame is correct.
  EXPECT_EQ(stream_id_manager_->incoming_actual_max_streams(),
            stream_id_manager_->incoming_advertised_max_streams());
  EXPECT_EQ(MAX_STREAMS_FRAME, session_->save_frame().type);
  EXPECT_EQ(stream_id_manager_->incoming_advertised_max_streams(),
            session_->save_frame().max_streams_frame.stream_count);
  // Make sure that this is the only MAX_STREAMS
  EXPECT_EQ(1u, GetControlFrameId(session_->save_frame()));
}

// Test GetNextOutgoingStream. This is client/server agnostic.
TEST_P(QuicStreamIdManagerTestClient, StreamIdManagerGetNextOutgoingStream) {
  // Number of streams we can open and the first one we should get when
  // opening...
  size_t number_of_streams = kDefaultMaxStreamsPerConnection;
  QuicStreamId stream_id =
      IsUnidi() ? session_->next_outgoing_unidirectional_stream_id()
                : session_->next_outgoing_bidirectional_stream_id();

  EXPECT_EQ(number_of_streams, stream_id_manager_->outgoing_max_streams());
  while (number_of_streams) {
    EXPECT_TRUE(stream_id_manager_->CanOpenNextOutgoingStream());
    EXPECT_EQ(stream_id, stream_id_manager_->GetNextOutgoingStreamId());
    stream_id += kV99StreamIdIncrement;
    number_of_streams--;
  }

  // If we try to check that the next outgoing stream id is available it should
  // A) fail and B) generate a STREAMS_BLOCKED frame.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  EXPECT_FALSE(stream_id_manager_->CanOpenNextOutgoingStream());
  EXPECT_EQ(STREAMS_BLOCKED_FRAME, session_->save_frame().type);
  // If bidi, Crypto stream default created  at start up, it is one
  // more stream to account for since initialization is "number of
  // request/responses" & crypto is added in to that, not streams.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            session_->save_frame().max_streams_frame.stream_count);
  // If we try to get the next id (above the limit), it should cause a quic-bug.
  EXPECT_QUIC_BUG(
      stream_id_manager_->GetNextOutgoingStreamId(),
      "Attempt to allocate a new outgoing stream that would exceed the limit");
}

// Ensure that MaybeIncreaseLargestPeerStreamId works properly. This is
// server/client agnostic.
TEST_P(QuicStreamIdManagerTestClient,
       StreamIdManagerServerMaybeIncreaseLargestPeerStreamId) {
  QuicStreamId max_stream_id =
      StreamCountToId(stream_id_manager_->incoming_actual_max_streams(),
                      Perspective::IS_SERVER);
  EXPECT_TRUE(
      stream_id_manager_->MaybeIncreaseLargestPeerStreamId(max_stream_id));

  QuicStreamId server_initiated_stream_id =
      StreamCountToId(1u,  // get 1st id
                      Perspective::IS_SERVER);
  EXPECT_TRUE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(
      server_initiated_stream_id));
  // A bad stream ID results in a closed connection.
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  EXPECT_FALSE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(
      max_stream_id + kV99StreamIdIncrement));
}

// Test the MAX STREAMS Window functionality.
TEST_P(QuicStreamIdManagerTestClient, StreamIdManagerServerMaxStreams) {
  // Test that a MAX_STREAMS frame is generated when the peer has less than
  // |max_streams_window_| streams left that it can initiate.

  // First, open, and then close, max_streams_window_ streams.  This will
  // max_streams_window_ streams available for the peer -- no MAX_STREAMS
  // should be sent. The -1 is because the check in
  // QuicStreamIdManager::MaybeSendMaxStreamsFrame sends a MAX_STREAMS if the
  // number of available streams at the peer is <= |max_streams_window_|
  int stream_count = stream_id_manager_->max_streams_window() - 1;

  // Should not get a control-frame transmission since the peer should have
  // "plenty" of stream IDs to use.
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);

  // Get the first incoming stream ID to try and allocate.
  QuicStreamId stream_id = IsBidi() ? GetNthServerInitiatedBidirectionalId(0)
                                    : GetNthServerInitiatedUnidirectionalId(0);
  size_t old_available_incoming_streams =
      stream_id_manager_->available_incoming_streams();
  while (stream_count) {
    EXPECT_TRUE(
        stream_id_manager_->MaybeIncreaseLargestPeerStreamId(stream_id));

    // This node should think that the peer believes it has one fewer
    // stream it can create.
    old_available_incoming_streams--;
    EXPECT_EQ(old_available_incoming_streams,
              stream_id_manager_->available_incoming_streams());

    stream_count--;
    stream_id += kV99StreamIdIncrement;
  }

  // Now close them, still should get no MAX_STREAMS
  stream_count = stream_id_manager_->max_streams_window();
  stream_id = IsBidi() ? GetNthServerInitiatedBidirectionalId(0)
                       : GetNthServerInitiatedUnidirectionalId(0);
  QuicStreamCount expected_actual_max =
      stream_id_manager_->incoming_actual_max_streams();
  QuicStreamCount expected_advertised_max_streams =
      stream_id_manager_->incoming_advertised_max_streams();
  while (stream_count) {
    stream_id_manager_->OnStreamClosed(stream_id);
    stream_count--;
    stream_id += kV99StreamIdIncrement;
    expected_actual_max++;
    EXPECT_EQ(expected_actual_max,
              stream_id_manager_->incoming_actual_max_streams());
    // Advertised maximum should remain the same.
    EXPECT_EQ(expected_advertised_max_streams,
              stream_id_manager_->incoming_advertised_max_streams());
  }

  // This should not change.
  EXPECT_EQ(old_available_incoming_streams,
            stream_id_manager_->available_incoming_streams());

  // Now whenever we close a stream we should get a MAX_STREAMS frame.
  // Above code closed all the open streams, so we have to open/close
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  EXPECT_TRUE(stream_id_manager_->MaybeIncreaseLargestPeerStreamId(stream_id));
  stream_id_manager_->OnStreamClosed(stream_id);
  stream_id += kV99StreamIdIncrement;

  // Check that the MAX STREAMS was sent and has the correct values.
  EXPECT_EQ(MAX_STREAMS_FRAME, session_->save_frame().type);
  EXPECT_EQ(stream_id_manager_->incoming_advertised_max_streams(),
            session_->save_frame().max_streams_frame.stream_count);
}

// Check that edge conditions of the stream count in a STREAMS_BLOCKED frame
// are. properly handled.
TEST_P(QuicStreamIdManagerTestClient, StreamsBlockedEdgeConditions) {
  QuicStreamsBlockedFrame frame;
  frame.unidirectional = IsUnidi();

  // Check that receipt of a STREAMS BLOCKED with stream-count = 0 does nothing
  // when max_allowed_incoming_streams is 0.
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  stream_id_manager_->SetMaxOpenIncomingStreams(0);
  frame.stream_count = 0;
  stream_id_manager_->OnStreamsBlockedFrame(frame);

  // Check that receipt of a STREAMS BLOCKED with stream-count = 0 invokes a
  // MAX STREAMS, count = 123, when the MaxOpen... is set to 123.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillOnce(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  stream_id_manager_->SetMaxOpenIncomingStreams(123);
  frame.stream_count = 0;
  stream_id_manager_->OnStreamsBlockedFrame(frame);
  EXPECT_EQ(MAX_STREAMS_FRAME, session_->save_frame().type);
  EXPECT_EQ(123u, session_->save_frame().max_streams_frame.stream_count);
}

// Following tests all are server-specific. They depend, in some way, on
// server-specific attributes, such as the initial stream ID.

class QuicStreamIdManagerTestServer : public QuicStreamIdManagerTestBase {
 protected:
  QuicStreamIdManagerTestServer()
      : QuicStreamIdManagerTestBase(Perspective::IS_SERVER) {}
};

INSTANTIATE_TEST_SUITE_P(Tests, QuicStreamIdManagerTestServer, testing::Bool());

// This test checks that the initialization for the maximum allowed outgoing
// stream id is correct.
TEST_P(QuicStreamIdManagerTestServer, CheckMaxAllowedOutgoing) {
  const size_t kIncomingStreamCount = 123;
  stream_id_manager_->SetMaxOpenOutgoingStreams(kIncomingStreamCount);
  EXPECT_EQ(kIncomingStreamCount, stream_id_manager_->outgoing_max_streams());
}

// Test that a MAX_STREAMS frame is generated when half the stream ids become
// available. This has a useful side effect of testing that when streams are
// closed, the number of available stream ids increases.
TEST_P(QuicStreamIdManagerTestServer, MaxStreamsSlidingWindow) {
  // Ignore OnStreamReset calls.
  EXPECT_CALL(*connection_, OnStreamReset(_, _)).WillRepeatedly(Return());
  // Capture control frames for analysis.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  // Simulate config being negotiated, causing the limits all to be initialized.
  session_->OnConfigNegotiated();
  QuicStreamCount first_advert =
      stream_id_manager_->incoming_advertised_max_streams();

  // Open/close enough streams to shrink the window without causing a MAX
  // STREAMS to be generated. The window will open (and a MAX STREAMS generated)
  // when max_streams_window() stream IDs have been made available. The loop
  // will make that many stream IDs available, so the last CloseStream should
  // cause a MAX STREAMS frame to be generated.
  int i = static_cast<int>(stream_id_manager_->max_streams_window());
  QuicStreamId id =
      QuicStreamIdManagerPeer::GetFirstIncomingStreamId(stream_id_manager_);
  while (i) {
    QuicStream* stream = session_->GetOrCreateStream(id);
    EXPECT_NE(nullptr, stream);
    // have to set the stream's fin-received flag to true so that it
    // does not go into the has-not-received-byte-offset state, leading
    // to the stream being added to the locally_closed_streams_highest_offset_
    // map, and therefore not counting as truly being closed. The test requires
    // that the stream truly close, so that new streams become available,
    // causing the MAX_STREAMS to be sent.
    stream->set_fin_received(true);
    EXPECT_EQ(id, stream->id());
    if (IsBidi()) {
      // Only send reset for incoming bidirectional streams.
      EXPECT_CALL(*session_, SendRstStream(_, _, _));
    }
    CloseStream(stream->id());
    i--;
    id += kV99StreamIdIncrement;
  }
  EXPECT_EQ(MAX_STREAMS_FRAME, session_->save_frame().type);
  QuicStreamCount second_advert =
      session_->save_frame().max_streams_frame.stream_count;
  EXPECT_EQ(first_advert + stream_id_manager_->max_streams_window(),
            second_advert);
}

// Tast that an attempt to create an outgoing stream does not exceed the limit
// and that it generates an appropriate STREAMS_BLOCKED frame.
TEST_P(QuicStreamIdManagerTestServer, NewStreamDoesNotExceedLimit) {
  size_t stream_count = stream_id_manager_->outgoing_max_streams();
  EXPECT_NE(0u, stream_count);
  TestQuicStream* stream;
  while (stream_count) {
    stream = IsBidi() ? session_->CreateOutgoingBidirectionalStream()
                      : session_->CreateOutgoingUnidirectionalStream();
    EXPECT_NE(stream, nullptr);
    stream_count--;
  }

  EXPECT_EQ(stream_id_manager_->outgoing_stream_count(),
            stream_id_manager_->outgoing_max_streams());
  // Create another, it should fail. Should also send a STREAMS_BLOCKED
  // control frame.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  stream = IsBidi() ? session_->CreateOutgoingBidirectionalStream()
                    : session_->CreateOutgoingUnidirectionalStream();
  EXPECT_EQ(nullptr, stream);
}

// Check that the parameters used by the stream ID manager are properly
// initialized
TEST_P(QuicStreamIdManagerTestServer, StreamIdManagerServerInitialization) {
  // These fields are inited via the QuicSession constructor to default
  // values defined as a constant.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->incoming_initial_max_open_streams());

  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->incoming_actual_max_streams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->outgoing_max_streams());

  // The window for advertising updates to the MAX STREAM ID is half the number
  // of stream allowed.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection / kMaxStreamsWindowDivisor,
            stream_id_manager_->max_streams_window());
}

TEST_P(QuicStreamIdManagerTestServer, AvailableStreams) {
  stream_id_manager_->MaybeIncreaseLargestPeerStreamId(
      IsBidi() ? GetNthClientInitiatedBidirectionalId(3)
               : GetNthClientInitiatedUnidirectionalId(3));
  EXPECT_TRUE(stream_id_manager_->IsAvailableStream(
      IsBidi() ? GetNthClientInitiatedBidirectionalId(1)
               : GetNthClientInitiatedUnidirectionalId(1)));
  EXPECT_TRUE(stream_id_manager_->IsAvailableStream(
      IsBidi() ? GetNthClientInitiatedBidirectionalId(2)
               : GetNthClientInitiatedUnidirectionalId(2)));
}

// Tests that if MaybeIncreaseLargestPeerStreamId is given an extremely
// large stream ID (larger than the limit) it is rejected.
// This is a regression for Chromium bugs 909987 and 910040
TEST_P(QuicStreamIdManagerTestServer, ExtremeMaybeIncreaseLargestPeerStreamId) {
  QuicStreamId too_big_stream_id = StreamCountToId(
      stream_id_manager_->incoming_actual_max_streams() + 20,
      Perspective::IS_CLIENT);  // This node is a server, incoming stream
                                // ids must be client-originated.
  std::string error_details;
  if (IsBidi()) {
    if (QuicVersionUsesCryptoFrames(transport_version())) {
      error_details = "Stream id 476 would exceed stream count limit 100";
    } else {
      error_details = "Stream id 480 would exceed stream count limit 101";
    }
  } else {
    error_details = "Stream id 478 would exceed stream count limit 100";
  }

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID, error_details, _));
  EXPECT_FALSE(
      stream_id_manager_->MaybeIncreaseLargestPeerStreamId(too_big_stream_id));
}

}  // namespace
}  // namespace test
}  // namespace quic
