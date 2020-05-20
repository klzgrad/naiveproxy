// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_session.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_max_streams_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_session_visitor.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using spdy::kV3HighestPriority;
using spdy::SpdyPriority;
using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::WithArg;

namespace quic {
namespace test {
namespace {

class TestCryptoStream : public QuicCryptoStream, public QuicCryptoHandshaker {
 public:
  explicit TestCryptoStream(QuicSession* session)
      : QuicCryptoStream(session),
        QuicCryptoHandshaker(this, session),
        encryption_established_(false),
        one_rtt_keys_available_(false),
        params_(new QuicCryptoNegotiatedParameters) {
    // Simulate a negotiated cipher_suite with a fake value.
    params_->cipher_suite = 1;
  }

  void OnHandshakeMessage(const CryptoHandshakeMessage& /*message*/) override {
    encryption_established_ = true;
    one_rtt_keys_available_ = true;
    QuicErrorCode error;
    std::string error_details;
    session()->config()->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    session()->config()->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);
    if (session()->connection()->version().handshake_protocol ==
        PROTOCOL_TLS1_3) {
      TransportParameters transport_parameters;
      EXPECT_TRUE(
          session()->config()->FillTransportParameters(&transport_parameters));
      error = session()->config()->ProcessTransportParameters(
          transport_parameters, CLIENT, &error_details);
    } else {
      CryptoHandshakeMessage msg;
      session()->config()->ToHandshakeMessage(&msg, transport_version());
      error =
          session()->config()->ProcessPeerHello(msg, CLIENT, &error_details);
    }
    EXPECT_THAT(error, IsQuicNoError());
    session()->OnConfigNegotiated();
    if (session()->connection()->version().handshake_protocol ==
        PROTOCOL_TLS1_3) {
      session()->OnOneRttKeysAvailable();
    } else {
      session()->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    }
    session()->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
  }

  // QuicCryptoStream implementation
  bool encryption_established() const override {
    return encryption_established_;
  }
  bool one_rtt_keys_available() const override {
    return one_rtt_keys_available_;
  }
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override {
    return *params_;
  }
  CryptoMessageParser* crypto_message_parser() override {
    return QuicCryptoHandshaker::crypto_message_parser();
  }
  void OnPacketDecrypted(EncryptionLevel /*level*/) override {}
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakeDoneReceived() override {}
  HandshakeState GetHandshakeState() const override {
    return one_rtt_keys_available() ? HANDSHAKE_COMPLETE : HANDSHAKE_START;
  }

  MOCK_METHOD0(OnCanWrite, void());
  bool HasPendingCryptoRetransmission() const override { return false; }

  MOCK_CONST_METHOD0(HasPendingRetransmission, bool());

 private:
  using QuicCryptoStream::session;

  bool encryption_established_;
  bool one_rtt_keys_available_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
};

class TestStream : public QuicStream {
 public:
  TestStream(QuicStreamId id, QuicSession* session, StreamType type)
      : TestStream(id, session, /*is_static=*/false, type) {}

  TestStream(QuicStreamId id,
             QuicSession* session,
             bool is_static,
             StreamType type)
      : QuicStream(id, session, is_static, type) {}

  TestStream(PendingStream* pending, StreamType type)
      : QuicStream(pending, type, /*is_static=*/false) {}

  using QuicStream::CloseReadSide;
  using QuicStream::CloseWriteSide;
  using QuicStream::WriteMemSlices;

  void OnDataAvailable() override {}

  MOCK_METHOD0(OnCanWrite, void());
  MOCK_METHOD4(RetransmitStreamData,
               bool(QuicStreamOffset, QuicByteCount, bool, TransmissionType));

  MOCK_CONST_METHOD0(HasPendingRetransmission, bool());
};

class TestSession : public QuicSession {
 public:
  explicit TestSession(QuicConnection* connection,
                       MockQuicSessionVisitor* session_visitor)
      : QuicSession(connection,
                    session_visitor,
                    DefaultQuicConfig(),
                    CurrentSupportedVersions(),
                    /*num_expected_unidirectional_static_streams = */ 0),
        crypto_stream_(this),
        writev_consumes_all_data_(false),
        uses_pending_streams_(false),
        num_incoming_streams_created_(0) {
    Initialize();
    this->connection()->SetEncrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullEncrypter>(connection->perspective()));
  }

  ~TestSession() override { DeleteConnection(); }

  TestCryptoStream* GetMutableCryptoStream() override {
    return &crypto_stream_;
  }

  const TestCryptoStream* GetCryptoStream() const override {
    return &crypto_stream_;
  }

  TestStream* CreateOutgoingBidirectionalStream() {
    QuicStreamId id = GetNextOutgoingBidirectionalStreamId();
    if (id ==
        QuicUtils::GetInvalidStreamId(connection()->transport_version())) {
      return nullptr;
    }
    TestStream* stream = new TestStream(id, this, BIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  TestStream* CreateOutgoingUnidirectionalStream() {
    TestStream* stream = new TestStream(GetNextOutgoingUnidirectionalStreamId(),
                                        this, WRITE_UNIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  TestStream* CreateIncomingStream(QuicStreamId id) override {
    // Enforce the limit on the number of open streams.
    if (GetNumOpenIncomingStreams() + 1 >
            max_open_incoming_bidirectional_streams() &&
        !VersionHasIetfQuicFrames(connection()->transport_version())) {
      // No need to do this test for version 99; it's done by
      // QuicSession::GetOrCreateStream.
      connection()->CloseConnection(
          QUIC_TOO_MANY_OPEN_STREAMS, "Too many streams!",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return nullptr;
    }

    TestStream* stream =
        new TestStream(id, this,
                       DetermineStreamType(
                           id, connection()->transport_version(), perspective(),
                           /*is_incoming=*/true, BIDIRECTIONAL));
    ActivateStream(QuicWrapUnique(stream));
    ++num_incoming_streams_created_;
    return stream;
  }

  TestStream* CreateIncomingStream(PendingStream* pending) override {
    QuicStreamId id = pending->id();
    TestStream* stream = new TestStream(
        pending, DetermineStreamType(id, connection()->transport_version(),
                                     perspective(),
                                     /*is_incoming=*/true, BIDIRECTIONAL));
    ActivateStream(QuicWrapUnique(stream));
    ++num_incoming_streams_created_;
    return stream;
  }

  // QuicSession doesn't do anything in this method. So it's overridden here to
  // test that the session handles pending streams correctly in terms of
  // receiving stream frames.
  bool ProcessPendingStream(PendingStream* pending) override {
    struct iovec iov;
    if (pending->sequencer()->GetReadableRegion(&iov)) {
      // Create TestStream once the first byte is received.
      CreateIncomingStream(pending);
      return true;
    }
    return false;
  }

  bool IsClosedStream(QuicStreamId id) {
    return QuicSession::IsClosedStream(id);
  }

  QuicStream* GetOrCreateStream(QuicStreamId stream_id) {
    return QuicSession::GetOrCreateStream(stream_id);
  }

  bool ShouldKeepConnectionAlive() const override {
    return GetNumActiveStreams() > 0;
  }

  QuicConsumedData WritevData(
      QuicStreamId id,
      size_t write_length,
      QuicStreamOffset offset,
      StreamSendingState state,
      TransmissionType type,
      quiche::QuicheOptional<EncryptionLevel> level) override {
    bool fin = state != NO_FIN;
    QuicConsumedData consumed(write_length, fin);
    if (!writev_consumes_all_data_) {
      consumed =
          QuicSession::WritevData(id, write_length, offset, state, type, level);
    }
    QuicSessionPeer::GetWriteBlockedStreams(this)->UpdateBytesForStream(
        id, consumed.bytes_consumed);
    return consumed;
  }

  MOCK_METHOD1(OnCanCreateNewOutgoingStream, void(bool unidirectional));

  void set_writev_consumes_all_data(bool val) {
    writev_consumes_all_data_ = val;
  }

  QuicConsumedData SendStreamData(QuicStream* stream) {
    struct iovec iov;
    if (!QuicUtils::IsCryptoStreamId(connection()->transport_version(),
                                     stream->id()) &&
        this->connection()->encryption_level() != ENCRYPTION_FORWARD_SECURE) {
      this->connection()->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    }
    MakeIOVector("not empty", &iov);
    QuicStreamPeer::SendBuffer(stream).SaveStreamData(&iov, 1, 0, 9);
    QuicConsumedData consumed =
        WritevData(stream->id(), 9, 0, FIN, NOT_RETRANSMISSION, QuicheNullOpt);
    QuicStreamPeer::SendBuffer(stream).OnStreamDataConsumed(
        consumed.bytes_consumed);
    return consumed;
  }

  const QuicFrame& save_frame() { return save_frame_; }

  bool SaveFrame(const QuicFrame& frame) {
    save_frame_ = frame;
    DeleteFrame(&const_cast<QuicFrame&>(frame));
    return true;
  }

  QuicConsumedData SendLargeFakeData(QuicStream* stream, int bytes) {
    DCHECK(writev_consumes_all_data_);
    return WritevData(stream->id(), bytes, 0, FIN, NOT_RETRANSMISSION,
                      QuicheNullOpt);
  }

  bool UsesPendingStreams() const override { return uses_pending_streams_; }

  void set_uses_pending_streams(bool uses_pending_streams) {
    uses_pending_streams_ = uses_pending_streams;
  }

  int num_incoming_streams_created() const {
    return num_incoming_streams_created_;
  }

  using QuicSession::ActivateStream;
  using QuicSession::CanOpenNextOutgoingBidirectionalStream;
  using QuicSession::CanOpenNextOutgoingUnidirectionalStream;
  using QuicSession::closed_streams;
  using QuicSession::GetNextOutgoingBidirectionalStreamId;
  using QuicSession::GetNextOutgoingUnidirectionalStreamId;
  using QuicSession::zombie_streams;

 private:
  StrictMock<TestCryptoStream> crypto_stream_;

  bool writev_consumes_all_data_;
  bool uses_pending_streams_;
  QuicFrame save_frame_;
  int num_incoming_streams_created_;
};

class QuicSessionTestBase : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  QuicSessionTestBase(Perspective perspective, bool configure_session)
      : connection_(
            new StrictMock<MockQuicConnection>(&helper_,
                                               &alarm_factory_,
                                               perspective,
                                               SupportedVersions(GetParam()))),
        session_(connection_, &session_visitor_),
        configure_session_(configure_session) {
    session_.config()->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    session_.config()->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);

    if (configure_session) {
      if (VersionHasIetfQuicFrames(transport_version())) {
        EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(1);
        EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(true)).Times(1);
      }
      QuicConfigPeer::SetReceivedMaxBidirectionalStreams(
          session_.config(), kDefaultMaxStreamsPerConnection);
      QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(
          session_.config(), kDefaultMaxStreamsPerConnection);
      QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
          session_.config(), kMinimumFlowControlSendWindow);
      QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
          session_.config(), kMinimumFlowControlSendWindow);
      QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
          session_.config(), kMinimumFlowControlSendWindow);
      QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
          session_.config(), kMinimumFlowControlSendWindow);
      connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
      session_.OnConfigNegotiated();
    }
    TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .Times(testing::AnyNumber());
    testing::Mock::VerifyAndClearExpectations(&session_);
  }

  ~QuicSessionTestBase() {
    if (configure_session_) {
      EXPECT_TRUE(session_.is_configured());
    }
  }

  void CheckClosedStreams() {
    QuicStreamId first_stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        connection_->transport_version(), Perspective::IS_CLIENT);
    if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
      first_stream_id =
          QuicUtils::GetCryptoStreamId(connection_->transport_version());
    }
    for (QuicStreamId i = first_stream_id; i < 100; i++) {
      if (!QuicContainsKey(closed_streams_, i)) {
        EXPECT_FALSE(session_.IsClosedStream(i)) << " stream id: " << i;
      } else {
        EXPECT_TRUE(session_.IsClosedStream(i)) << " stream id: " << i;
      }
    }
  }

  void CloseStream(QuicStreamId id) {
    if (VersionHasIetfQuicFrames(transport_version())) {
      if (QuicUtils::GetStreamType(id, session_.perspective(),
                                   session_.IsIncomingStream(id)) ==
          READ_UNIDIRECTIONAL) {
        // Verify reset is not sent for READ_UNIDIRECTIONAL streams.
        EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
        EXPECT_CALL(*connection_, OnStreamReset(_, _)).Times(0);
      } else if (QuicUtils::GetStreamType(id, session_.perspective(),
                                          session_.IsIncomingStream(id)) ==
                 WRITE_UNIDIRECTIONAL) {
        // Verify RESET_STREAM but not STOP_SENDING is sent for write-only
        // stream.
        EXPECT_CALL(*connection_, SendControlFrame(_))
            .Times(1)
            .WillOnce(Invoke(&ClearControlFrame));
        EXPECT_CALL(*connection_, OnStreamReset(id, _));
      } else {
        // Verify RESET_STREAM and STOP_SENDING are sent for BIDIRECTIONAL
        // streams.
        EXPECT_CALL(*connection_, SendControlFrame(_))
            .Times(2)
            .WillRepeatedly(Invoke(&ClearControlFrame));
        EXPECT_CALL(*connection_, OnStreamReset(id, _));
      }
    } else {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
      EXPECT_CALL(*connection_, OnStreamReset(id, _));
    }
    session_.CloseStream(id);
    closed_streams_.insert(id);
  }

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_CLIENT) +
           QuicUtils::StreamIdDelta(connection_->transport_version()) * n;
  }

  QuicStreamId GetNthClientInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_CLIENT) +
           QuicUtils::StreamIdDelta(connection_->transport_version()) * n;
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return QuicUtils::GetFirstBidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_SERVER) +
           QuicUtils::StreamIdDelta(connection_->transport_version()) * n;
  }

  QuicStreamId GetNthServerInitiatedUnidirectionalId(int n) {
    return QuicUtils::GetFirstUnidirectionalStreamId(
               connection_->transport_version(), Perspective::IS_SERVER) +
           QuicUtils::StreamIdDelta(connection_->transport_version()) * n;
  }

  QuicStreamId StreamCountToId(QuicStreamCount stream_count,
                               Perspective perspective,
                               bool bidirectional) {
    // Calculate and build up stream ID rather than use
    // GetFirst... because tests that rely on this method
    // needs to do the stream count where #1 is 0/1/2/3, and not
    // take into account that stream 0 is special.
    QuicStreamId id =
        ((stream_count - 1) * QuicUtils::StreamIdDelta(transport_version()));
    if (!bidirectional) {
      id |= 0x2;
    }
    if (perspective == Perspective::IS_SERVER) {
      id |= 0x1;
    }
    return id;
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  NiceMock<MockQuicSessionVisitor> session_visitor_;
  StrictMock<MockQuicConnection>* connection_;
  TestSession session_;
  std::set<QuicStreamId> closed_streams_;
  bool configure_session_;
};

