// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quic/core/quic_stream_id_manager.h"

#include <cstdint>
#include <set>
#include <utility>

#include "net/third_party/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/quic_data_writer.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_stream.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_map_util.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

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
    TestQuicStream* stream = new TestQuicStream(id, this, BIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  bool SaveFrame(const QuicFrame& frame) {
    save_frame_ = frame;
    DeleteFrame(&const_cast<QuicFrame&>(frame));
    return true;
  }

  const QuicFrame& save_frame() { return save_frame_; }

  bool ClearControlFrame(const QuicFrame& frame) {
    DeleteFrame(&const_cast<QuicFrame&>(frame));
    return true;
  }

  TestQuicStream* CreateOutgoingBidirectionalStream() {
    if (CanOpenNextOutgoingStream() == false) {
      return nullptr;
    }
    QuicStreamId id = GetNextOutgoingStreamId();
    TestQuicStream* stream = new TestQuicStream(id, this, BIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

 private:
  QuicFrame save_frame_;
};

class QuicStreamIdManagerTestBase
    : public QuicTestWithParam<ParsedQuicVersion> {
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
    stream_id_manager_ = QuicSessionPeer::v99_streamid_manager(session_.get());
  }

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  void CloseStream(QuicStreamId id) { session_->CloseStream(id); }

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

// Check that the parameters used by the stream ID manager are properly
// initialized.
TEST_F(QuicStreamIdManagerTestClient, StreamIdManagerClientInitialization) {
  // These fields are inited via the QuicSession constructor to default
  // values defined as a constant.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->max_allowed_incoming_streams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->max_allowed_outgoing_streams());

  // The window for advertising updates to the MAX STREAM ID is half the number
  // of streams allowed.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection / kMaxStreamIdWindowDivisor,
            stream_id_manager_->max_stream_id_window());

  // This test runs as a client, so it initiates (that is to say, outgoing)
  // even-numbered stream IDs. Also, our implementation starts allocating
  // stream IDs at 0 (for clients) 1 (for servers) -- before taking statically
  // allocated streams into account. The -1 in the calculation is
  // because the value being tested is the maximum allowed stream ID, not the
  // first unallowed stream id.
  const QuicStreamId kExpectedMaxOutgoingStreamId =
      session_->next_outgoing_stream_id() +
      ((kDefaultMaxStreamsPerConnection - 1) * kV99StreamIdIncrement);
  EXPECT_EQ(kExpectedMaxOutgoingStreamId,
            stream_id_manager_->max_allowed_outgoing_stream_id());

  // Same for IDs of incoming streams...
  const QuicStreamId kExpectedMaxIncomingStreamId =
      stream_id_manager_->first_incoming_dynamic_stream_id() +
      (kDefaultMaxStreamsPerConnection - 1) * kV99StreamIdIncrement;
  EXPECT_EQ(kExpectedMaxIncomingStreamId,
            stream_id_manager_->actual_max_allowed_incoming_stream_id());
  EXPECT_EQ(kExpectedMaxIncomingStreamId,
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());
}

// This test checks that the initialization for the maximum allowed outgoing
// stream id is correct.
TEST_F(QuicStreamIdManagerTestClient, CheckMaxAllowedOutgoing) {
  const size_t kNumOutgoingStreams = 124;
  stream_id_manager_->SetMaxOpenOutgoingStreams(kNumOutgoingStreams);
  EXPECT_EQ(kNumOutgoingStreams,
            stream_id_manager_->max_allowed_outgoing_streams());

  // Check that the maximum available stream is properly set.
  size_t expected_max_outgoing_id =
      session_->next_outgoing_stream_id() +
      ((kNumOutgoingStreams - 1) * kV99StreamIdIncrement);
  EXPECT_EQ(expected_max_outgoing_id,
            stream_id_manager_->max_allowed_outgoing_stream_id());
}

// This test checks that the initialization for the maximum allowed incoming
// stream id is correct.
TEST_F(QuicStreamIdManagerTestClient, CheckMaxAllowedIncoming) {
  const size_t kStreamCount = 245;
  stream_id_manager_->SetMaxOpenIncomingStreams(kStreamCount);
  EXPECT_EQ(kStreamCount, stream_id_manager_->max_allowed_incoming_streams());
  // Check that the window is 1/2 (integer math) of the stream count.
  EXPECT_EQ(kStreamCount / 2, stream_id_manager_->max_stream_id_window());

  // Actual- and advertised- maxima start out equal.
  EXPECT_EQ(stream_id_manager_->actual_max_allowed_incoming_stream_id(),
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());

  // Check that the maximum stream ID is properly calculated.
  EXPECT_EQ(stream_id_manager_->first_incoming_dynamic_stream_id() +
                ((kStreamCount - 1) * kV99StreamIdIncrement),
            stream_id_manager_->actual_max_allowed_incoming_stream_id());
}

// This test checks that the stream advertisement window is set to 1
// if the number of stream ids is 1. This is a special case in the code.
TEST_F(QuicStreamIdManagerTestClient, CheckMaxStreamIdWindow1) {
  stream_id_manager_->SetMaxOpenIncomingStreams(1);
  EXPECT_EQ(1u, stream_id_manager_->max_allowed_incoming_streams());
  // If streamid_count/2==0 (integer math) force it to 1.
  EXPECT_EQ(1u, stream_id_manager_->max_stream_id_window());
}

// Check the case of the stream ID in a STREAM_ID_BLOCKED frame is less than the
// stream ID most recently advertised in a MAX_STREAM_ID frame. This should
// cause a MAX_STREAM_ID frame with the most recently advertised stream id to be
// sent.
TEST_F(QuicStreamIdManagerTestClient, ProcessStreamIdBlockedOk) {
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  QuicStreamId stream_id =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id() -
      kV99StreamIdIncrement;
  QuicStreamIdBlockedFrame frame(0, stream_id);
  session_->OnStreamIdBlockedFrame(frame);

  // We should see a MAX_STREAM_ID frame.
  EXPECT_EQ(MAX_STREAM_ID_FRAME, session_->save_frame().type);

  // and it should advertise the current max-allowed value.
  EXPECT_EQ(stream_id_manager_->actual_max_allowed_incoming_stream_id(),
            session_->save_frame().max_stream_id_frame.max_stream_id);
}

// Check the case of the stream ID in a STREAM_ID_BLOCKED frame is equal to
// stream ID most recently advertised in a MAX_STREAM_ID frame. No
// MAX_STREAM_ID should be generated.
TEST_F(QuicStreamIdManagerTestClient, ProcessStreamIdBlockedNoOp) {
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  QuicStreamId stream_id =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id();
  QuicStreamIdBlockedFrame frame(0, stream_id);
  session_->OnStreamIdBlockedFrame(frame);
}

// Check the case of the stream ID in a STREAM_ID_BLOCKED frame is greater than
// the stream ID most recently advertised in a MAX_STREAM_ID frame. Expect a
// connection close with an error.
TEST_F(QuicStreamIdManagerTestClient, ProcessStreamIdBlockedTooBig) {
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_ID_BLOCKED_ERROR, _, _));
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  QuicStreamId stream_id =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id() +
      kV99StreamIdIncrement;
  QuicStreamIdBlockedFrame frame(0, stream_id);
  session_->OnStreamIdBlockedFrame(frame);
}