class QuicSessionTestServer : public QuicSessionTestBase {
 public:
  // CheckMultiPathResponse validates that a written packet
  // contains both expected path responses.
  WriteResult CheckMultiPathResponse(const char* buffer,
                                     size_t buf_len,
                                     const QuicIpAddress& /*self_address*/,
                                     const QuicSocketAddress& /*peer_address*/,
                                     PerPacketOptions* /*options*/) {
    QuicEncryptedPacket packet(buffer, buf_len);
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_))
          .WillOnce(
              WithArg<0>(Invoke([this](const QuicPathResponseFrame& frame) {
                EXPECT_EQ(path_frame_buffer1_, frame.data_buffer);
                return true;
              })));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_))
          .WillOnce(
              WithArg<0>(Invoke([this](const QuicPathResponseFrame& frame) {
                EXPECT_EQ(path_frame_buffer2_, frame.data_buffer);
                return true;
              })));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    client_framer_.ProcessPacket(packet);
    return WriteResult(WRITE_STATUS_OK, 0);
  }

 protected:
  QuicSessionTestServer()
      : QuicSessionTestBase(Perspective::IS_SERVER, /*configure_session=*/true),
        path_frame_buffer1_({0, 1, 2, 3, 4, 5, 6, 7}),
        path_frame_buffer2_({8, 9, 10, 11, 12, 13, 14, 15}),
        client_framer_(SupportedVersions(GetParam()),
                       QuicTime::Zero(),
                       Perspective::IS_CLIENT,
                       kQuicDefaultConnectionIdLength) {
    client_framer_.set_visitor(&framer_visitor_);
    client_framer_.SetInitialObfuscators(TestConnectionId());
  }

  QuicPathFrameBuffer path_frame_buffer1_;
  QuicPathFrameBuffer path_frame_buffer2_;
  StrictMock<MockFramerVisitor> framer_visitor_;
  // Framer used to process packets sent by server.
  QuicFramer client_framer_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSessionTestServer,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSessionTestServer, PeerAddress) {
  EXPECT_EQ(QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort),
            session_.peer_address());
}

TEST_P(QuicSessionTestServer, SelfAddress) {
  EXPECT_TRUE(session_.self_address().IsInitialized());
}

TEST_P(QuicSessionTestServer, DontCallOnWriteBlockedForDisconnectedConnection) {
  EXPECT_CALL(*connection_, CloseConnection(_, _, _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  connection_->CloseConnection(QUIC_NO_ERROR, "Everything is fine.",
                               ConnectionCloseBehavior::SILENT_CLOSE);
  ASSERT_FALSE(connection_->connected());

  EXPECT_CALL(session_visitor_, OnWriteBlocked(_)).Times(0);
  session_.OnWriteBlocked();
}

TEST_P(QuicSessionTestServer, OneRttKeysAvailable) {
  EXPECT_FALSE(session_.OneRttKeysAvailable());
  CryptoHandshakeMessage message;
  if (connection_->version().HasHandshakeDone()) {
    EXPECT_CALL(*connection_, SendControlFrame(_));
  }
  session_.GetMutableCryptoStream()->OnHandshakeMessage(message);
  EXPECT_TRUE(session_.OneRttKeysAvailable());
}

TEST_P(QuicSessionTestServer, IsClosedStreamDefault) {
  // Ensure that no streams are initially closed.
  QuicStreamId first_stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      connection_->transport_version(), Perspective::IS_CLIENT);
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    first_stream_id =
        QuicUtils::GetCryptoStreamId(connection_->transport_version());
  }
  for (QuicStreamId i = first_stream_id; i < 100; i++) {
    EXPECT_FALSE(session_.IsClosedStream(i)) << "stream id: " << i;
  }
}

TEST_P(QuicSessionTestServer, AvailableBidirectionalStreams) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(3)) != nullptr);
  // Smaller bidirectional streams should be available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(1)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(2)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(2)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(1)) != nullptr);
}

TEST_P(QuicSessionTestServer, AvailableUnidirectionalStreams) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedUnidirectionalId(3)) != nullptr);
  // Smaller unidirectional streams should be available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedUnidirectionalId(1)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedUnidirectionalId(2)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedUnidirectionalId(2)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedUnidirectionalId(1)) != nullptr);
}

TEST_P(QuicSessionTestServer, MaxAvailableBidirectionalStreams) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_EQ(session_.max_open_incoming_bidirectional_streams(),
              session_.MaxAvailableBidirectionalStreams());
  } else {
    // The protocol specification requires that there can be at least 10 times
    // as many available streams as the connection's maximum open streams.
    EXPECT_EQ(session_.max_open_incoming_bidirectional_streams() *
                  kMaxAvailableStreamsMultiplier,
              session_.MaxAvailableBidirectionalStreams());
  }
}

TEST_P(QuicSessionTestServer, MaxAvailableUnidirectionalStreams) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_EQ(session_.max_open_incoming_unidirectional_streams(),
              session_.MaxAvailableUnidirectionalStreams());
  } else {
    // The protocol specification requires that there can be at least 10 times
    // as many available streams as the connection's maximum open streams.
    EXPECT_EQ(session_.max_open_incoming_unidirectional_streams() *
                  kMaxAvailableStreamsMultiplier,
              session_.MaxAvailableUnidirectionalStreams());
  }
}

TEST_P(QuicSessionTestServer, IsClosedBidirectionalStreamLocallyCreated) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedBidirectionalId(0), stream2->id());
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedBidirectionalId(1), stream4->id());

  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedBidirectionalId(0));
  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedBidirectionalId(1));
  CheckClosedStreams();
}

TEST_P(QuicSessionTestServer, IsClosedUnidirectionalStreamLocallyCreated) {
  TestStream* stream2 = session_.CreateOutgoingUnidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedUnidirectionalId(0), stream2->id());
  TestStream* stream4 = session_.CreateOutgoingUnidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedUnidirectionalId(1), stream4->id());

  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedUnidirectionalId(0));
  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedUnidirectionalId(1));
  CheckClosedStreams();
}

TEST_P(QuicSessionTestServer, IsClosedBidirectionalStreamPeerCreated) {
  QuicStreamId stream_id1 = GetNthClientInitiatedBidirectionalId(0);
  QuicStreamId stream_id2 = GetNthClientInitiatedBidirectionalId(1);
  session_.GetOrCreateStream(stream_id1);
  session_.GetOrCreateStream(stream_id2);

  CheckClosedStreams();
  CloseStream(stream_id1);
  CheckClosedStreams();
  CloseStream(stream_id2);
  // Create a stream, and make another available.
  QuicStream* stream3 = session_.GetOrCreateStream(
      stream_id2 +
      2 * QuicUtils::StreamIdDelta(connection_->transport_version()));
  CheckClosedStreams();
  // Close one, but make sure the other is still not closed
  CloseStream(stream3->id());
  CheckClosedStreams();
}

TEST_P(QuicSessionTestServer, IsClosedUnidirectionalStreamPeerCreated) {
  QuicStreamId stream_id1 = GetNthClientInitiatedUnidirectionalId(0);
  QuicStreamId stream_id2 = GetNthClientInitiatedUnidirectionalId(1);
  session_.GetOrCreateStream(stream_id1);
  session_.GetOrCreateStream(stream_id2);

  CheckClosedStreams();
  CloseStream(stream_id1);
  CheckClosedStreams();
  CloseStream(stream_id2);
  // Create a stream, and make another available.
  QuicStream* stream3 = session_.GetOrCreateStream(
      stream_id2 +
      2 * QuicUtils::StreamIdDelta(connection_->transport_version()));
  CheckClosedStreams();
  // Close one, but make sure the other is still not closed
  CloseStream(stream3->id());
  CheckClosedStreams();
}

TEST_P(QuicSessionTestServer, MaximumAvailableOpenedBidirectionalStreams) {
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  session_.GetOrCreateStream(stream_id);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_NE(nullptr,
            session_.GetOrCreateStream(GetNthClientInitiatedBidirectionalId(
                session_.max_open_incoming_bidirectional_streams() - 1)));
}

TEST_P(QuicSessionTestServer, MaximumAvailableOpenedUnidirectionalStreams) {
  QuicStreamId stream_id = GetNthClientInitiatedUnidirectionalId(0);
  session_.GetOrCreateStream(stream_id);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_NE(nullptr,
            session_.GetOrCreateStream(GetNthClientInitiatedUnidirectionalId(
                session_.max_open_incoming_unidirectional_streams() - 1)));
}

TEST_P(QuicSessionTestServer, TooManyAvailableBidirectionalStreams) {
  QuicStreamId stream_id1 = GetNthClientInitiatedBidirectionalId(0);
  QuicStreamId stream_id2;
  EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id1));
  // A stream ID which is too large to create.
  stream_id2 = GetNthClientInitiatedBidirectionalId(
      session_.MaxAvailableBidirectionalStreams() + 2);
  if (VersionHasIetfQuicFrames(transport_version())) {
    // IETF QUIC terminates the connection with invalid stream id
    EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  } else {
    // other versions terminate the connection with
    // QUIC_TOO_MANY_AVAILABLE_STREAMS.
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_TOO_MANY_AVAILABLE_STREAMS, _, _));
  }
  EXPECT_EQ(nullptr, session_.GetOrCreateStream(stream_id2));
}

TEST_P(QuicSessionTestServer, TooManyAvailableUnidirectionalStreams) {
  QuicStreamId stream_id1 = GetNthClientInitiatedUnidirectionalId(0);
  QuicStreamId stream_id2;
  EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id1));
  // A stream ID which is too large to create.
  stream_id2 = GetNthClientInitiatedUnidirectionalId(
      session_.MaxAvailableUnidirectionalStreams() + 2);
  if (VersionHasIetfQuicFrames(transport_version())) {
    // IETF QUIC terminates the connection with invalid stream id
    EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  } else {
    // other versions terminate the connection with
    // QUIC_TOO_MANY_AVAILABLE_STREAMS.
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_TOO_MANY_AVAILABLE_STREAMS, _, _));
  }
  EXPECT_EQ(nullptr, session_.GetOrCreateStream(stream_id2));
}

TEST_P(QuicSessionTestServer, ManyAvailableBidirectionalStreams) {
  // When max_open_streams_ is 200, should be able to create 200 streams
  // out-of-order, that is, creating the one with the largest stream ID first.
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_, 200);
    // Smaller limit on unidirectional streams to help detect crossed wires.
    QuicSessionPeer::SetMaxOpenIncomingUnidirectionalStreams(&session_, 50);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, 200);
  }
  // Create a stream at the start of the range.
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));

  // Create the largest stream ID of a threatened total of 200 streams.
  // GetNth... starts at 0, so for 200 streams, get the 199th.
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_NE(nullptr, session_.GetOrCreateStream(
                         GetNthClientInitiatedBidirectionalId(199)));

  if (VersionHasIetfQuicFrames(transport_version())) {
    // If IETF QUIC, check to make sure that creating bidirectional
    // streams does not mess up the unidirectional streams.
    stream_id = GetNthClientInitiatedUnidirectionalId(0);
    EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));
    // Now try to get the last possible unidirectional stream.
    EXPECT_NE(nullptr, session_.GetOrCreateStream(
                           GetNthClientInitiatedUnidirectionalId(49)));
    // and this should fail because it exceeds the unidirectional limit
    // (but not the bi-)
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INVALID_STREAM_ID,
                        "Stream id 798 would exceed stream count limit 50",
                        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET))
        .Times(1);
    EXPECT_EQ(nullptr, session_.GetOrCreateStream(
                           GetNthClientInitiatedUnidirectionalId(199)));
  }
}

TEST_P(QuicSessionTestServer, ManyAvailableUnidirectionalStreams) {
  // When max_open_streams_ is 200, should be able to create 200 streams
  // out-of-order, that is, creating the one with the largest stream ID first.
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingUnidirectionalStreams(&session_, 200);
    // Smaller limit on unidirectional streams to help detect crossed wires.
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_, 50);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, 200);
  }
  // Create one stream.
  QuicStreamId stream_id = GetNthClientInitiatedUnidirectionalId(0);
  EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));

  // Create the largest stream ID of a threatened total of 200 streams.
  // GetNth... starts at 0, so for 200 streams, get the 199th.
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_NE(nullptr, session_.GetOrCreateStream(
                         GetNthClientInitiatedUnidirectionalId(199)));
  if (VersionHasIetfQuicFrames(transport_version())) {
    // If IETF QUIC, check to make sure that creating unidirectional
    // streams does not mess up the bidirectional streams.
    stream_id = GetNthClientInitiatedBidirectionalId(0);
    EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));
    // Now try to get the last possible bidirectional stream.
    EXPECT_NE(nullptr, session_.GetOrCreateStream(
                           GetNthClientInitiatedBidirectionalId(49)));
    // and this should fail because it exceeds the bnidirectional limit
    // (but not the uni-)
    std::string error_detail;
    if (QuicVersionUsesCryptoFrames(transport_version())) {
      error_detail = "Stream id 796 would exceed stream count limit 50";
    } else {
      error_detail = "Stream id 800 would exceed stream count limit 50";
    }
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INVALID_STREAM_ID, error_detail,
                        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET))
        .Times(1);
    EXPECT_EQ(nullptr, session_.GetOrCreateStream(
                           GetNthClientInitiatedBidirectionalId(199)));
  }
}

TEST_P(QuicSessionTestServer, DebugDFatalIfMarkingClosedStreamWriteBlocked) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  QuicStreamId closed_stream_id = stream2->id();
  // Close the stream.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(closed_stream_id, _));
  stream2->Reset(QUIC_BAD_APPLICATION_PAYLOAD);
  std::string msg = quiche::QuicheStrCat("Marking unknown stream ",
                                         closed_stream_id, " blocked.");
  EXPECT_QUIC_BUG(session_.MarkConnectionLevelWriteBlocked(closed_stream_id),
                  msg);
}

TEST_P(QuicSessionTestServer, OnCanWrite) {
  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());

  InSequence s;

  // Reregister, to test the loop limit.
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
    session_.MarkConnectionLevelWriteBlocked(stream2->id());
  }));
  // 2 will get called a second time as it didn't finish its block
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
  }));
  EXPECT_CALL(*stream6, OnCanWrite()).WillOnce(Invoke([this, stream6]() {
    session_.SendStreamData(stream6);
  }));
  // 4 will not get called, as we exceeded the loop limit.
  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, TestBatchedWrites) {
  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.set_writev_consumes_all_data(true);
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());

  // With two sessions blocked, we should get two write calls.  They should both
  // go to the first stream as it will only write 6k and mark itself blocked
  // again.
  InSequence s;
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendLargeFakeData(stream2, 6000);
    session_.MarkConnectionLevelWriteBlocked(stream2->id());
  }));
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendLargeFakeData(stream2, 6000);
    session_.MarkConnectionLevelWriteBlocked(stream2->id());
  }));
  session_.OnCanWrite();

  // We should get one more call for stream2, at which point it has used its
  // write quota and we move over to stream 4.
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendLargeFakeData(stream2, 6000);
    session_.MarkConnectionLevelWriteBlocked(stream2->id());
  }));
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendLargeFakeData(stream4, 6000);
    session_.MarkConnectionLevelWriteBlocked(stream4->id());
  }));
  session_.OnCanWrite();

  // Now let stream 4 do the 2nd of its 3 writes, but add a block for a high
  // priority stream 6.  4 should be preempted.  6 will write but *not* block so
  // will cede back to 4.
  stream6->SetPriority(spdy::SpdyStreamPrecedence(kV3HighestPriority));
  EXPECT_CALL(*stream4, OnCanWrite())
      .WillOnce(Invoke([this, stream4, stream6]() {
        session_.SendLargeFakeData(stream4, 6000);
        session_.MarkConnectionLevelWriteBlocked(stream4->id());
        session_.MarkConnectionLevelWriteBlocked(stream6->id());
      }));
  EXPECT_CALL(*stream6, OnCanWrite())
      .WillOnce(Invoke([this, stream4, stream6]() {
        session_.SendStreamData(stream6);
        session_.SendLargeFakeData(stream4, 6000);
      }));
  session_.OnCanWrite();

  // Stream4 alread did 6k worth of writes, so after doing another 12k it should
  // cede and 2 should resume.
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendLargeFakeData(stream4, 12000);
    session_.MarkConnectionLevelWriteBlocked(stream4->id());
  }));
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendLargeFakeData(stream2, 6000);
    session_.MarkConnectionLevelWriteBlocked(stream2->id());
  }));
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, Http2Priority) {
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // The test is using HTTP/2 priority which is not supported in IETF QUIC.
    return;
  }
  QuicTagVector copt;
  copt.push_back(kH2PR);
  QuicConfigPeer::SetReceivedConnectionOptions(session_.config(), copt);
  session_.OnConfigNegotiated();
  ASSERT_TRUE(session_.use_http2_priority_write_scheduler());

  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.set_writev_consumes_all_data(true);
  /*
           0
          /|\
         2 4 6
  */
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  // Verify streams are scheduled round robin.
  InSequence s;
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*stream6, OnCanWrite());
  session_.OnCanWrite();

  /*
           0
           |
           4
          / \
         2   6
  */
  // Update stream 4's priority.
  stream4->SetPriority(
      spdy::SpdyStreamPrecedence(0, spdy::kHttp2DefaultStreamWeight, true));
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.MarkConnectionLevelWriteBlocked(stream4->id());
  }));
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*stream2, OnCanWrite());
  session_.OnCanWrite();
  EXPECT_CALL(*stream6, OnCanWrite());
  session_.OnCanWrite();

  /*
         0
         |
         6
         |
         4
         |
         2
  */
  // Update stream 6's priority.
  stream6->SetPriority(
      spdy::SpdyStreamPrecedence(0, spdy::kHttp2DefaultStreamWeight, true));
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  EXPECT_CALL(*stream6, OnCanWrite()).WillOnce(Invoke([this, stream6]() {
    session_.MarkConnectionLevelWriteBlocked(stream6->id());
  }));
  EXPECT_CALL(*stream6, OnCanWrite());
  EXPECT_CALL(*stream4, OnCanWrite());
  session_.OnCanWrite();
  EXPECT_CALL(*stream2, OnCanWrite());
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, RoundRobinScheduling) {
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // IETF QUIC currently doesn't support PRIORITY.
    return;
  }
  QuicTagVector copt;
  copt.push_back(kRRWS);
  QuicConfigPeer::SetReceivedConnectionOptions(session_.config(), copt);
  session_.OnConfigNegotiated();

  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.set_writev_consumes_all_data(true);
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  // Verify streams are scheduled round robin.
  InSequence s;
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*stream6, OnCanWrite());
  session_.OnCanWrite();

  /* 2, 4, 6, 8 */
  TestStream* stream8 = session_.CreateOutgoingBidirectionalStream();

  // Verify updated priority is ignored.
  stream4->SetPriority(spdy::SpdyStreamPrecedence(spdy::kV3HighestPriority));
  session_.MarkConnectionLevelWriteBlocked(stream8->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  EXPECT_CALL(*stream8, OnCanWrite());
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream6, OnCanWrite());
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, OnCanWriteBundlesStreams) {
  // Encryption needs to be established before data can be sent.
  if (connection_->version().HasHandshakeDone()) {
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillRepeatedly(Invoke(&ClearControlFrame));
  }
  CryptoHandshakeMessage msg;
  MockPacketWriter* writer = static_cast<MockPacketWriter*>(
      QuicConnectionPeer::GetWriter(session_.connection()));
  session_.GetMutableCryptoStream()->OnHandshakeMessage(msg);

  // Drive congestion control manually.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());

  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm, GetCongestionWindow())
      .WillRepeatedly(Return(kMaxOutgoingPacketSize * 10));
  EXPECT_CALL(*send_algorithm, InRecovery()).WillRepeatedly(Return(false));
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
  }));
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendStreamData(stream4);
  }));
  EXPECT_CALL(*stream6, OnCanWrite()).WillOnce(Invoke([this, stream6]() {
    session_.SendStreamData(stream6);
  }));

  // Expect that we only send one packet, the writes from different streams
  // should be bundled together.
  EXPECT_CALL(*writer, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(*send_algorithm, OnPacketSent(_, _, _, _, _));
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));
  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, OnCanWriteCongestionControlBlocks) {
  session_.set_writev_consumes_all_data(true);
  InSequence s;

  // Drive congestion control manually.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());

  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
  }));
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream6, OnCanWrite()).WillOnce(Invoke([this, stream6]() {
    session_.SendStreamData(stream6);
  }));
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(false));
  // stream4->OnCanWrite is not called.

  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());

  // Still congestion-control blocked.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(false));
  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());

  // stream4->OnCanWrite is called once the connection stops being
  // congestion-control blocked.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendStreamData(stream4);
  }));
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));
  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, OnCanWriteWriterBlocks) {
  // Drive congestion control manually in order to ensure that
  // application-limited signaling is handled correctly.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));

  // Drive packet writer manually.
  MockPacketWriter* writer = static_cast<MockPacketWriter*>(
      QuicConnectionPeer::GetWriter(session_.connection()));
  EXPECT_CALL(*writer, IsWriteBlocked()).WillRepeatedly(Return(true));
  EXPECT_CALL(*writer, WritePacket(_, _, _, _, _)).Times(0);

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());

  EXPECT_CALL(*stream2, OnCanWrite()).Times(0);
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_)).Times(0);

  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, SendStreamsBlocked) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  for (size_t i = 0; i < kDefaultMaxStreamsPerConnection; ++i) {
    ASSERT_TRUE(session_.CanOpenNextOutgoingBidirectionalStream());
    session_.GetNextOutgoingBidirectionalStreamId();
  }
  // Next checking causes STREAMS_BLOCKED to be sent.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke([](const QuicFrame& frame) {
        EXPECT_FALSE(frame.streams_blocked_frame.unidirectional);
        EXPECT_EQ(kDefaultMaxStreamsPerConnection,
                  frame.streams_blocked_frame.stream_count);
        ClearControlFrame(frame);
        return true;
      }));
  EXPECT_FALSE(session_.CanOpenNextOutgoingBidirectionalStream());

  for (size_t i = 0; i < kDefaultMaxStreamsPerConnection; ++i) {
    ASSERT_TRUE(session_.CanOpenNextOutgoingUnidirectionalStream());
    session_.GetNextOutgoingUnidirectionalStreamId();
  }
  // Next checking causes STREAM_BLOCKED to be sent.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke([](const QuicFrame& frame) {
        EXPECT_TRUE(frame.streams_blocked_frame.unidirectional);
        EXPECT_EQ(kDefaultMaxStreamsPerConnection,
                  frame.streams_blocked_frame.stream_count);
        ClearControlFrame(frame);
        return true;
      }));
  EXPECT_FALSE(session_.CanOpenNextOutgoingUnidirectionalStream());
}