// Same basic tests as above, but calls
// QuicStreamIdManager::OnIncomingStreamOpened directly, avoiding the call
// chain. The intent is that if there is a problem, the following tests will
// point to either the stream ID manager or the call chain. They also provide
// specific, small scale, tests of a public QuicStreamIdManager method.
// First test make sure that streams with ids below the limit are accepted.
TEST_F(QuicStreamIdManagerTestClient, IsIncomingStreamIdValidBelowLimit) {
  QuicStreamId stream_id =
      stream_id_manager_->actual_max_allowed_incoming_stream_id() -
      kV99StreamIdIncrement;
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(stream_id));
}

// Accept a stream with an ID that equals the limit.
TEST_F(QuicStreamIdManagerTestClient, IsIncomingStreamIdValidAtLimit) {
  QuicStreamId stream_id =
      stream_id_manager_->actual_max_allowed_incoming_stream_id();
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(stream_id));
}

// Close the connection if the id exceeds the limit.
TEST_F(QuicStreamIdManagerTestClient, IsIncomingStreamIdInValidAboveLimit) {
  QuicStreamId stream_id =
      stream_id_manager_->actual_max_allowed_incoming_stream_id() +
      kV99StreamIdIncrement;
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID, "201 above 199", _));
  EXPECT_FALSE(stream_id_manager_->OnIncomingStreamOpened(stream_id));
}

// Test that a client will reject a MAX_STREAM_ID that specifies a
// server-initiated stream ID.
TEST_F(QuicStreamIdManagerTestClient, RejectServerMaxStreamId) {
  QuicStreamId id = stream_id_manager_->max_allowed_outgoing_stream_id();

  // Ensure that the ID that will be in the MAX_STREAM_ID is larger than the
  // current MAX.
  id += (kV99StreamIdIncrement * 2);

  // Make it an odd (server-initiated) ID.
  id |= 0x1;
  EXPECT_FALSE(QuicUtils::IsClientInitiatedStreamId(QUIC_VERSION_99, id));

  // Make the frame and process it; should result in the connection being
  // closed.
  QuicMaxStreamIdFrame frame(0, id);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_MAX_STREAM_ID_ERROR, _, _));
  session_->OnMaxStreamIdFrame(frame);
}

// Test that a client will reject a STREAM_ID_BLOCKED that specifies a
// client-initiated stream ID. STREAM_ID_BLOCKED from a server should specify an
// odd (server-initiated_ ID). Generate one with an odd ID and check that the
// connection is closed.
TEST_F(QuicStreamIdManagerTestClient, RejectServerStreamIdBlocked) {
  QuicStreamId id = stream_id_manager_->max_allowed_outgoing_stream_id();

  // Ensure that the ID that will be in the MAX_STREAM_ID is larger than the
  // current MAX.
  id += (kV99StreamIdIncrement * 2);
  // Make sure it's odd, like a client-initiated ID.
  id &= ~0x01;
  EXPECT_TRUE(QuicUtils::IsClientInitiatedStreamId(QUIC_VERSION_99, id));

  // Generate and process the frame; connection should be closed.
  QuicStreamIdBlockedFrame frame(0, id);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_ID_BLOCKED_ERROR, _, _));
  session_->OnStreamIdBlockedFrame(frame);
}

// Test functionality for reception of a MAX STREAM ID frame. This code is
// client/server-agnostic.
TEST_F(QuicStreamIdManagerTestClient, StreamIdManagerClientOnMaxStreamIdFrame) {
  // Get the current maximum allowed outgoing stream ID.
  QuicStreamId initial_stream_id =
      stream_id_manager_->max_allowed_outgoing_stream_id();
  QuicMaxStreamIdFrame frame;

  // If the stream ID in the frame is < the current maximum then
  // the frame should be ignored.
  frame.max_stream_id = initial_stream_id - kV99StreamIdIncrement;
  EXPECT_TRUE(stream_id_manager_->OnMaxStreamIdFrame(frame));
  EXPECT_EQ(initial_stream_id,
            stream_id_manager_->max_allowed_outgoing_stream_id());

  // A stream ID greater than the current limit should increase the limit.
  frame.max_stream_id = initial_stream_id + kV99StreamIdIncrement;
  EXPECT_TRUE(stream_id_manager_->OnMaxStreamIdFrame(frame));
  EXPECT_EQ(initial_stream_id + kV99StreamIdIncrement,
            stream_id_manager_->max_allowed_outgoing_stream_id());
}