TEST_P(QuicSessionTestServer, BufferedHandshake) {
  // This test is testing behavior of crypto stream flow control, but when
  // CRYPTO frames are used, there is no flow control for the crypto handshake.
  if (QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  session_.set_writev_consumes_all_data(true);
  EXPECT_FALSE(session_.HasPendingHandshake());  // Default value.

  // Test that blocking other streams does not change our status.
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  EXPECT_FALSE(session_.HasPendingHandshake());

  TestStream* stream3 = session_.CreateOutgoingBidirectionalStream();
  session_.MarkConnectionLevelWriteBlocked(stream3->id());
  EXPECT_FALSE(session_.HasPendingHandshake());

  // Blocking (due to buffering of) the Crypto stream is detected.
  session_.MarkConnectionLevelWriteBlocked(
      QuicUtils::GetCryptoStreamId(connection_->transport_version()));
  EXPECT_TRUE(session_.HasPendingHandshake());

  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  EXPECT_TRUE(session_.HasPendingHandshake());

  InSequence s;
  // Force most streams to re-register, which is common scenario when we block
  // the Crypto stream, and only the crypto stream can "really" write.

  // Due to prioritization, we *should* be asked to write the crypto stream
  // first.
  // Don't re-register the crypto stream (which signals complete writing).
  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  EXPECT_CALL(*crypto_stream, OnCanWrite());

  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
  }));
  EXPECT_CALL(*stream3, OnCanWrite()).WillOnce(Invoke([this, stream3]() {
    session_.SendStreamData(stream3);
  }));
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendStreamData(stream4);
    session_.MarkConnectionLevelWriteBlocked(stream4->id());
  }));

  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());
  EXPECT_FALSE(session_.HasPendingHandshake());  // Crypto stream wrote.
}

TEST_P(QuicSessionTestServer, OnCanWriteWithClosedStream) {
  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  CloseStream(stream6->id());

  InSequence s;
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*stream2, OnCanWrite()).WillOnce(Invoke([this, stream2]() {
    session_.SendStreamData(stream2);
  }));
  EXPECT_CALL(*stream4, OnCanWrite()).WillOnce(Invoke([this, stream4]() {
    session_.SendStreamData(stream4);
  }));
  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, OnCanWriteLimitsNumWritesIfFlowControlBlocked) {
  // Drive congestion control manually in order to ensure that
  // application-limited signaling is handled correctly.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));

  // Ensure connection level flow control blockage.
  QuicFlowControllerPeer::SetSendWindowOffset(session_.flow_controller(), 0);
  EXPECT_TRUE(session_.flow_controller()->IsBlocked());
  EXPECT_TRUE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());

  // Mark the crypto and headers streams as write blocked, we expect them to be
  // allowed to write later.
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    session_.MarkConnectionLevelWriteBlocked(
        QuicUtils::GetCryptoStreamId(connection_->transport_version()));
  }

  // Create a data stream, and although it is write blocked we never expect it
  // to be allowed to write as we are connection level flow control blocked.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_CALL(*stream, OnCanWrite()).Times(0);

  // The crypto and headers streams should be called even though we are
  // connection flow control blocked.
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
    EXPECT_CALL(*crypto_stream, OnCanWrite());
  }

  // After the crypto and header streams perform a write, the connection will be
  // blocked by the flow control, hence it should become application-limited.
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));

  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, SendGoAway) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // In IETF QUIC, GOAWAY lives up in the HTTP layer.
    return;
  }
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  MockPacketWriter* writer = static_cast<MockPacketWriter*>(
      QuicConnectionPeer::GetWriter(session_.connection()));
  EXPECT_CALL(*writer, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));

  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallySendControlFrame));
  session_.SendGoAway(QUIC_PEER_GOING_AWAY, "Going Away.");
  EXPECT_TRUE(session_.goaway_sent());

  const QuicStreamId kTestStreamId = 5u;
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  EXPECT_CALL(*connection_,
              OnStreamReset(kTestStreamId, QUIC_STREAM_PEER_GOING_AWAY))
      .Times(0);
  EXPECT_TRUE(session_.GetOrCreateStream(kTestStreamId));
}

TEST_P(QuicSessionTestServer, DoNotSendGoAwayTwice) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // In IETF QUIC, GOAWAY lives up in the HTTP layer.
    return;
  }
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  session_.SendGoAway(QUIC_PEER_GOING_AWAY, "Going Away.");
  EXPECT_TRUE(session_.goaway_sent());
  session_.SendGoAway(QUIC_PEER_GOING_AWAY, "Going Away.");
}

TEST_P(QuicSessionTestServer, InvalidGoAway) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // In IETF QUIC, GOAWAY lives up in the HTTP layer.
    return;
  }
  QuicGoAwayFrame go_away(kInvalidControlFrameId, QUIC_PEER_GOING_AWAY,
                          session_.next_outgoing_bidirectional_stream_id(), "");
  session_.OnGoAway(go_away);
}

// Test that server session will send a connectivity probe in response to a
// connectivity probe on the same path.
TEST_P(QuicSessionTestServer, ServerReplyToConnectivityProbe) {
  QuicSocketAddress old_peer_address =
      QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort);
  EXPECT_EQ(old_peer_address, session_.peer_address());

  QuicSocketAddress new_peer_address =
      QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort + 1);

  MockPacketWriter* writer = static_cast<MockPacketWriter*>(
      QuicConnectionPeer::GetWriter(session_.connection()));
  EXPECT_CALL(*writer, WritePacket(_, _, _, new_peer_address, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(*connection_, SendConnectivityProbingResponsePacket(_))
      .WillOnce(Invoke(
          connection_,
          &MockQuicConnection::ReallySendConnectivityProbingResponsePacket));
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Need to explicitly do this to emulate the reception of a PathChallenge,
    // which stores its payload for use in generating the response.
    connection_->OnPathChallengeFrame(
        QuicPathChallengeFrame(0, path_frame_buffer1_));
  }
  session_.OnPacketReceived(session_.self_address(), new_peer_address,
                            /*is_connectivity_probe=*/true);
  EXPECT_EQ(old_peer_address, session_.peer_address());
}

// Same as above, but check that if there are two PATH_CHALLENGE frames in the
// packet, the response has both of them AND we do not do migration.  This for
// IETF QUIC only.
TEST_P(QuicSessionTestServer, ServerReplyToConnectivityProbes) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  QuicSocketAddress old_peer_address =
      QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort);
  EXPECT_EQ(old_peer_address, session_.peer_address());

  MockPacketWriter* writer = static_cast<MockPacketWriter*>(
      QuicConnectionPeer::GetWriter(session_.connection()));
  // CheckMultiPathResponse validates that the written packet
  // contains both path responses.
  EXPECT_CALL(*writer, WritePacket(_, _, _, old_peer_address, _))
      .WillOnce(Invoke(this, &QuicSessionTestServer::CheckMultiPathResponse));

  EXPECT_CALL(*connection_, SendConnectivityProbingResponsePacket(_))
      .WillOnce(Invoke(
          connection_,
          &MockQuicConnection::ReallySendConnectivityProbingResponsePacket));
  QuicConnectionPeer::SetLastHeaderFormat(connection_,
                                          IETF_QUIC_SHORT_HEADER_PACKET);
  // Need to explicitly do this to emulate the reception of a PathChallenge,
  // which stores its payload for use in generating the response.
  connection_->OnPathChallengeFrame(
      QuicPathChallengeFrame(0, path_frame_buffer1_));
  connection_->OnPathChallengeFrame(
      QuicPathChallengeFrame(0, path_frame_buffer2_));
  session_.OnPacketReceived(session_.self_address(), old_peer_address,
                            /*is_connectivity_probe=*/true);
}

TEST_P(QuicSessionTestServer, IncreasedTimeoutAfterCryptoHandshake) {
  EXPECT_EQ(kInitialIdleTimeoutSecs + 3,
            QuicConnectionPeer::GetNetworkTimeout(connection_).ToSeconds());
  if (connection_->version().HasHandshakeDone()) {
    EXPECT_CALL(*connection_, SendControlFrame(_));
  }
  CryptoHandshakeMessage msg;
  session_.GetMutableCryptoStream()->OnHandshakeMessage(msg);
  EXPECT_EQ(kMaximumIdleTimeoutSecs + 3,
            QuicConnectionPeer::GetNetworkTimeout(connection_).ToSeconds());
}