// Test functionality for reception of a STREAM ID BLOCKED frame.
// This code is client/server-agnostic.
TEST_F(QuicStreamIdManagerTestClient, StreamIdManagerOnStreamIdBlockedFrame) {
  // Get the current maximum allowed incoming stream ID.
  QuicStreamId advertised_stream_id =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id();
  QuicStreamIdBlockedFrame frame;

  // If the peer is saying it's blocked on the stream ID that
  // we've advertised, it's a noop since the peer has the correct information.
  frame.stream_id = advertised_stream_id;
  EXPECT_TRUE(stream_id_manager_->OnStreamIdBlockedFrame(frame));

  // If the peer is saying it's blocked on a stream ID that is larger
  // than what we've advertised, the connection should get closed.
  frame.stream_id = advertised_stream_id + kV99StreamIdIncrement;
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_ID_BLOCKED_ERROR, _, _));
  EXPECT_FALSE(stream_id_manager_->OnStreamIdBlockedFrame(frame));

  // If the peer is saying it's blocked on a stream ID that is less than
  // what we've advertised, we send a MAX STREAM ID frame and update
  // the advertised value.
  // First, need to bump up the actual max so there is room for the MAX
  // STREAM_ID frame to send a larger ID.
  QuicStreamId actual_stream_id =
      stream_id_manager_->actual_max_allowed_incoming_stream_id();
  stream_id_manager_->OnStreamClosed(
      stream_id_manager_->first_incoming_dynamic_stream_id());
  EXPECT_EQ(actual_stream_id + kV99StreamIdIncrement,
            stream_id_manager_->actual_max_allowed_incoming_stream_id());
  EXPECT_GT(stream_id_manager_->actual_max_allowed_incoming_stream_id(),
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());

  // Now simulate receiving a STTREAM_ID_BLOCKED frame...
  // Changing the actual maximum, above, forces a MAX STREAM ID frame to be
  // sent, so the logic for that (SendMaxStreamIdFrame(), etc) is tested.
  frame.stream_id = advertised_stream_id;
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  EXPECT_TRUE(stream_id_manager_->OnStreamIdBlockedFrame(frame));
  EXPECT_EQ(stream_id_manager_->actual_max_allowed_incoming_stream_id(),
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());
  EXPECT_EQ(MAX_STREAM_ID_FRAME, session_->save_frame().type);
  EXPECT_EQ(stream_id_manager_->advertised_max_allowed_incoming_stream_id(),
            session_->save_frame().max_stream_id_frame.max_stream_id);

  // Server intiates streams with odd stream IDs, so a STREAM_ID_BLOCKED frame
  // should contain an odd stream ID.  Ensure that an even one is
  // rejected. closing the connection.
  frame.stream_id = 4;
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_ID_BLOCKED_ERROR, _, _));
  EXPECT_FALSE(stream_id_manager_->OnStreamIdBlockedFrame(frame));
}

// Test GetNextOutgoingStream. This is client/server agnostic.
TEST_F(QuicStreamIdManagerTestClient, StreamIdManagerGetNextOutgoingFrame) {
  // Number of streams we can open and the first one we should get when
  // opening...
  int number_of_streams = kDefaultMaxStreamsPerConnection;
  QuicStreamId stream_id = session_->next_outgoing_stream_id();

  while (number_of_streams) {
    EXPECT_TRUE(stream_id_manager_->CanOpenNextOutgoingStream());
    EXPECT_EQ(stream_id, stream_id_manager_->GetNextOutgoingStreamId());
    stream_id += kV99StreamIdIncrement;
    number_of_streams--;
  }
  EXPECT_EQ(stream_id - kV99StreamIdIncrement,
            stream_id_manager_->max_allowed_outgoing_stream_id());

  // If we try to check that the next outgoing stream id is available it should
  // A) fail and B) generate a STREAM_ID_BLOCKED frame.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  EXPECT_FALSE(stream_id_manager_->CanOpenNextOutgoingStream());
  EXPECT_EQ(STREAM_ID_BLOCKED_FRAME, session_->save_frame().type);
  EXPECT_EQ(stream_id_manager_->max_allowed_outgoing_stream_id(),
            session_->save_frame().max_stream_id_frame.max_stream_id);
  // If we try to get the next id (above the limit), it should cause a quic-bug.
  EXPECT_QUIC_BUG(
      stream_id_manager_->GetNextOutgoingStreamId(),
      "Attempt allocate a new outgoing stream ID would exceed the limit");
}

// Ensure that OnIncomingStreamOpened works properly. This is server/client
// agnostic.
TEST_F(QuicStreamIdManagerTestClient,
       StreamIdManagerServerOnIncomingStreamOpened) {
  EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(
      stream_id_manager_->actual_max_allowed_incoming_stream_id()));
  EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(2));
  // A bad stream ID results in a closed connection.
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  EXPECT_FALSE(stream_id_manager_->OnIncomingStreamOpened(
      stream_id_manager_->actual_max_allowed_incoming_stream_id() +
      kV99StreamIdIncrement));
}