TEST_P(QuicSessionTestServer, OnStreamFrameFinStaticStreamId) {
  if (VersionUsesHttp3(connection_->transport_version())) {
    // The test relies on headers stream, which no longer exists in IETF QUIC.
    return;
  }
  QuicStreamId headers_stream_id =
      QuicUtils::GetHeadersStreamId(connection_->transport_version());
  std::unique_ptr<TestStream> fake_headers_stream =
      std::make_unique<TestStream>(headers_stream_id, &session_,
                                   /*is_static*/ true, BIDIRECTIONAL);
  QuicSessionPeer::ActivateStream(&session_, std::move(fake_headers_stream));
  // Send two bytes of payload.
  QuicStreamFrame data1(headers_stream_id, true, 0,
                        quiche::QuicheStringPiece("HT"));
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Attempt to close a static stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSessionTestServer, OnStreamFrameInvalidStreamId) {
  // Send two bytes of payload.
  QuicStreamFrame data1(
      QuicUtils::GetInvalidStreamId(connection_->transport_version()), true, 0,
      quiche::QuicheStringPiece("HT"));
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSessionTestServer, OnRstStreamInvalidStreamId) {
  // Send two bytes of payload.
  QuicRstStreamFrame rst1(
      kInvalidControlFrameId,
      QuicUtils::GetInvalidStreamId(connection_->transport_version()),
      QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnRstStream(rst1);
}

TEST_P(QuicSessionTestServer, HandshakeUnblocksFlowControlBlockedStream) {
  if (connection_->version().handshake_protocol == PROTOCOL_TLS1_3) {
    // This test requires Google QUIC crypto because it assumes streams start
    // off unblocked.
    return;
  }
  // Test that if a stream is flow control blocked, then on receipt of the SHLO
  // containing a suitable send window offset, the stream becomes unblocked.

  // Ensure that Writev consumes all the data it is given (simulate no socket
  // blocking).
  session_.set_writev_consumes_all_data(true);

  // Create a stream, and send enough data to make it flow control blocked.
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  std::string body(kMinimumFlowControlSendWindow, '.');
  EXPECT_FALSE(stream2->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(AtLeast(1));
  stream2->WriteOrBufferData(body, false, nullptr);
  EXPECT_TRUE(stream2->flow_controller()->IsBlocked());
  EXPECT_TRUE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());

  // Now complete the crypto handshake, resulting in an increased flow control
  // send window.
  CryptoHandshakeMessage msg;
  session_.GetMutableCryptoStream()->OnHandshakeMessage(msg);
  EXPECT_TRUE(QuicSessionPeer::IsStreamWriteBlocked(&session_, stream2->id()));
  // Stream is now unblocked.
  EXPECT_FALSE(stream2->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

TEST_P(QuicSessionTestServer, HandshakeUnblocksFlowControlBlockedCryptoStream) {
  if (QuicVersionUsesCryptoFrames(GetParam().transport_version)) {
    // QUIC version 47 onwards uses CRYPTO frames for the handshake, so this
    // test doesn't make sense for those versions since CRYPTO frames aren't
    // flow controlled.
    return;
  }
  // Test that if the crypto stream is flow control blocked, then if the SHLO
  // contains a larger send window offset, the stream becomes unblocked.
  session_.set_writev_consumes_all_data(true);
  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  for (QuicStreamId i = 0;
       !crypto_stream->flow_controller()->IsBlocked() && i < 1000u; i++) {
    EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
    EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
    QuicStreamOffset offset = crypto_stream->stream_bytes_written();
    QuicConfig config;
    CryptoHandshakeMessage crypto_message;
    config.ToHandshakeMessage(&crypto_message, transport_version());
    crypto_stream->SendHandshakeMessage(crypto_message);
    char buf[1000];
    QuicDataWriter writer(1000, buf, quiche::NETWORK_BYTE_ORDER);
    crypto_stream->WriteStreamData(offset, crypto_message.size(), &writer);
  }
  EXPECT_TRUE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());
  EXPECT_FALSE(session_.HasDataToWrite());
  EXPECT_TRUE(crypto_stream->HasBufferedData());

  // Now complete the crypto handshake, resulting in an increased flow control
  // send window.
  CryptoHandshakeMessage msg;
  session_.GetMutableCryptoStream()->OnHandshakeMessage(msg);
  EXPECT_TRUE(QuicSessionPeer::IsStreamWriteBlocked(
      &session_,
      QuicUtils::GetCryptoStreamId(connection_->transport_version())));
  // Stream is now unblocked and will no longer have buffered data.
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

TEST_P(QuicSessionTestServer, ConnectionFlowControlAccountingRstOutOfOrder) {
  // Test that when we receive an out of order stream RST we correctly adjust
  // our connection level flow control receive window.
  // On close, the stream should mark as consumed all bytes between the highest
  // byte consumed so far and the final byte offset from the RST frame.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();

  const QuicStreamOffset kByteOffset =
      1 + kInitialSessionFlowControlWindowForTest / 2;

  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));

  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream->id(),
                               QUIC_STREAM_CANCELLED, kByteOffset);
  session_.OnRstStream(rst_frame);
  if (VersionHasIetfQuicFrames(transport_version())) {
    // The test requires the stream to be fully closed in both directions. For
    // IETF QUIC, the RST_STREAM only closes one side.
    QuicStopSendingFrame frame(kInvalidControlFrameId, stream->id(),
                               QUIC_STREAM_CANCELLED);
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
    session_.OnStopSendingFrame(frame);
  }
  EXPECT_EQ(kByteOffset, session_.flow_controller()->bytes_consumed());
}

TEST_P(QuicSessionTestServer, ConnectionFlowControlAccountingFinAndLocalReset) {
  // Test the situation where we receive a FIN on a stream, and before we fully
  // consume all the data from the sequencer buffer we locally RST the stream.
  // The bytes between highest consumed byte, and the final byte offset that we
  // determined when the FIN arrived, should be marked as consumed at the
  // connection level flow controller when the stream is reset.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();

  const QuicStreamOffset kByteOffset =
      kInitialSessionFlowControlWindowForTest / 2 - 1;
  QuicStreamFrame frame(stream->id(), true, kByteOffset, ".");
  session_.OnStreamFrame(frame);
  EXPECT_TRUE(connection_->connected());

  EXPECT_EQ(0u, stream->flow_controller()->bytes_consumed());
  EXPECT_EQ(kByteOffset + frame.data_length,
            stream->flow_controller()->highest_received_byte_offset());

  // Reset stream locally.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  stream->Reset(QUIC_STREAM_CANCELLED);
  EXPECT_EQ(kByteOffset + frame.data_length,
            session_.flow_controller()->bytes_consumed());
}

TEST_P(QuicSessionTestServer, ConnectionFlowControlAccountingFinAfterRst) {
  // Test that when we RST the stream (and tear down stream state), and then
  // receive a FIN from the peer, we correctly adjust our connection level flow
  // control receive window.

  // Connection starts with some non-zero highest received byte offset,
  // due to other active streams.
  const uint64_t kInitialConnectionBytesConsumed = 567;
  const uint64_t kInitialConnectionHighestReceivedOffset = 1234;
  EXPECT_LT(kInitialConnectionBytesConsumed,
            kInitialConnectionHighestReceivedOffset);
  session_.flow_controller()->UpdateHighestReceivedOffset(
      kInitialConnectionHighestReceivedOffset);
  session_.flow_controller()->AddBytesConsumed(kInitialConnectionBytesConsumed);

  // Reset our stream: this results in the stream being closed locally.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  stream->Reset(QUIC_STREAM_CANCELLED);

  // Now receive a response from the peer with a FIN. We should handle this by
  // adjusting the connection level flow control receive window to take into
  // account the total number of bytes sent by the peer.
  const QuicStreamOffset kByteOffset = 5678;
  std::string body = "hello";
  QuicStreamFrame frame(stream->id(), true, kByteOffset,
                        quiche::QuicheStringPiece(body));
  session_.OnStreamFrame(frame);

  QuicStreamOffset total_stream_bytes_sent_by_peer =
      kByteOffset + body.length();
  EXPECT_EQ(kInitialConnectionBytesConsumed + total_stream_bytes_sent_by_peer,
            session_.flow_controller()->bytes_consumed());
  EXPECT_EQ(
      kInitialConnectionHighestReceivedOffset + total_stream_bytes_sent_by_peer,
      session_.flow_controller()->highest_received_byte_offset());
}

TEST_P(QuicSessionTestServer, ConnectionFlowControlAccountingRstAfterRst) {
  // Test that when we RST the stream (and tear down stream state), and then
  // receive a RST from the peer, we correctly adjust our connection level flow
  // control receive window.

  // Connection starts with some non-zero highest received byte offset,
  // due to other active streams.
  const uint64_t kInitialConnectionBytesConsumed = 567;
  const uint64_t kInitialConnectionHighestReceivedOffset = 1234;
  EXPECT_LT(kInitialConnectionBytesConsumed,
            kInitialConnectionHighestReceivedOffset);
  session_.flow_controller()->UpdateHighestReceivedOffset(
      kInitialConnectionHighestReceivedOffset);
  session_.flow_controller()->AddBytesConsumed(kInitialConnectionBytesConsumed);

  // Reset our stream: this results in the stream being closed locally.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  stream->Reset(QUIC_STREAM_CANCELLED);
  EXPECT_TRUE(QuicStreamPeer::read_side_closed(stream));

  // Now receive a RST from the peer. We should handle this by adjusting the
  // connection level flow control receive window to take into account the total
  // number of bytes sent by the peer.
  const QuicStreamOffset kByteOffset = 5678;
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream->id(),
                               QUIC_STREAM_CANCELLED, kByteOffset);
  session_.OnRstStream(rst_frame);

  EXPECT_EQ(kInitialConnectionBytesConsumed + kByteOffset,
            session_.flow_controller()->bytes_consumed());
  EXPECT_EQ(kInitialConnectionHighestReceivedOffset + kByteOffset,
            session_.flow_controller()->highest_received_byte_offset());
}

TEST_P(QuicSessionTestServer, InvalidStreamFlowControlWindowInHandshake) {
  // Test that receipt of an invalid (< default) stream flow control window from
  // the peer results in the connection being torn down.
  const uint32_t kInvalidWindow = kMinimumFlowControlSendWindow - 1;
  QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(session_.config(),
                                                            kInvalidWindow);

  if (connection_->version().handshake_protocol != PROTOCOL_TLS1_3) {
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_FLOW_CONTROL_INVALID_WINDOW, _, _));
  } else {
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  }
  session_.OnConfigNegotiated();
}

TEST_P(QuicSessionTestServer, InvalidSessionFlowControlWindowInHandshake) {
  // Test that receipt of an invalid (< default) session flow control window
  // from the peer results in the connection being torn down.
  const uint32_t kInvalidWindow = kMinimumFlowControlSendWindow - 1;
  QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(session_.config(),
                                                             kInvalidWindow);
  if (!connection_->version().AllowsLowFlowControlLimits()) {
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_FLOW_CONTROL_INVALID_WINDOW, _, _));
  } else {
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  }
  session_.OnConfigNegotiated();
}

// Test negotiation of custom server initial flow control window.
TEST_P(QuicSessionTestServer, CustomFlowControlWindow) {
  QuicTagVector copt;
  copt.push_back(kIFW7);
  QuicConfigPeer::SetReceivedConnectionOptions(session_.config(), copt);

  session_.OnConfigNegotiated();
  EXPECT_EQ(192 * 1024u, QuicFlowControllerPeer::ReceiveWindowSize(
                             session_.flow_controller()));
}

TEST_P(QuicSessionTestServer, FlowControlWithInvalidFinalOffset) {
  // Test that if we receive a stream RST with a highest byte offset that
  // violates flow control, that we close the connection.
  const uint64_t kLargeOffset = kInitialSessionFlowControlWindowForTest + 1;
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA, _, _))
      .Times(2);

  // Check that stream frame + FIN results in connection close.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  stream->Reset(QUIC_STREAM_CANCELLED);
  QuicStreamFrame frame(stream->id(), true, kLargeOffset,
                        quiche::QuicheStringPiece());
  session_.OnStreamFrame(frame);

  // Check that RST results in connection close.
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream->id(),
                               QUIC_STREAM_CANCELLED, kLargeOffset);
  session_.OnRstStream(rst_frame);
}

TEST_P(QuicSessionTestServer, TooManyUnfinishedStreamsCauseServerRejectStream) {
  // If a buggy/malicious peer creates too many streams that are not ended
  // with a FIN or RST then we send an RST to refuse streams. For IETF QUIC the
  // connection is closed.
  const QuicStreamId kMaxStreams = 5;
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_,
                                                            kMaxStreams);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, kMaxStreams);
  }
  const QuicStreamId kFirstStreamId = GetNthClientInitiatedBidirectionalId(0);
  const QuicStreamId kFinalStreamId =
      GetNthClientInitiatedBidirectionalId(kMaxStreams);
  // Create kMaxStreams data streams, and close them all without receiving a
  // FIN or a RST_STREAM from the client.
  for (QuicStreamId i = kFirstStreamId; i < kFinalStreamId;
       i += QuicUtils::StreamIdDelta(connection_->transport_version())) {
    QuicStreamFrame data1(i, false, 0, quiche::QuicheStringPiece("HT"));
    session_.OnStreamFrame(data1);
    // EXPECT_EQ(1u, session_.GetNumOpenStreams());
    if (VersionHasIetfQuicFrames(transport_version())) {
      // Expect two control frames, RST STREAM and STOP SENDING
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .Times(2)
          .WillRepeatedly(Invoke(&ClearControlFrame));
    } else {
      // Expect one control frame, just RST STREAM
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
    }
    // Close stream. Should not make new streams available since
    // the stream is not finished.
    EXPECT_CALL(*connection_, OnStreamReset(i, _));
    session_.CloseStream(i);
  }

  if (VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INVALID_STREAM_ID,
                        "Stream id 20 would exceed stream count limit 5", _));
  } else {
    EXPECT_CALL(*connection_, SendControlFrame(_)).Times(1);
    EXPECT_CALL(*connection_,
                OnStreamReset(kFinalStreamId, QUIC_REFUSED_STREAM))
        .Times(1);
  }
  // Create one more data streams to exceed limit of open stream.
  QuicStreamFrame data1(kFinalStreamId, false, 0,
                        quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSessionTestServer, DrainingStreamsDoNotCountAsOpenedOutgoing) {
  // Verify that a draining stream (which has received a FIN but not consumed
  // it) does not count against the open quota (because it is closed from the
  // protocol point of view).
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();
  QuicStreamFrame data1(stream_id, true, 0, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(1);
  session_.StreamDraining(stream_id);
}

TEST_P(QuicSessionTestServer, NoPendingStreams) {
  session_.set_uses_pending_streams(false);

  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame data1(stream_id, true, 10, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(1, session_.num_incoming_streams_created());

  QuicStreamFrame data2(stream_id, false, 0, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data2);
  EXPECT_EQ(1, session_.num_incoming_streams_created());
}

TEST_P(QuicSessionTestServer, PendingStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  session_.set_uses_pending_streams(true);

  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame data1(stream_id, true, 10, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_TRUE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());

  QuicStreamFrame data2(stream_id, false, 0, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data2);
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(1, session_.num_incoming_streams_created());
}

TEST_P(QuicSessionTestServer, RstPendingStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  session_.set_uses_pending_streams(true);

  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame data1(stream_id, true, 10, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_TRUE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  QuicRstStreamFrame rst1(kInvalidControlFrameId, stream_id,
                          QUIC_ERROR_PROCESSING_STREAM, 12);
  session_.OnRstStream(rst1);
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  QuicStreamFrame data2(stream_id, false, 0, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data2);
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
}

TEST_P(QuicSessionTestServer, OnFinPendingStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  session_.set_uses_pending_streams(true);

  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame data(stream_id, true, 0, "");
  session_.OnStreamFrame(data);

  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
}

TEST_P(QuicSessionTestServer, PendingStreamOnWindowUpdate) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  session_.set_uses_pending_streams(true);
  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame data1(stream_id, true, 10, quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_TRUE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_EQ(0, session_.num_incoming_streams_created());
  QuicWindowUpdateFrame window_update_frame(kInvalidControlFrameId, stream_id,
                                            0);
  EXPECT_CALL(
      *connection_,
      CloseConnection(
          QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM,
          "WindowUpdateFrame received on READ_UNIDIRECTIONAL stream.", _));
  session_.OnWindowUpdateFrame(window_update_frame);
}

TEST_P(QuicSessionTestServer, DrainingStreamsDoNotCountAsOpened) {
  // Verify that a draining stream (which has received a FIN but not consumed
  // it) does not count against the open quota (because it is closed from the
  // protocol point of view).
  if (VersionHasIetfQuicFrames(transport_version())) {
    // On IETF QUIC, we will expect to see a MAX_STREAMS go out when there are
    // not enough streams to create the next one.
    EXPECT_CALL(*connection_, SendControlFrame(_)).Times(1);
  } else {
    EXPECT_CALL(*connection_, SendControlFrame(_)).Times(0);
  }
  EXPECT_CALL(*connection_, OnStreamReset(_, QUIC_REFUSED_STREAM)).Times(0);
  const QuicStreamId kMaxStreams = 5;
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_,
                                                            kMaxStreams);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, kMaxStreams);
  }

  // Create kMaxStreams + 1 data streams, and mark them draining.
  const QuicStreamId kFirstStreamId = GetNthClientInitiatedBidirectionalId(0);
  const QuicStreamId kFinalStreamId =
      GetNthClientInitiatedBidirectionalId(2 * kMaxStreams + 1);
  for (QuicStreamId i = kFirstStreamId; i < kFinalStreamId;
       i += QuicUtils::StreamIdDelta(connection_->transport_version())) {
    QuicStreamFrame data1(i, true, 0, quiche::QuicheStringPiece("HT"));
    session_.OnStreamFrame(data1);
    EXPECT_EQ(1u, session_.GetNumOpenIncomingStreams());
    session_.StreamDraining(i);
    EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
  }
}

class QuicSessionTestClient : public QuicSessionTestBase {
 protected:
  QuicSessionTestClient()
      : QuicSessionTestBase(Perspective::IS_CLIENT,
                            /*configure_session=*/true) {}
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSessionTestClient,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSessionTestClient, AvailableBidirectionalStreamsClient) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(2)) != nullptr);
  // Smaller bidirectional streams should be available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedBidirectionalId(0)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedBidirectionalId(1)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(0)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(1)) != nullptr);
  // And 5 should be not available.
  EXPECT_FALSE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(1)));
}

TEST_P(QuicSessionTestClient, OnMaxStreamFrame) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  QuicMaxStreamsFrame frame;
  frame.unidirectional = false;
  frame.stream_count = 120;
  EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(1);
  session_.OnMaxStreamsFrame(frame);

  QuicMaxStreamsFrame frame2;
  frame2.unidirectional = false;
  frame2.stream_count = 110;
  EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(0);
  session_.OnMaxStreamsFrame(frame2);
}

TEST_P(QuicSessionTestClient, AvailableUnidirectionalStreamsClient) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedUnidirectionalId(2)) != nullptr);
  // Smaller unidirectional streams should be available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedUnidirectionalId(0)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedUnidirectionalId(1)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedUnidirectionalId(0)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedUnidirectionalId(1)) != nullptr);
  // And 5 should be not available.
  EXPECT_FALSE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedUnidirectionalId(1)));
}

TEST_P(QuicSessionTestClient, RecordFinAfterReadSideClosed) {
  // Verify that an incoming FIN is recorded in a stream object even if the read
  // side has been closed.  This prevents an entry from being made in
  // locally_closed_streams_highest_offset_ (which will never be deleted).
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();

  // Close the read side manually.
  QuicStreamPeer::CloseReadSide(stream);

  // Receive a stream data frame with FIN.
  QuicStreamFrame frame(stream_id, true, 0, quiche::QuicheStringPiece());
  session_.OnStreamFrame(frame);
  EXPECT_TRUE(stream->fin_received());

  // Reset stream locally.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  stream->Reset(QUIC_STREAM_CANCELLED);
  EXPECT_TRUE(QuicStreamPeer::read_side_closed(stream));

  EXPECT_TRUE(connection_->connected());
  EXPECT_TRUE(QuicSessionPeer::IsStreamClosed(&session_, stream_id));
  EXPECT_FALSE(QuicSessionPeer::IsStreamCreated(&session_, stream_id));

  // The stream is not waiting for the arrival of the peer's final offset as it
  // was received with the FIN earlier.
  EXPECT_EQ(
      0u,
      QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(&session_).size());
}

TEST_P(QuicSessionTestClient, IncomingStreamWithClientInitiatedStreamId) {
  const QuicErrorCode expected_error =
      VersionHasIetfQuicFrames(transport_version())
          ? QUIC_HTTP_STREAM_WRONG_DIRECTION
          : QUIC_INVALID_STREAM_ID;
  EXPECT_CALL(
      *connection_,
      CloseConnection(expected_error, "Data for nonexistent stream",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));

  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(1),
                        /* fin = */ false, /* offset = */ 0,
                        quiche::QuicheStringPiece("foo"));
  session_.OnStreamFrame(frame);
}

TEST_P(QuicSessionTestServer, ZombieStreams) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  QuicStreamPeer::SetStreamBytesWritten(3, stream2);
  EXPECT_TRUE(stream2->IsWaitingForAcks());

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream2->id(), _));
  session_.CloseStream(stream2->id());
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  ASSERT_EQ(1u, session_.closed_streams()->size());
  EXPECT_EQ(stream2->id(), session_.closed_streams()->front()->id());
  session_.OnStreamDoneWaitingForAcks(stream2->id());
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  EXPECT_EQ(1u, session_.closed_streams()->size());
  EXPECT_EQ(stream2->id(), session_.closed_streams()->front()->id());
}

TEST_P(QuicSessionTestServer, RstStreamReceivedAfterRstStreamSent) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  QuicStreamPeer::SetStreamBytesWritten(3, stream2);
  EXPECT_TRUE(stream2->IsWaitingForAcks());

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream2->id(), _));
  EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(0);
  stream2->Reset(quic::QUIC_STREAM_CANCELLED);

  QuicRstStreamFrame rst1(kInvalidControlFrameId, stream2->id(),
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  if (!VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(session_, OnCanCreateNewOutgoingStream(false)).Times(1);
  }
  session_.OnRstStream(rst1);
}

// Regression test of b/71548958.
TEST_P(QuicSessionTestServer, TestZombieStreams) {
  session_.set_writev_consumes_all_data(true);

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  std::string body(100, '.');
  stream2->WriteOrBufferData(body, false, nullptr);
  EXPECT_TRUE(stream2->IsWaitingForAcks());
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream2).size());

  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream2->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  // Just for the RST_STREAM
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  if (VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(*connection_,
                OnStreamReset(stream2->id(), QUIC_STREAM_CANCELLED));
  } else {
    EXPECT_CALL(*connection_,
                OnStreamReset(stream2->id(), QUIC_RST_ACKNOWLEDGEMENT));
  }
  stream2->OnStreamReset(rst_frame);

  if (VersionHasIetfQuicFrames(transport_version())) {
    // The test requires the stream to be fully closed in both directions. For
    // IETF QUIC, the RST_STREAM only closes one side.
    QuicStopSendingFrame frame(kInvalidControlFrameId, stream2->id(),
                               QUIC_STREAM_CANCELLED);
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
    session_.OnStopSendingFrame(frame);
  }
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  ASSERT_EQ(1u, session_.closed_streams()->size());
  EXPECT_EQ(stream2->id(), session_.closed_streams()->front()->id());

  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Once for the RST_STREAM, once for the STOP_SENDING
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .Times(2)
        .WillRepeatedly(Invoke(&ClearControlFrame));
  } else {
    // Just for the RST_STREAM
    EXPECT_CALL(*connection_, SendControlFrame(_)).Times(1);
  }
  EXPECT_CALL(*connection_,
              OnStreamReset(stream4->id(), QUIC_STREAM_CANCELLED));
  stream4->WriteOrBufferData(body, false, nullptr);
  // Note well: Reset() actually closes the stream in both directions. For
  // GOOGLE QUIC it sends a RST_STREAM (which does a 2-way close), for IETF
  // QUIC it sends both a RST_STREAM and a STOP_SENDING (each of which
  // closes in only one direction).
  stream4->Reset(QUIC_STREAM_CANCELLED);
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream4->id()));
  EXPECT_EQ(2u, session_.closed_streams()->size());
}