// Test the MAX STREAM ID Window functionality.
// Free up Stream ID space. Do not expect to see a MAX_STREAM_ID
// until |window| stream ids are available.
TEST_F(QuicStreamIdManagerTestClient, StreamIdManagerServerMaxStreamId) {
  // Test that a MAX_STREAM_ID frame is generated when the peer has less than
  // |max_stream_id_window_| streams left that it can initiate.

  // First, open, and then close, max_stream_id_window_ streams.  This will
  // max_stream_id_window_ streams available for the peer -- no MAX_STREAM_ID
  // should be sent. The -1 is because the check in
  // QuicStreamIdManager::MaybeSendMaxStreamIdFrame sends a MAX_STREAM_ID if the
  // number of available streams at the peer is <= |max_stream_id_window_|
  int stream_count = stream_id_manager_->max_stream_id_window() - 1;

  QuicStreamId advertised_max =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id();
  QuicStreamId expected_actual_max_id =
      stream_id_manager_->actual_max_allowed_incoming_stream_id();

  // Should not get a control-frame transmission since the peer should have
  // "plenty" of stream IDs to use.
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  // This test runs as a client, so the first stream to release is 2, a
  // server-initiated stream.
  QuicStreamId stream_id = 1;
  size_t old_available_incoming_streams =
      stream_id_manager_->available_incoming_streams();

  while (stream_count) {
    EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(stream_id));

    old_available_incoming_streams--;
    EXPECT_EQ(old_available_incoming_streams,
              stream_id_manager_->available_incoming_streams());

    stream_count--;
    stream_id += kV99StreamIdIncrement;
  }

  // Now close them, still should get no MAX_STREAM_ID
  stream_count = stream_id_manager_->max_stream_id_window();
  stream_id = 1;
  while (stream_count) {
    stream_id_manager_->OnStreamClosed(stream_id);
    stream_count--;
    stream_id += kV99StreamIdIncrement;
    expected_actual_max_id += kV99StreamIdIncrement;
    EXPECT_EQ(expected_actual_max_id,
              stream_id_manager_->actual_max_allowed_incoming_stream_id());
    // Advertised maximum should remain the same.
    EXPECT_EQ(advertised_max,
              stream_id_manager_->advertised_max_allowed_incoming_stream_id());
  }

  // This should not change.
  EXPECT_EQ(old_available_incoming_streams,
            stream_id_manager_->available_incoming_streams());

  // Now whenever we close a stream we should get a MAX_STREAM_ID frame.
  // Above code closed all the open streams, so we have to open/close
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  EXPECT_TRUE(stream_id_manager_->OnIncomingStreamOpened(stream_id));
  stream_id_manager_->OnStreamClosed(stream_id);
  stream_id += kV99StreamIdIncrement;

  // Check that the MAX STREAM ID was sent and has the correct values.
  EXPECT_EQ(MAX_STREAM_ID_FRAME, session_->save_frame().type);
  EXPECT_EQ(stream_id_manager_->advertised_max_allowed_incoming_stream_id(),
            session_->save_frame().max_stream_id_frame.max_stream_id);
}

// Test that registering static stream IDs causes the stream ID limit to rise
// accordingly. This is server/client agnostic.
TEST_F(QuicStreamIdManagerTestClient, TestStaticStreamAdjustment) {
  QuicStreamId first_dynamic =
      stream_id_manager_->first_incoming_dynamic_stream_id();
  QuicStreamId expected_max_incoming =
      stream_id_manager_->actual_max_allowed_incoming_stream_id();

  // First test will register the first dynamic stream id as being for a static
  // stream.  This takes one stream ID out of the low-end of the dynamic range
  // so therefore the high end should go up by 1 ID.
  expected_max_incoming += kV99StreamIdIncrement;
  stream_id_manager_->RegisterStaticStream(first_dynamic);
  EXPECT_EQ(expected_max_incoming,
            stream_id_manager_->actual_max_allowed_incoming_stream_id());

  // Now be extreme, increase static by 100 stream ids.  A discontinuous
  // jump is not allowed; make sure.
  first_dynamic += kV99StreamIdIncrement * 100;
  expected_max_incoming += kV99StreamIdIncrement * 100;
  EXPECT_QUIC_BUG(stream_id_manager_->RegisterStaticStream(first_dynamic),
                  "Error in incoming static stream allocation, expected to "
                  "allocate 3 got 201");
}

// Following tests all are server-specific. They depend, in some way, on
// server-specific attributes, such as the initial stream ID.

class QuicStreamIdManagerTestServer : public QuicStreamIdManagerTestBase {
 protected:
  QuicStreamIdManagerTestServer()
      : QuicStreamIdManagerTestBase(Perspective::IS_SERVER) {}
};