TEST_P(QuicSessionTestServer, OnStreamFrameLost) {
  InSequence s;

  // Drive congestion control manually.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);

  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();

  QuicStreamFrame frame1;
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    frame1 = QuicStreamFrame(
        QuicUtils::GetCryptoStreamId(connection_->transport_version()), false,
        0, 1300);
  }
  QuicStreamFrame frame2(stream2->id(), false, 0, 9);
  QuicStreamFrame frame3(stream4->id(), false, 0, 9);

  // Lost data on cryption stream, streams 2 and 4.
  EXPECT_CALL(*stream4, HasPendingRetransmission()).WillOnce(Return(true));
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .WillOnce(Return(true));
  }
  EXPECT_CALL(*stream2, HasPendingRetransmission()).WillOnce(Return(true));
  session_.OnFrameLost(QuicFrame(frame3));
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    session_.OnFrameLost(QuicFrame(frame1));
  } else {
    QuicCryptoFrame crypto_frame(ENCRYPTION_INITIAL, 0, 1300);
    session_.OnFrameLost(QuicFrame(&crypto_frame));
  }
  session_.OnFrameLost(QuicFrame(frame2));
  EXPECT_TRUE(session_.WillingAndAbleToWrite());

  // Mark streams 2 and 4 write blocked.
  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());

  // Lost data is retransmitted before new data, and retransmissions for crypto
  // stream go first.
  // Do not check congestion window when crypto stream has lost data.
  EXPECT_CALL(*send_algorithm, CanSend(_)).Times(0);
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    EXPECT_CALL(*crypto_stream, OnCanWrite());
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .WillOnce(Return(false));
  }
  // Check congestion window for non crypto streams.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*stream4, HasPendingRetransmission()).WillOnce(Return(false));
  // Connection is blocked.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(false));

  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());

  // Unblock connection.
  // Stream 2 retransmits lost data.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream2, HasPendingRetransmission()).WillOnce(Return(false));
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  // Stream 2 sends new data.
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillOnce(Return(true));
  EXPECT_CALL(*stream4, OnCanWrite());
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));

  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSessionTestServer, DonotRetransmitDataOfClosedStreams) {
  InSequence s;

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();

  QuicStreamFrame frame1(stream2->id(), false, 0, 9);
  QuicStreamFrame frame2(stream4->id(), false, 0, 9);
  QuicStreamFrame frame3(stream6->id(), false, 0, 9);

  EXPECT_CALL(*stream6, HasPendingRetransmission()).WillOnce(Return(true));
  EXPECT_CALL(*stream4, HasPendingRetransmission()).WillOnce(Return(true));
  EXPECT_CALL(*stream2, HasPendingRetransmission()).WillOnce(Return(true));
  session_.OnFrameLost(QuicFrame(frame3));
  session_.OnFrameLost(QuicFrame(frame2));
  session_.OnFrameLost(QuicFrame(frame1));

  session_.MarkConnectionLevelWriteBlocked(stream2->id());
  session_.MarkConnectionLevelWriteBlocked(stream4->id());
  session_.MarkConnectionLevelWriteBlocked(stream6->id());

  // Reset stream 4 locally.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream4->id(), _));
  stream4->Reset(QUIC_STREAM_CANCELLED);

  // Verify stream 4 is removed from streams with lost data list.
  EXPECT_CALL(*stream6, OnCanWrite());
  EXPECT_CALL(*stream6, HasPendingRetransmission()).WillOnce(Return(false));
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream2, HasPendingRetransmission()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*stream2, OnCanWrite());
  EXPECT_CALL(*stream6, OnCanWrite());
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, RetransmitFrames) {
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);
  InSequence s;

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream6 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  session_.SendWindowUpdate(stream2->id(), 9);

  QuicStreamFrame frame1(stream2->id(), false, 0, 9);
  QuicStreamFrame frame2(stream4->id(), false, 0, 9);
  QuicStreamFrame frame3(stream6->id(), false, 0, 9);
  QuicWindowUpdateFrame window_update(1, stream2->id(), 9);
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1));
  frames.push_back(QuicFrame(&window_update));
  frames.push_back(QuicFrame(frame2));
  frames.push_back(QuicFrame(frame3));
  EXPECT_FALSE(session_.WillingAndAbleToWrite());

  EXPECT_CALL(*stream2, RetransmitStreamData(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_CALL(*stream4, RetransmitStreamData(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*stream6, RetransmitStreamData(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));
  session_.RetransmitFrames(frames, TLP_RETRANSMISSION);
}

// Regression test of b/110082001.
TEST_P(QuicSessionTestServer, RetransmitLostDataCausesConnectionClose) {
  // This test mimics the scenario when a dynamic stream retransmits lost data
  // and causes connection close.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamFrame frame(stream->id(), false, 0, 9);

  EXPECT_CALL(*stream, HasPendingRetransmission())
      .Times(2)
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  session_.OnFrameLost(QuicFrame(frame));
  // Retransmit stream data causes connection close. Stream has not sent fin
  // yet, so an RST is sent.
  EXPECT_CALL(*stream, OnCanWrite())
      .WillOnce(Invoke(stream, &QuicStream::OnClose));
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Once for the RST_STREAM, once for the STOP_SENDING
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .Times(2)
        .WillRepeatedly(Invoke(&session_, &TestSession::SaveFrame));
  } else {
    // Just for the RST_STREAM
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(&session_, &TestSession::SaveFrame));
  }
  EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, SendMessage) {
  // Cannot send message when encryption is not established.
  EXPECT_FALSE(session_.OneRttKeysAvailable());
  quic::QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  EXPECT_EQ(MessageResult(MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED, 0),
            session_.SendMessage(
                MakeSpan(connection_->helper()->GetStreamSendBufferAllocator(),
                         "", &storage)));

  // Finish handshake.
  if (connection_->version().HasHandshakeDone()) {
    EXPECT_CALL(*connection_, SendControlFrame(_));
  }
  CryptoHandshakeMessage handshake_message;
  session_.GetMutableCryptoStream()->OnHandshakeMessage(handshake_message);
  EXPECT_TRUE(session_.OneRttKeysAvailable());

  quiche::QuicheStringPiece message;
  EXPECT_CALL(*connection_, SendMessage(1, _, false))
      .WillOnce(Return(MESSAGE_STATUS_SUCCESS));
  EXPECT_EQ(MessageResult(MESSAGE_STATUS_SUCCESS, 1),
            session_.SendMessage(
                MakeSpan(connection_->helper()->GetStreamSendBufferAllocator(),
                         message, &storage)));
  // Verify message_id increases.
  EXPECT_CALL(*connection_, SendMessage(2, _, false))
      .WillOnce(Return(MESSAGE_STATUS_TOO_LARGE));
  EXPECT_EQ(MessageResult(MESSAGE_STATUS_TOO_LARGE, 0),
            session_.SendMessage(
                MakeSpan(connection_->helper()->GetStreamSendBufferAllocator(),
                         message, &storage)));
  // Verify unsent message does not consume a message_id.
  EXPECT_CALL(*connection_, SendMessage(2, _, false))
      .WillOnce(Return(MESSAGE_STATUS_SUCCESS));
  EXPECT_EQ(MessageResult(MESSAGE_STATUS_SUCCESS, 2),
            session_.SendMessage(
                MakeSpan(connection_->helper()->GetStreamSendBufferAllocator(),
                         message, &storage)));

  QuicMessageFrame frame(1);
  QuicMessageFrame frame2(2);
  EXPECT_FALSE(session_.IsFrameOutstanding(QuicFrame(&frame)));
  EXPECT_FALSE(session_.IsFrameOutstanding(QuicFrame(&frame2)));

  // Lost message 2.
  session_.OnMessageLost(2);
  EXPECT_FALSE(session_.IsFrameOutstanding(QuicFrame(&frame2)));

  // message 1 gets acked.
  session_.OnMessageAcked(1, QuicTime::Zero());
  EXPECT_FALSE(session_.IsFrameOutstanding(QuicFrame(&frame)));
}

// Regression test of b/115323618.
TEST_P(QuicSessionTestServer, LocallyResetZombieStreams) {
  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  std::string body(100, '.');
  stream2->CloseReadSide();
  stream2->WriteOrBufferData(body, true, nullptr);
  EXPECT_TRUE(stream2->IsWaitingForAcks());
  // Verify stream2 is a zombie streams.
  EXPECT_TRUE(QuicContainsKey(session_.zombie_streams(), stream2->id()));

  QuicStreamFrame frame(stream2->id(), true, 0, 100);
  EXPECT_CALL(*stream2, HasPendingRetransmission())
      .WillRepeatedly(Return(true));
  session_.OnFrameLost(QuicFrame(frame));

  // Reset stream2 locally.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(stream2->id(), _));
  stream2->Reset(QUIC_STREAM_CANCELLED);

  // Verify stream 2 gets closed.
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  EXPECT_TRUE(session_.IsClosedStream(stream2->id()));
  EXPECT_CALL(*stream2, OnCanWrite()).Times(0);
  session_.OnCanWrite();
}

TEST_P(QuicSessionTestServer, CleanUpClosedStreamsAlarm) {
  EXPECT_FALSE(
      QuicSessionPeer::GetCleanUpClosedStreamsAlarm(&session_)->IsSet());

  session_.set_writev_consumes_all_data(true);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_FALSE(stream2->IsWaitingForAcks());

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream2->id(), _));
  session_.CloseStream(stream2->id());
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  EXPECT_EQ(1u, session_.closed_streams()->size());
  EXPECT_TRUE(
      QuicSessionPeer::GetCleanUpClosedStreamsAlarm(&session_)->IsSet());

  alarm_factory_.FireAlarm(
      QuicSessionPeer::GetCleanUpClosedStreamsAlarm(&session_));
  EXPECT_TRUE(session_.closed_streams()->empty());
}

TEST_P(QuicSessionTestServer, WriteUnidirectionalStream) {
  session_.set_writev_consumes_all_data(true);
  TestStream* stream4 = new TestStream(GetNthServerInitiatedUnidirectionalId(1),
                                       &session_, WRITE_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));
  std::string body(100, '.');
  stream4->WriteOrBufferData(body, false, nullptr);
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream4->id()));
  stream4->WriteOrBufferData(body, true, nullptr);
  EXPECT_TRUE(QuicContainsKey(session_.zombie_streams(), stream4->id()));
}

TEST_P(QuicSessionTestServer, ReceivedDataOnWriteUnidirectionalStream) {
  TestStream* stream4 = new TestStream(GetNthServerInitiatedUnidirectionalId(1),
                                       &session_, WRITE_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM, _, _))
      .Times(1);
  QuicStreamFrame stream_frame(GetNthServerInitiatedUnidirectionalId(1), false,
                               0, 2);
  session_.OnStreamFrame(stream_frame);
}

TEST_P(QuicSessionTestServer, ReadUnidirectionalStream) {
  TestStream* stream4 = new TestStream(GetNthClientInitiatedUnidirectionalId(1),
                                       &session_, READ_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));
  EXPECT_FALSE(stream4->IsWaitingForAcks());
  // Discard all incoming data.
  stream4->StopReading();

  std::string data(100, '.');
  QuicStreamFrame stream_frame(GetNthClientInitiatedUnidirectionalId(1), false,
                               0, data);
  stream4->OnStreamFrame(stream_frame);
  EXPECT_TRUE(session_.closed_streams()->empty());

  QuicStreamFrame stream_frame2(GetNthClientInitiatedUnidirectionalId(1), true,
                                100, data);
  stream4->OnStreamFrame(stream_frame2);
  EXPECT_EQ(1u, session_.closed_streams()->size());
}

TEST_P(QuicSessionTestServer, WriteOrBufferDataOnReadUnidirectionalStream) {
  TestStream* stream4 = new TestStream(GetNthClientInitiatedUnidirectionalId(1),
                                       &session_, READ_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));

  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM, _, _))
      .Times(1);
  std::string body(100, '.');
  stream4->WriteOrBufferData(body, false, nullptr);
}

TEST_P(QuicSessionTestServer, WritevDataOnReadUnidirectionalStream) {
  TestStream* stream4 = new TestStream(GetNthClientInitiatedUnidirectionalId(1),
                                       &session_, READ_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));

  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM, _, _))
      .Times(1);
  std::string body(100, '.');
  struct iovec iov = {const_cast<char*>(body.data()), body.length()};
  QuicMemSliceStorage storage(
      &iov, 1, session_.connection()->helper()->GetStreamSendBufferAllocator(),
      1024);
  stream4->WriteMemSlices(storage.ToSpan(), false);
}

TEST_P(QuicSessionTestServer, WriteMemSlicesOnReadUnidirectionalStream) {
  TestStream* stream4 = new TestStream(GetNthClientInitiatedUnidirectionalId(1),
                                       &session_, READ_UNIDIRECTIONAL);
  session_.ActivateStream(QuicWrapUnique(stream4));

  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM, _, _))
      .Times(1);
  char data[1024];
  std::vector<std::pair<char*, size_t>> buffers;
  buffers.push_back(std::make_pair(data, QUICHE_ARRAYSIZE(data)));
  buffers.push_back(std::make_pair(data, QUICHE_ARRAYSIZE(data)));
  QuicTestMemSliceVector vector(buffers);
  stream4->WriteMemSlices(vector.span(), false);
}