// This test checks that the initialization for the maximum allowed outgoing
// stream id is correct.
TEST_F(QuicStreamIdManagerTestServer, CheckMaxAllowedOutgoing) {
  const size_t kIncomingStreamCount = 123;
  stream_id_manager_->SetMaxOpenOutgoingStreams(kIncomingStreamCount);
  EXPECT_EQ(kIncomingStreamCount,
            stream_id_manager_->max_allowed_outgoing_streams());

  // Check that the max outgoing stream id is properly calculated
  EXPECT_EQ(stream_id_manager_->GetNextOutgoingStreamId() +
                ((kIncomingStreamCount - 1) * kV99StreamIdIncrement),
            stream_id_manager_->max_allowed_outgoing_stream_id());
}

// This test checks that the initialization for the maximum allowed incoming
// stream id is correct.
TEST_F(QuicStreamIdManagerTestServer, CheckMaxAllowedIncoming) {
  const size_t kIncomingStreamCount = 245;
  stream_id_manager_->SetMaxOpenIncomingStreams(kIncomingStreamCount);
  EXPECT_EQ(kIncomingStreamCount,
            stream_id_manager_->max_allowed_incoming_streams());

  // Check that the window is 1/2 (integer math) of the stream count.
  EXPECT_EQ((kIncomingStreamCount / 2),
            stream_id_manager_->max_stream_id_window());

  // Actual- and advertised- maxima start out equal.
  EXPECT_EQ(stream_id_manager_->actual_max_allowed_incoming_stream_id(),
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());

  // First stream ID the client should use should be 3, this means that the max
  // stream id is 491 -- ((number of stream ids-1) * 2) + first available id.
  EXPECT_EQ(stream_id_manager_->first_incoming_dynamic_stream_id() +
                ((kIncomingStreamCount - 1) * kV99StreamIdIncrement),
            stream_id_manager_->actual_max_allowed_incoming_stream_id());
}

// Test that a MAX_STREAM_ID frame is generated when half the stream ids become
// available. This has a useful side effect of testing that when streams are
// closed, the number of available stream ids increases.
TEST_F(QuicStreamIdManagerTestServer, MaxStreamIdSlidingWindow) {
  // Ignore OnStreamReset calls.
  EXPECT_CALL(*connection_, OnStreamReset(_, _)).WillRepeatedly(Return());
  // Capture control frames for analysis.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(session_.get(), &TestQuicSession::SaveFrame));
  // Simulate config being negotiated, causing the limits all to be initialized.
  session_->OnConfigNegotiated();
  QuicStreamId first_advert =
      stream_id_manager_->advertised_max_allowed_incoming_stream_id();

  // Open/close enough streams to shrink the window without causing a MAX STREAM
  // ID to be generated. The window will open (and a MAX STREAM ID generated)
  // when max_stream_id_window() stream IDs have been made available. The loop
  // will make that many stream IDs available, so the last CloseStream should
  // cause a MAX STREAM ID frame to be generated.
  int i = static_cast<int>(stream_id_manager_->max_stream_id_window());
  QuicStreamId id = stream_id_manager_->first_incoming_dynamic_stream_id();
  while (i) {
    QuicStream* stream = session_->GetOrCreateStream(id);
    EXPECT_NE(nullptr, stream);
    // have to set the stream's fin-received flag to true so that it
    // does not go into the has-not-received-byte-offset state, leading
    // to the stream being added to the locally_closed_streams_highest_offset_
    // map, and therefore not counting as truly being closed. The test requires
    // that the stream truly close, so that new streams become available,
    // causing the MAX_STREAM_ID to be sent.
    stream->set_fin_received(true);
    EXPECT_EQ(id, stream->id());
    EXPECT_CALL(*session_, SendRstStream(_, _, _));
    CloseStream(stream->id());
    i--;
    id += kV99StreamIdIncrement;
  }
  EXPECT_EQ(MAX_STREAM_ID_FRAME, session_->save_frame().type);
  QuicStreamId second_advert =
      session_->save_frame().max_stream_id_frame.max_stream_id;
  EXPECT_EQ(first_advert + (stream_id_manager_->max_stream_id_window() *
                            kV99StreamIdIncrement),
            second_advert);
}