// Test code that tests that an incoming stream frame with a new (not previously
// seen) stream id is acceptable. The ID must not be larger than has been
// advertised. It may be equal to what has been advertised.  These tests
// invoke QuicStreamIdManager::MaybeIncreaseLargestPeerStreamId by calling
// QuicSession::OnStreamFrame in order to check that all the steps are connected
// properly and that nothing in the call path interferes with the check.
// First test make sure that streams with ids below the limit are accepted.
TEST_P(QuicSessionTestServer, NewStreamIdBelowLimit) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Applicable only to IETF QUIC
    return;
  }
  QuicStreamId bidirectional_stream_id = StreamCountToId(
      QuicSessionPeer::v99_streamid_manager(&session_)
              ->advertised_max_incoming_bidirectional_streams() -
          1,
      Perspective::IS_CLIENT,
      /*bidirectional=*/true);

  QuicStreamFrame bidirectional_stream_frame(bidirectional_stream_id, false, 0,
                                             "Random String");
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStreamFrame(bidirectional_stream_frame);

  QuicStreamId unidirectional_stream_id = StreamCountToId(
      QuicSessionPeer::v99_streamid_manager(&session_)
              ->advertised_max_incoming_unidirectional_streams() -
          1,
      Perspective::IS_CLIENT,
      /*bidirectional=*/false);
  QuicStreamFrame unidirectional_stream_frame(unidirectional_stream_id, false,
                                              0, "Random String");
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStreamFrame(unidirectional_stream_frame);
}

// Accept a stream with an ID that equals the limit.
TEST_P(QuicSessionTestServer, NewStreamIdAtLimit) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Applicable only to IETF QUIC
    return;
  }
  QuicStreamId bidirectional_stream_id =
      StreamCountToId(QuicSessionPeer::v99_streamid_manager(&session_)
                          ->advertised_max_incoming_bidirectional_streams(),
                      Perspective::IS_CLIENT, /*bidirectional=*/true);
  QuicStreamFrame bidirectional_stream_frame(bidirectional_stream_id, false, 0,
                                             "Random String");
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStreamFrame(bidirectional_stream_frame);

  QuicStreamId unidirectional_stream_id =
      StreamCountToId(QuicSessionPeer::v99_streamid_manager(&session_)
                          ->advertised_max_incoming_unidirectional_streams(),
                      Perspective::IS_CLIENT, /*bidirectional=*/false);
  QuicStreamFrame unidirectional_stream_frame(unidirectional_stream_id, false,
                                              0, "Random String");
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStreamFrame(unidirectional_stream_frame);
}

// Close the connection if the id exceeds the limit.
TEST_P(QuicSessionTestServer, NewStreamIdAboveLimit) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Applicable only to IETF QUIC
    return;
  }

  QuicStreamId bidirectional_stream_id = StreamCountToId(
      QuicSessionPeer::v99_streamid_manager(&session_)
              ->advertised_max_incoming_bidirectional_streams() +
          1,
      Perspective::IS_CLIENT, /*bidirectional=*/true);
  QuicStreamFrame bidirectional_stream_frame(bidirectional_stream_id, false, 0,
                                             "Random String");
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_INVALID_STREAM_ID,
                      "Stream id 400 would exceed stream count limit 100", _));
  session_.OnStreamFrame(bidirectional_stream_frame);

  QuicStreamId unidirectional_stream_id = StreamCountToId(
      QuicSessionPeer::v99_streamid_manager(&session_)
              ->advertised_max_incoming_unidirectional_streams() +
          1,
      Perspective::IS_CLIENT, /*bidirectional=*/false);
  QuicStreamFrame unidirectional_stream_frame(unidirectional_stream_id, false,
                                              0, "Random String");
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_INVALID_STREAM_ID,
                      "Stream id 402 would exceed stream count limit 100", _));
  session_.OnStreamFrame(unidirectional_stream_frame);
}

// Checks that invalid stream ids are handled.
TEST_P(QuicSessionTestServer, OnStopSendingInvalidStreamId) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  // Check that "invalid" stream ids are rejected.
  QuicStopSendingFrame frame(1, -1, 123);
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_INVALID_STREAM_ID,
                      "Received STOP_SENDING for an invalid stream", _));
  session_.OnStopSendingFrame(frame);
}

TEST_P(QuicSessionTestServer, OnStopSendingReadUnidirectional) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  // It's illegal to send STOP_SENDING with a stream ID that is read-only.
  QuicStopSendingFrame frame(1, GetNthClientInitiatedUnidirectionalId(1), 123);
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_INVALID_STREAM_ID,
                      "Received STOP_SENDING for a read-only stream", _));
  session_.OnStopSendingFrame(frame);
}

// Static streams ignore STOP_SENDING.
TEST_P(QuicSessionTestServer, OnStopSendingStaticStreams) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  QuicStreamId stream_id = 0;
  std::unique_ptr<TestStream> fake_static_stream = std::make_unique<TestStream>(
      stream_id, &session_, /*is_static*/ true, BIDIRECTIONAL);
  QuicSessionPeer::ActivateStream(&session_, std::move(fake_static_stream));
  // Check that a stream id in the static stream map is ignored.
  QuicStopSendingFrame frame(1, stream_id, 123);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID,
                              "Received STOP_SENDING for a static stream", _));
  session_.OnStopSendingFrame(frame);
}

// If stream is write closed, do not send a RESET_STREAM frame.
TEST_P(QuicSessionTestServer, OnStopSendingForWriteClosedStream) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }

  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();
  stream->CloseWriteSide();
  EXPECT_TRUE(stream->write_side_closed());
  QuicStopSendingFrame frame(1, stream_id, 123);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStopSendingFrame(frame);
}

// If stream is closed, return true and do not close the connection.
TEST_P(QuicSessionTestServer, OnStopSendingClosedStream) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }

  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();
  // Expect these as side effect of closing the stream.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(_, _));
  session_.CloseStream(stream_id);
  QuicStopSendingFrame frame(1, stream_id, 123);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStopSendingFrame(frame);
}

// If stream id is a nonexistent local stream, return false and close the
// connection.
TEST_P(QuicSessionTestServer, OnStopSendingInputNonExistentLocalStream) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }

  QuicStopSendingFrame frame(1, GetNthServerInitiatedBidirectionalId(123456),
                             123);
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_STREAM_WRONG_DIRECTION,
                                            "Data for nonexistent stream", _))
      .Times(1);
  session_.OnStopSendingFrame(frame);
}

// If a STOP_SENDING is received for a peer initiated stream, the new stream
// will be created.
TEST_P(QuicSessionTestServer, OnStopSendingNewStream) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }
  QuicStopSendingFrame frame(1, GetNthClientInitiatedBidirectionalId(1), 123);

  // A Rst will be sent as a response for STOP_SENDING.
  EXPECT_CALL(*connection_, SendControlFrame(_)).Times(1);
  EXPECT_CALL(*connection_, OnStreamReset(_, _)).Times(1);
  session_.OnStopSendingFrame(frame);

  QuicStream* stream =
      session_.GetOrCreateStream(GetNthClientInitiatedBidirectionalId(1));
  EXPECT_TRUE(stream);
  EXPECT_TRUE(stream->write_side_closed());
}

// For a valid stream, ensure that all works
TEST_P(QuicSessionTestServer, OnStopSendingInputValidStream) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Applicable only to IETF QUIC
    return;
  }

  TestStream* stream = session_.CreateOutgoingBidirectionalStream();

  // Ensure that the stream starts out open in both directions.
  EXPECT_FALSE(stream->write_side_closed());
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream));

  QuicStreamId stream_id = stream->id();
  QuicStopSendingFrame frame(1, stream_id, 123);
  // Expect a reset to come back out.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(
      *connection_,
      OnStreamReset(stream_id, static_cast<QuicRstStreamErrorCode>(123)));
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_.OnStopSendingFrame(frame);

  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream));
  EXPECT_TRUE(stream->write_side_closed());
}

TEST_P(QuicSessionTestServer, WriteBufferedCryptoFrames) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  std::string data(1350, 'a');
  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  // Only consumed 1000 bytes.
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Return(1000));
  crypto_stream->WriteCryptoData(ENCRYPTION_INITIAL, data);
  EXPECT_TRUE(session_.HasPendingHandshake());
  EXPECT_TRUE(session_.WillingAndAbleToWrite());

  EXPECT_CALL(*connection_, SendCryptoData(_, _, _)).Times(0);
  crypto_stream->WriteCryptoData(ENCRYPTION_ZERO_RTT, data);

  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 350, 1000))
      .WillOnce(Return(350));
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 1350, 0))
      .WillOnce(Return(1350));
  session_.OnCanWrite();
  EXPECT_FALSE(session_.HasPendingHandshake());
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

// Regression test for
// https://bugs.chromium.org/p/chromium/issues/detail?id=1002119
TEST_P(QuicSessionTestServer, StreamFrameReceivedAfterFin) {
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  QuicStreamFrame frame(stream->id(), true, 0, ",");
  session_.OnStreamFrame(frame);

  QuicStreamFrame frame1(stream->id(), false, 1, ",");
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET, _, _));
  session_.OnStreamFrame(frame1);
}

TEST_P(QuicSessionTestServer, ResetForIETFStreamTypes) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return;
  }

  QuicStreamId read_only = GetNthClientInitiatedUnidirectionalId(0);

  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(read_only, _));
  session_.SendRstStream(read_only, QUIC_STREAM_CANCELLED, 0);

  QuicStreamId write_only = GetNthServerInitiatedUnidirectionalId(0);
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(1)
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(write_only, _));
  session_.SendRstStream(write_only, QUIC_STREAM_CANCELLED, 0);

  QuicStreamId bidirectional = GetNthClientInitiatedBidirectionalId(0);
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(bidirectional, _));
  session_.SendRstStream(bidirectional, QUIC_STREAM_CANCELLED, 0);
}

TEST_P(QuicSessionTestServer, DecryptionKeyAvailableBeforeEncryptionKey) {
  if (connection_->version().handshake_protocol != PROTOCOL_TLS1_3) {
    return;
  }
  ASSERT_FALSE(connection_->framer().HasEncrypterOfEncryptionLevel(
      ENCRYPTION_HANDSHAKE));
  EXPECT_FALSE(session_.OnNewDecryptionKeyAvailable(
      ENCRYPTION_HANDSHAKE, /*decrypter=*/nullptr,
      /*set_alternative_decrypter=*/false, /*latch_once_used=*/false));
}

TEST_P(QuicSessionTestServer, IncomingStreamWithServerInitiatedStreamId) {
  const QuicErrorCode expected_error =
      VersionHasIetfQuicFrames(transport_version())
          ? QUIC_HTTP_STREAM_WRONG_DIRECTION
          : QUIC_INVALID_STREAM_ID;
  EXPECT_CALL(
      *connection_,
      CloseConnection(expected_error, "Data for nonexistent stream",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));

  QuicStreamFrame frame(GetNthServerInitiatedBidirectionalId(1),
                        /* fin = */ false, /* offset = */ 0,
                        quiche::QuicheStringPiece("foo"));
  session_.OnStreamFrame(frame);
}

// A client test class that can be used when the automatic configuration is not
// desired.
class QuicSessionTestClientUnconfigured : public QuicSessionTestBase {
 protected:
  QuicSessionTestClientUnconfigured()
      : QuicSessionTestBase(Perspective::IS_CLIENT,
                            /*configure_session=*/false) {}
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSessionTestClientUnconfigured,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSessionTestClientUnconfigured, StreamInitiallyBlockedThenUnblocked) {
  if (!connection_->version().AllowsLowFlowControlLimits()) {
    return;
  }
  // Create a stream before negotiating the config and verify it starts off
  // blocked.
  QuicSessionPeer::SetMaxOpenOutgoingBidirectionalStreams(&session_, 10);
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_TRUE(stream2->flow_controller()->IsBlocked());
  EXPECT_TRUE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());

  // Negotiate the config with higher received limits.
  QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
      session_.config(), kMinimumFlowControlSendWindow);
  QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
      session_.config(), kMinimumFlowControlSendWindow);
  session_.OnConfigNegotiated();

  // Stream is now unblocked.
  EXPECT_FALSE(stream2->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

}  // namespace
}  // namespace test
}  // namespace quic