// Tast that an attempt to create an outgoing stream does not exceed the limit
// and that it generates an appropriate STREAM_ID_BLOCKED frame.
TEST_F(QuicStreamIdManagerTestServer, NewStreamDoesNotExceedLimit) {
  size_t stream_count = stream_id_manager_->max_allowed_outgoing_streams();
  EXPECT_NE(0u, stream_count);
  TestQuicStream* stream;
  while (stream_count) {
    stream = session_->CreateOutgoingBidirectionalStream();
    EXPECT_NE(stream, nullptr);
    stream_count--;
  }
  // Quis Custodiet Ipsos Custodes.
  EXPECT_EQ(stream->id(), stream_id_manager_->max_allowed_outgoing_stream_id());
  // Create another, it should fail. Should also send a STREAM_ID_BLOCKED
  // control frame.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  stream = session_->CreateOutgoingBidirectionalStream();
  EXPECT_EQ(nullptr, stream);
}

// Test that a server will reject a MAX_STREAM_ID that specifies a
// client-initiated stream ID.
TEST_F(QuicStreamIdManagerTestServer, RejectClientMaxStreamId) {
  QuicStreamId id = stream_id_manager_->max_allowed_outgoing_stream_id();

  // Ensure that the ID that will be in the MAX_STREAM_ID is larger than the
  // current MAX.
  id += (kV99StreamIdIncrement * 2);

  // Turn it into a client-initiated ID (even).
  id &= ~0x1;
  EXPECT_TRUE(QuicUtils::IsClientInitiatedStreamId(QUIC_VERSION_99, id));

  // Generate a MAX_STREAM_ID frame and process it; the connection should close.
  QuicMaxStreamIdFrame frame(0, id);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_MAX_STREAM_ID_ERROR, _, _));
  session_->OnMaxStreamIdFrame(frame);
}

// Test that a server will reject a STREAM_ID_BLOCKED that specifies a
// server-initiated stream ID. STREAM_ID_BLOCKED from a client should specify an
// even (client-initiated_ ID) generate one with an odd ID and check that the
// connection is closed.
TEST_F(QuicStreamIdManagerTestServer, RejectClientStreamIdBlocked) {
  QuicStreamId id = stream_id_manager_->max_allowed_outgoing_stream_id();

  // Ensure that the ID that will be in the MAX_STREAM_ID is larger than the
  // current MAX.
  id += (kV99StreamIdIncrement * 2);

  // Make the ID odd, so it looks like the client is trying to specify a
  // server-initiated ID.
  id |= 0x1;
  EXPECT_FALSE(QuicUtils::IsClientInitiatedStreamId(QUIC_VERSION_99, id));

  // Generate a STREAM_ID_BLOCKED frame and process it; the connection should
  // close.
  QuicStreamIdBlockedFrame frame(0, id);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_ID_BLOCKED_ERROR, _, _));
  session_->OnStreamIdBlockedFrame(frame);
}

// Check that the parameters used by the stream ID manager are properly
// initialized
TEST_F(QuicStreamIdManagerTestServer, StreamIdManagerServerInitialization) {
  // These fields are inited via the QuicSession constructor to default
  // values defined as a constant.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->max_allowed_incoming_streams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            stream_id_manager_->max_allowed_outgoing_streams());

  // The window for advertising updates to the MAX STREAM ID is half the number
  // of stream allowed.
  EXPECT_EQ(kDefaultMaxStreamsPerConnection / kMaxStreamIdWindowDivisor,
            stream_id_manager_->max_stream_id_window());

  // This test runs as a server, so it initiates (that is to say, outgoing)
  // even-numbered stream IDs. The -1 in the calculation is because the value
  // being tested is the maximum allowed stream ID, not the first unallowed
  // stream id.
  const QuicStreamId kExpectedMaxOutgoingStreamId =
      session_->next_outgoing_stream_id() +
      ((kDefaultMaxStreamsPerConnection - 1) * kV99StreamIdIncrement);
  EXPECT_EQ(kExpectedMaxOutgoingStreamId,
            stream_id_manager_->max_allowed_outgoing_stream_id());

  // Same for IDs of incoming streams... But they are client initiated, so are
  // even.
  const QuicStreamId kExpectedMaxIncomingStreamId =
      ((kDefaultMaxStreamsPerConnection)*kV99StreamIdIncrement);
  EXPECT_EQ(kExpectedMaxIncomingStreamId,
            stream_id_manager_->actual_max_allowed_incoming_stream_id());
  EXPECT_EQ(kExpectedMaxIncomingStreamId,
            stream_id_manager_->advertised_max_allowed_incoming_stream_id());
}

}  // namespace
}  // namespace test
}  // namespace quic
