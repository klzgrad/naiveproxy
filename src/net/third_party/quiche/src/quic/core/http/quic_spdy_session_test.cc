// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_streams_blocked_frame.h"
#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_encoder_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_header_table_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

using spdy::kV3HighestPriority;
using spdy::Spdy3PriorityToHttp2Weight;
using spdy::SpdyFramer;
using spdy::SpdyHeaderBlock;
using spdy::SpdyPriority;
using spdy::SpdyPriorityIR;
using spdy::SpdySerializedFrame;
using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

bool VerifyAndClearStopSendingFrame(const QuicFrame& frame) {
  EXPECT_EQ(STOP_SENDING_FRAME, frame.type);
  return ClearControlFrame(frame);
}

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
    session()->OnNewEncryptionKeyAvailable(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullEncrypter>(session()->perspective()));
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
  HandshakeState GetHandshakeState() const override {
    return one_rtt_keys_available() ? HANDSHAKE_COMPLETE : HANDSHAKE_START;
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

  MOCK_METHOD0(OnCanWrite, void());

  bool HasPendingCryptoRetransmission() const override { return false; }

  MOCK_CONST_METHOD0(HasPendingRetransmission, bool());

 private:
  using QuicCryptoStream::session;

  bool encryption_established_;
  bool one_rtt_keys_available_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
};

class TestHeadersStream : public QuicHeadersStream {
 public:
  explicit TestHeadersStream(QuicSpdySession* session)
      : QuicHeadersStream(session) {}

  MOCK_METHOD0(OnCanWrite, void());
};

class TestStream : public QuicSpdyStream {
 public:
  TestStream(QuicStreamId id, QuicSpdySession* session, StreamType type)
      : QuicSpdyStream(id, session, type) {}

  TestStream(PendingStream* pending, QuicSpdySession* session, StreamType type)
      : QuicSpdyStream(pending, session, type) {}

  using QuicStream::CloseWriteSide;

  void OnBodyAvailable() override {}

  MOCK_METHOD0(OnCanWrite, void());
  MOCK_METHOD4(RetransmitStreamData,
               bool(QuicStreamOffset, QuicByteCount, bool, TransmissionType));

  MOCK_CONST_METHOD0(HasPendingRetransmission, bool());
};

class TestSession : public QuicSpdySession {
 public:
  explicit TestSession(QuicConnection* connection)
      : QuicSpdySession(connection,
                        nullptr,
                        DefaultQuicConfig(),
                        CurrentSupportedVersions()),
        crypto_stream_(this),
        writev_consumes_all_data_(false) {
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

  TestStream* CreateOutgoingBidirectionalStream() override {
    TestStream* stream = new TestStream(GetNextOutgoingBidirectionalStreamId(),
                                        this, BIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  TestStream* CreateOutgoingUnidirectionalStream() override {
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
      connection()->CloseConnection(
          QUIC_TOO_MANY_OPEN_STREAMS, "Too many streams!",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return nullptr;
    } else {
      TestStream* stream = new TestStream(
          id, this,
          DetermineStreamType(id, connection()->transport_version(),
                              perspective(), /*is_incoming=*/true,
                              BIDIRECTIONAL));
      ActivateStream(QuicWrapUnique(stream));
      return stream;
    }
  }

  TestStream* CreateIncomingStream(PendingStream* pending) override {
    QuicStreamId id = pending->id();
    TestStream* stream =
        new TestStream(pending, this,
                       DetermineStreamType(
                           id, connection()->transport_version(), perspective(),
                           /*is_incoming=*/true, BIDIRECTIONAL));
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }

  bool ShouldCreateIncomingStream(QuicStreamId /*id*/) override { return true; }

  bool ShouldCreateOutgoingBidirectionalStream() override { return true; }
  bool ShouldCreateOutgoingUnidirectionalStream() override { return true; }

  bool IsClosedStream(QuicStreamId id) {
    return QuicSession::IsClosedStream(id);
  }

  QuicStream* GetOrCreateStream(QuicStreamId stream_id) {
    return QuicSpdySession::GetOrCreateStream(stream_id);
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

  void set_writev_consumes_all_data(bool val) {
    writev_consumes_all_data_ = val;
  }

  QuicConsumedData SendStreamData(QuicStream* stream) {
    struct iovec iov;
    if (!QuicUtils::IsCryptoStreamId(connection()->transport_version(),
                                     stream->id()) &&
        connection()->encryption_level() != ENCRYPTION_FORWARD_SECURE) {
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

  QuicConsumedData SendLargeFakeData(QuicStream* stream, int bytes) {
    DCHECK(writev_consumes_all_data_);
    return WritevData(stream->id(), bytes, 0, FIN, NOT_RETRANSMISSION,
                      QuicheNullOpt);
  }

  using QuicSession::closed_streams;
  using QuicSession::ShouldKeepConnectionAlive;
  using QuicSession::zombie_streams;
  using QuicSpdySession::ProcessPendingStream;
  using QuicSpdySession::UsesPendingStreams;

 private:
  StrictMock<TestCryptoStream> crypto_stream_;

  bool writev_consumes_all_data_;
};

class QuicSpdySessionTestBase : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  bool ClearMaxStreamsControlFrame(const QuicFrame& frame) {
    if (frame.type == MAX_STREAMS_FRAME) {
      DeleteFrame(&const_cast<QuicFrame&>(frame));
      return true;
    }
    return false;
  }

 protected:
  explicit QuicSpdySessionTestBase(Perspective perspective)
      : connection_(
            new StrictMock<MockQuicConnection>(&helper_,
                                               &alarm_factory_,
                                               perspective,
                                               SupportedVersions(GetParam()))),
        session_(connection_) {
    session_.config()->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    session_.config()->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);
    if (VersionUsesHttp3(transport_version())) {
      QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(
          session_.config(),
          session_.num_expected_unidirectional_static_streams());
    }
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    session_.OnConfigNegotiated();
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
    TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .Times(testing::AnyNumber());
    writer_ = static_cast<MockPacketWriter*>(
        QuicConnectionPeer::GetWriter(session_.connection()));
  }

  void CheckClosedStreams() {
    QuicStreamId first_stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        transport_version(), Perspective::IS_CLIENT);
    if (!QuicVersionUsesCryptoFrames(transport_version())) {
      first_stream_id = QuicUtils::GetCryptoStreamId(transport_version());
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
    if (!VersionHasIetfQuicFrames(transport_version())) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
    } else {
      // V99 has two frames, RST_STREAM and STOP_SENDING
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .Times(2)
          .WillRepeatedly(Invoke(&ClearControlFrame));
    }
    EXPECT_CALL(*connection_, OnStreamReset(id, _));
    session_.CloseStream(id);
    closed_streams_.insert(id);
  }

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return GetNthClientInitiatedBidirectionalStreamId(transport_version(), n);
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return GetNthServerInitiatedBidirectionalStreamId(transport_version(), n);
  }

  QuicStreamId IdDelta() {
    return QuicUtils::StreamIdDelta(transport_version());
  }

  std::string EncodeSettings(const SettingsFrame& settings) {
    std::unique_ptr<char[]> buffer;
    auto header_length = HttpEncoder::SerializeSettingsFrame(settings, &buffer);
    return std::string(buffer.get(), header_length);
  }

  std::string SerializePriorityUpdateFrame(
      const PriorityUpdateFrame& priority_update) {
    std::unique_ptr<char[]> priority_buffer;
    QuicByteCount priority_frame_length =
        HttpEncoder::SerializePriorityUpdateFrame(priority_update,
                                                  &priority_buffer);
    return std::string(priority_buffer.get(), priority_frame_length);
  }

  QuicStreamId StreamCountToId(QuicStreamCount stream_count,
                               Perspective perspective,
                               bool bidirectional) {
    // Calculate and build up stream ID rather than use
    // GetFirst... because the test that relies on this method
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

  void CompleteHandshake() {
    if (VersionHasIetfQuicFrames(transport_version())) {
      EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
          .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
    }
    // HANDSHAKE_DONE frame sent by the server.
    if (connection_->version().HasHandshakeDone() &&
        connection_->perspective() == Perspective::IS_SERVER) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
    }

    CryptoHandshakeMessage message;
    session_.GetMutableCryptoStream()->OnHandshakeMessage(message);
    testing::Mock::VerifyAndClearExpectations(writer_);
    testing::Mock::VerifyAndClearExpectations(connection_);
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  TestSession session_;
  std::set<QuicStreamId> closed_streams_;
  MockPacketWriter* writer_;
};

class QuicSpdySessionTestServer : public QuicSpdySessionTestBase {
 protected:
  QuicSpdySessionTestServer()
      : QuicSpdySessionTestBase(Perspective::IS_SERVER) {}
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdySessionTestServer,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSpdySessionTestServer, UsesPendingStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  EXPECT_TRUE(session_.UsesPendingStreams());
}

TEST_P(QuicSpdySessionTestServer, PeerAddress) {
  EXPECT_EQ(QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort),
            session_.peer_address());
}

TEST_P(QuicSpdySessionTestServer, SelfAddress) {
  EXPECT_TRUE(session_.self_address().IsInitialized());
}

TEST_P(QuicSpdySessionTestServer, OneRttKeysAvailable) {
  EXPECT_FALSE(session_.OneRttKeysAvailable());
  CompleteHandshake();
  EXPECT_TRUE(session_.OneRttKeysAvailable());
}

TEST_P(QuicSpdySessionTestServer, IsClosedStreamDefault) {
  // Ensure that no streams are initially closed.
  QuicStreamId first_stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    first_stream_id = QuicUtils::GetCryptoStreamId(transport_version());
  }
  for (QuicStreamId i = first_stream_id; i < 100; i++) {
    EXPECT_FALSE(session_.IsClosedStream(i)) << "stream id: " << i;
  }
}

TEST_P(QuicSpdySessionTestServer, AvailableStreams) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(2)) != nullptr);
  // Both client initiated streams with smaller stream IDs are available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(0)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(1)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(1)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthClientInitiatedBidirectionalId(0)) != nullptr);
}

TEST_P(QuicSpdySessionTestServer, IsClosedStreamLocallyCreated) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedBidirectionalId(0), stream2->id());
  QuicSpdyStream* stream4 = session_.CreateOutgoingBidirectionalStream();
  EXPECT_EQ(GetNthServerInitiatedBidirectionalId(1), stream4->id());

  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedBidirectionalId(0));
  CheckClosedStreams();
  CloseStream(GetNthServerInitiatedBidirectionalId(1));
  CheckClosedStreams();
}

TEST_P(QuicSpdySessionTestServer, IsClosedStreamPeerCreated) {
  QuicStreamId stream_id1 = GetNthClientInitiatedBidirectionalId(0);
  QuicStreamId stream_id2 = GetNthClientInitiatedBidirectionalId(1);
  session_.GetOrCreateStream(stream_id1);
  session_.GetOrCreateStream(stream_id2);

  CheckClosedStreams();
  CloseStream(stream_id1);
  CheckClosedStreams();
  CloseStream(stream_id2);
  // Create a stream, and make another available.
  QuicStream* stream3 = session_.GetOrCreateStream(stream_id2 + 4);
  CheckClosedStreams();
  // Close one, but make sure the other is still not closed
  CloseStream(stream3->id());
  CheckClosedStreams();
}

TEST_P(QuicSpdySessionTestServer, MaximumAvailableOpenedStreams) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // For IETF QUIC, we should be able to obtain the max allowed
    // stream ID, the next ID should fail. Since the actual limit
    // is not the number of open streams, we allocate the max and the max+2.
    // Get the max allowed stream ID, this should succeed.
    QuicStreamId stream_id = StreamCountToId(
        QuicSessionPeer::v99_streamid_manager(&session_)
            ->max_incoming_bidirectional_streams(),
        Perspective::IS_CLIENT,  // Client initates stream, allocs stream id.
        /*bidirectional=*/true);
    EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));
    stream_id = StreamCountToId(QuicSessionPeer::v99_streamid_manager(&session_)
                                    ->max_incoming_unidirectional_streams(),
                                Perspective::IS_CLIENT,
                                /*bidirectional=*/false);
    EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id));
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(2);
    // Get the (max allowed stream ID)++. These should all fail.
    stream_id = StreamCountToId(QuicSessionPeer::v99_streamid_manager(&session_)
                                        ->max_incoming_bidirectional_streams() +
                                    1,
                                Perspective::IS_CLIENT,
                                /*bidirectional=*/true);
    EXPECT_EQ(nullptr, session_.GetOrCreateStream(stream_id));

    stream_id =
        StreamCountToId(QuicSessionPeer::v99_streamid_manager(&session_)
                                ->max_incoming_unidirectional_streams() +
                            1,
                        Perspective::IS_CLIENT,
                        /*bidirectional=*/false);
    EXPECT_EQ(nullptr, session_.GetOrCreateStream(stream_id));
  } else {
    QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
    session_.GetOrCreateStream(stream_id);
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
    EXPECT_NE(
        nullptr,
        session_.GetOrCreateStream(
            stream_id +
            IdDelta() *
                (session_.max_open_incoming_bidirectional_streams() - 1)));
  }
}

TEST_P(QuicSpdySessionTestServer, TooManyAvailableStreams) {
  QuicStreamId stream_id1 = GetNthClientInitiatedBidirectionalId(0);
  QuicStreamId stream_id2;
  EXPECT_NE(nullptr, session_.GetOrCreateStream(stream_id1));
  // A stream ID which is too large to create.
  stream_id2 = GetNthClientInitiatedBidirectionalId(
      2 * session_.MaxAvailableBidirectionalStreams() + 4);
  if (VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID, _, _));
  } else {
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_TOO_MANY_AVAILABLE_STREAMS, _, _));
  }
  EXPECT_EQ(nullptr, session_.GetOrCreateStream(stream_id2));
}

TEST_P(QuicSpdySessionTestServer, ManyAvailableStreams) {
  // When max_open_streams_ is 200, should be able to create 200 streams
  // out-of-order, that is, creating the one with the largest stream ID first.
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_, 200);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, 200);
  }
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  // Create one stream.
  session_.GetOrCreateStream(stream_id);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  // Stream count is 200, GetNth... starts counting at 0, so the 200'th stream
  // is 199. BUT actually we need to do 198 because the crypto stream (Stream
  // ID 0) has not been registered, but GetNth... assumes that it has.
  EXPECT_NE(nullptr, session_.GetOrCreateStream(
                         GetNthClientInitiatedBidirectionalId(198)));
}

TEST_P(QuicSpdySessionTestServer,
       DebugDFatalIfMarkingClosedStreamWriteBlocked) {
  CompleteHandshake();
  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillRepeatedly(Return(WriteResult(WRITE_STATUS_OK, 0)));

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

TEST_P(QuicSpdySessionTestServer, OnCanWrite) {
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

TEST_P(QuicSpdySessionTestServer, TooLargeStreamBlocked) {
  // STREAMS_BLOCKED frame is IETF QUIC only.
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  CompleteHandshake();
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  // Simualte the situation where the incoming stream count is at its limit and
  // the peer is blocked.
  QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(
      static_cast<QuicSession*>(&session_), QuicUtils::GetMaxStreamCount());
  QuicStreamsBlockedFrame frame;
  frame.stream_count = QuicUtils::GetMaxStreamCount();
  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(_));
  session_.OnStreamsBlockedFrame(frame);
}

TEST_P(QuicSpdySessionTestServer, TestBatchedWrites) {
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

TEST_P(QuicSpdySessionTestServer, OnCanWriteBundlesStreams) {
  // Encryption needs to be established before data can be sent.
  CompleteHandshake();

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
  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(*send_algorithm, OnPacketSent(_, _, _, _, _));
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));
  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSpdySessionTestServer, OnCanWriteCongestionControlBlocks) {
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

TEST_P(QuicSpdySessionTestServer, OnCanWriteWriterBlocks) {
  // Drive congestion control manually in order to ensure that
  // application-limited signaling is handled correctly.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));

  // Drive packet writer manually.
  EXPECT_CALL(*writer_, IsWriteBlocked()).WillRepeatedly(Return(true));
  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _)).Times(0);

  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();

  session_.MarkConnectionLevelWriteBlocked(stream2->id());

  EXPECT_CALL(*stream2, OnCanWrite()).Times(0);
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_)).Times(0);

  session_.OnCanWrite();
  EXPECT_TRUE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSpdySessionTestServer, BufferedHandshake) {
  // This tests prioritization of the crypto stream when flow control limits are
  // reached. When CRYPTO frames are in use, there is no flow control for the
  // crypto handshake, so this test is irrelevant.
  if (QuicVersionUsesCryptoFrames(transport_version())) {
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
      QuicUtils::GetCryptoStreamId(transport_version()));
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

TEST_P(QuicSpdySessionTestServer, OnCanWriteWithClosedStream) {
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

TEST_P(QuicSpdySessionTestServer,
       OnCanWriteLimitsNumWritesIfFlowControlBlocked) {
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
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    session_.MarkConnectionLevelWriteBlocked(
        QuicUtils::GetCryptoStreamId(transport_version()));
  }

  // Create a data stream, and although it is write blocked we never expect it
  // to be allowed to write as we are connection level flow control blocked.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_CALL(*stream, OnCanWrite()).Times(0);

  // The crypto and headers streams should be called even though we are
  // connection flow control blocked.
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
    EXPECT_CALL(*crypto_stream, OnCanWrite());
  }

  if (!VersionUsesHttp3(transport_version())) {
    TestHeadersStream* headers_stream;
    QuicSpdySessionPeer::SetHeadersStream(&session_, nullptr);
    headers_stream = new TestHeadersStream(&session_);
    QuicSpdySessionPeer::SetHeadersStream(&session_, headers_stream);
    session_.MarkConnectionLevelWriteBlocked(
        QuicUtils::GetHeadersStreamId(transport_version()));
    EXPECT_CALL(*headers_stream, OnCanWrite());
  }

  // After the crypto and header streams perform a write, the connection will be
  // blocked by the flow control, hence it should become application-limited.
  EXPECT_CALL(*send_algorithm, OnApplicationLimited(_));

  session_.OnCanWrite();
  EXPECT_FALSE(session_.WillingAndAbleToWrite());
}

TEST_P(QuicSpdySessionTestServer, SendGoAway) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // HTTP/3 GOAWAY has different semantic and thus has its own test.
    return;
  }
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
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

TEST_P(QuicSpdySessionTestServer, SendHttp3GoAway) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  CompleteHandshake();
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(_));
  session_.SendHttp3GoAway();
  EXPECT_TRUE(session_.http3_goaway_sent());

  const QuicStreamId kTestStreamId =
      GetNthClientInitiatedBidirectionalStreamId(transport_version(), 0);
  EXPECT_CALL(*connection_, OnStreamReset(kTestStreamId, _)).Times(0);
  EXPECT_TRUE(session_.GetOrCreateStream(kTestStreamId));
}

TEST_P(QuicSpdySessionTestServer, DoNotSendGoAwayTwice) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // HTTP/3 GOAWAY doesn't have such restriction.
    return;
  }
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  session_.SendGoAway(QUIC_PEER_GOING_AWAY, "Going Away.");
  EXPECT_TRUE(session_.goaway_sent());
  session_.SendGoAway(QUIC_PEER_GOING_AWAY, "Going Away.");
}

TEST_P(QuicSpdySessionTestServer, InvalidGoAway) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    // HTTP/3 GOAWAY has different semantics and thus has its own test.
    return;
  }
  QuicGoAwayFrame go_away(kInvalidControlFrameId, QUIC_PEER_GOING_AWAY,
                          session_.next_outgoing_bidirectional_stream_id(), "");
  session_.OnGoAway(go_away);
}

// Test that server session will send a connectivity probe in response to a
// connectivity probe on the same path.
TEST_P(QuicSpdySessionTestServer, ServerReplyToConnecitivityProbe) {
  QuicSocketAddress old_peer_address =
      QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort);
  EXPECT_EQ(old_peer_address, session_.peer_address());

  QuicSocketAddress new_peer_address =
      QuicSocketAddress(QuicIpAddress::Loopback4(), kTestPort + 1);

  EXPECT_CALL(*connection_,
              SendConnectivityProbingResponsePacket(new_peer_address));
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Need to explicitly do this to emulate the reception of a PathChallenge,
    // which stores its payload for use in generating the response.
    connection_->OnPathChallengeFrame(
        QuicPathChallengeFrame(0, {{0, 1, 2, 3, 4, 5, 6, 7}}));
  }
  session_.OnPacketReceived(session_.self_address(), new_peer_address,
                            /*is_connectivity_probe=*/true);
  EXPECT_EQ(old_peer_address, session_.peer_address());
}

TEST_P(QuicSpdySessionTestServer, IncreasedTimeoutAfterCryptoHandshake) {
  EXPECT_EQ(kInitialIdleTimeoutSecs + 3,
            QuicConnectionPeer::GetNetworkTimeout(connection_).ToSeconds());
  CompleteHandshake();
  EXPECT_EQ(kMaximumIdleTimeoutSecs + 3,
            QuicConnectionPeer::GetNetworkTimeout(connection_).ToSeconds());
}

TEST_P(QuicSpdySessionTestServer, RstStreamBeforeHeadersDecompressed) {
  CompleteHandshake();
  // Send two bytes of payload.
  QuicStreamFrame data1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(1u, session_.GetNumOpenIncomingStreams());

  if (!VersionHasIetfQuicFrames(transport_version())) {
    // For version99, OnStreamReset gets called because of the STOP_SENDING,
    // below. EXPECT the call there.
    EXPECT_CALL(*connection_,
                OnStreamReset(GetNthClientInitiatedBidirectionalId(0), _));
  }

  // In HTTP/3, Qpack stream will send data on stream reset and cause packet to
  // be flushed.
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  }
  EXPECT_CALL(*connection_, SendControlFrame(_));
  QuicRstStreamFrame rst1(kInvalidControlFrameId,
                          GetNthClientInitiatedBidirectionalId(0),
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  session_.OnRstStream(rst1);

  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes a
  // one-way close.
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Only needed for version 99/IETF QUIC.
    QuicStopSendingFrame stop_sending(
        kInvalidControlFrameId, GetNthClientInitiatedBidirectionalId(0),
        static_cast<QuicApplicationErrorCode>(QUIC_ERROR_PROCESSING_STREAM));
    // Expect the RESET_STREAM that is generated in response to receiving a
    // STOP_SENDING.
    EXPECT_CALL(*connection_,
                OnStreamReset(GetNthClientInitiatedBidirectionalId(0),
                              QUIC_ERROR_PROCESSING_STREAM));
    session_.OnStopSendingFrame(stop_sending);
  }

  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
  // Connection should remain alive.
  EXPECT_TRUE(connection_->connected());
}

TEST_P(QuicSpdySessionTestServer, OnStreamFrameFinStaticStreamId) {
  QuicStreamId id;
  // Initialize HTTP/3 control stream.
  if (VersionUsesHttp3(transport_version())) {
    id = GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
    char type[] = {kControlStream};

    QuicStreamFrame data1(id, false, 0, quiche::QuicheStringPiece(type, 1));
    session_.OnStreamFrame(data1);
  } else {
    id = QuicUtils::GetHeadersStreamId(transport_version());
  }

  // Send two bytes of payload.
  QuicStreamFrame data1(id, true, 0, quiche::QuicheStringPiece("HT"));
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Attempt to close a static stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSpdySessionTestServer, OnRstStreamStaticStreamId) {
  QuicStreamId id;
  QuicErrorCode expected_error;
  std::string error_message;
  // Initialize HTTP/3 control stream.
  if (VersionUsesHttp3(transport_version())) {
    id = GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
    char type[] = {kControlStream};

    QuicStreamFrame data1(id, false, 0, quiche::QuicheStringPiece(type, 1));
    session_.OnStreamFrame(data1);
    expected_error = QUIC_HTTP_CLOSED_CRITICAL_STREAM;
    error_message = "RESET_STREAM received for receive control stream";
  } else {
    id = QuicUtils::GetHeadersStreamId(transport_version());
    expected_error = QUIC_INVALID_STREAM_ID;
    error_message = "Attempt to reset headers stream";
  }

  // Send two bytes of payload.
  QuicRstStreamFrame rst1(kInvalidControlFrameId, id,
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(
      *connection_,
      CloseConnection(expected_error, error_message,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnRstStream(rst1);
}

TEST_P(QuicSpdySessionTestServer, OnStreamFrameInvalidStreamId) {
  // Send two bytes of payload.
  QuicStreamFrame data1(QuicUtils::GetInvalidStreamId(transport_version()),
                        true, 0, quiche::QuicheStringPiece("HT"));
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSpdySessionTestServer, OnRstStreamInvalidStreamId) {
  // Send two bytes of payload.
  QuicRstStreamFrame rst1(kInvalidControlFrameId,
                          QuicUtils::GetInvalidStreamId(transport_version()),
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(*connection_,
              CloseConnection(
                  QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnRstStream(rst1);
}

TEST_P(QuicSpdySessionTestServer, HandshakeUnblocksFlowControlBlockedStream) {
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
  stream2->WriteOrBufferBody(body, false);
  EXPECT_TRUE(stream2->flow_controller()->IsBlocked());
  EXPECT_TRUE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());

  // Now complete the crypto handshake, resulting in an increased flow control
  // send window.
  CompleteHandshake();
  EXPECT_TRUE(QuicSessionPeer::IsStreamWriteBlocked(&session_, stream2->id()));
  // Stream is now unblocked.
  EXPECT_FALSE(stream2->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

TEST_P(QuicSpdySessionTestServer,
       HandshakeUnblocksFlowControlBlockedCryptoStream) {
  if (QuicVersionUsesCryptoFrames(transport_version())) {
    // QUIC version 47 onwards uses CRYPTO frames for the handshake, so this
    // test doesn't make sense for those versions.
    return;
  }
  // Test that if the crypto stream is flow control blocked, then if the SHLO
  // contains a larger send window offset, the stream becomes unblocked.
  session_.set_writev_consumes_all_data(true);
  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(&session_);
  EXPECT_FALSE(headers_stream->flow_controller()->IsBlocked());
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
  EXPECT_FALSE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());
  EXPECT_FALSE(session_.HasDataToWrite());
  EXPECT_TRUE(crypto_stream->HasBufferedData());

  // Now complete the crypto handshake, resulting in an increased flow control
  // send window.
  CompleteHandshake();
  EXPECT_TRUE(QuicSessionPeer::IsStreamWriteBlocked(
      &session_, QuicUtils::GetCryptoStreamId(transport_version())));
  // Stream is now unblocked and will no longer have buffered data.
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

#if !defined(OS_IOS)
// This test is failing flakily for iOS bots.
// http://crbug.com/425050
// NOTE: It's not possible to use the standard MAYBE_ convention to disable
// this test on iOS because when this test gets instantiated it ends up with
// various names that are dependent on the parameters passed.
TEST_P(QuicSpdySessionTestServer,
       HandshakeUnblocksFlowControlBlockedHeadersStream) {
  // This test depends on stream-level flow control for the crypto stream, which
  // doesn't exist when CRYPTO frames are used.
  if (QuicVersionUsesCryptoFrames(transport_version())) {
    return;
  }

  // This test depends on the headers stream, which does not exist when QPACK is
  // used.
  if (VersionUsesHttp3(transport_version())) {
    return;
  }

  // Test that if the header stream is flow control blocked, then if the SHLO
  // contains a larger send window offset, the stream becomes unblocked.
  session_.set_writev_consumes_all_data(true);
  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(&session_);
  EXPECT_FALSE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  QuicStreamId stream_id = 5;
  // Write until the header stream is flow control blocked.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  SpdyHeaderBlock headers;
  SimpleRandom random;
  while (!headers_stream->flow_controller()->IsBlocked() && stream_id < 2000) {
    EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
    EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
    headers["header"] = quiche::QuicheStrCat(
        random.RandUint64(), random.RandUint64(), random.RandUint64());
    session_.WriteHeadersOnHeadersStream(stream_id, headers.Clone(), true,
                                         spdy::SpdyStreamPrecedence(0),
                                         nullptr);
    stream_id += IdDelta();
  }
  // Write once more to ensure that the headers stream has buffered data. The
  // random headers may have exactly filled the flow control window.
  session_.WriteHeadersOnHeadersStream(stream_id, std::move(headers), true,
                                       spdy::SpdyStreamPrecedence(0), nullptr);
  EXPECT_TRUE(headers_stream->HasBufferedData());

  EXPECT_TRUE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(crypto_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());
  EXPECT_FALSE(session_.HasDataToWrite());

  // Now complete the crypto handshake, resulting in an increased flow control
  // send window.
  CompleteHandshake();

  // Stream is now unblocked and will no longer have buffered data.
  EXPECT_FALSE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
  EXPECT_TRUE(headers_stream->HasBufferedData());
  EXPECT_TRUE(QuicSessionPeer::IsStreamWriteBlocked(
      &session_, QuicUtils::GetHeadersStreamId(transport_version())));
}
#endif  // !defined(OS_IOS)

TEST_P(QuicSpdySessionTestServer,
       ConnectionFlowControlAccountingRstOutOfOrder) {
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  CompleteHandshake();
  // Test that when we receive an out of order stream RST we correctly adjust
  // our connection level flow control receive window.
  // On close, the stream should mark as consumed all bytes between the highest
  // byte consumed so far and the final byte offset from the RST frame.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();

  const QuicStreamOffset kByteOffset =
      1 + kInitialSessionFlowControlWindowForTest / 2;

  if (!VersionHasIetfQuicFrames(transport_version())) {
    // For version99 the call to OnStreamReset happens as a result of receiving
    // the STOP_SENDING, so set up the EXPECT there.
    EXPECT_CALL(*connection_, OnStreamReset(stream->id(), _));
    EXPECT_CALL(*connection_, SendControlFrame(_));
  } else {
    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  }
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream->id(),
                               QUIC_STREAM_CANCELLED, kByteOffset);
  session_.OnRstStream(rst_frame);
  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes a
  // one-way close.
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Only needed for version 99/IETF QUIC.
    QuicStopSendingFrame stop_sending(
        kInvalidControlFrameId, stream->id(),
        static_cast<QuicApplicationErrorCode>(QUIC_STREAM_CANCELLED));
    // Expect the RESET_STREAM that is generated in response to receiving a
    // STOP_SENDING.
    EXPECT_CALL(*connection_,
                OnStreamReset(stream->id(), QUIC_STREAM_CANCELLED));
    EXPECT_CALL(*connection_, SendControlFrame(_));
    session_.OnStopSendingFrame(stop_sending);
  }

  EXPECT_EQ(kByteOffset, session_.flow_controller()->bytes_consumed());
}

TEST_P(QuicSpdySessionTestServer,
       ConnectionFlowControlAccountingFinAndLocalReset) {
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

TEST_P(QuicSpdySessionTestServer, ConnectionFlowControlAccountingFinAfterRst) {
  CompleteHandshake();
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillRepeatedly(Invoke(&ClearControlFrame));
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
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  }
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

TEST_P(QuicSpdySessionTestServer, ConnectionFlowControlAccountingRstAfterRst) {
  CompleteHandshake();
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
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  }
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

TEST_P(QuicSpdySessionTestServer, InvalidStreamFlowControlWindowInHandshake) {
  if (GetParam().handshake_protocol == PROTOCOL_TLS1_3) {
    // IETF Quic doesn't require a minimum flow control window.
    return;
  }
  // Test that receipt of an invalid (< default) stream flow control window from
  // the peer results in the connection being torn down.
  const uint32_t kInvalidWindow = kMinimumFlowControlSendWindow - 1;
  QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(session_.config(),
                                                            kInvalidWindow);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_INVALID_WINDOW, _, _));
  session_.OnConfigNegotiated();
}

TEST_P(QuicSpdySessionTestServer, InvalidSessionFlowControlWindowInHandshake) {
  if (GetParam().handshake_protocol == PROTOCOL_TLS1_3) {
    // IETF Quic doesn't require a minimum flow control window.
    return;
  }
  // Test that receipt of an invalid (< default) session flow control window
  // from the peer results in the connection being torn down.
  const uint32_t kInvalidWindow = kMinimumFlowControlSendWindow - 1;
  QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(session_.config(),
                                                             kInvalidWindow);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_INVALID_WINDOW, _, _));
  session_.OnConfigNegotiated();
}

TEST_P(QuicSpdySessionTestServer, TooLowUnidirectionalStreamLimitHttp3) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(session_.config(), 2u);

  EXPECT_CALL(
      *connection_,
      CloseConnection(_, "New unidirectional stream limit is too low.", _));
  session_.OnConfigNegotiated();
}

// Test negotiation of custom server initial flow control window.
TEST_P(QuicSpdySessionTestServer, CustomFlowControlWindow) {
  QuicTagVector copt;
  copt.push_back(kIFW7);
  QuicConfigPeer::SetReceivedConnectionOptions(session_.config(), copt);

  session_.OnConfigNegotiated();
  EXPECT_EQ(192 * 1024u, QuicFlowControllerPeer::ReceiveWindowSize(
                             session_.flow_controller()));
}

TEST_P(QuicSpdySessionTestServer, FlowControlWithInvalidFinalOffset) {
  CompleteHandshake();
  // Test that if we receive a stream RST with a highest byte offset that
  // violates flow control, that we close the connection.
  const uint64_t kLargeOffset = kInitialSessionFlowControlWindowForTest + 1;
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA, _, _))
      .Times(2);

  // Check that stream frame + FIN results in connection close.
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  }
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

TEST_P(QuicSpdySessionTestServer, WindowUpdateUnblocksHeadersStream) {
  if (VersionUsesHttp3(transport_version())) {
    // The test relies on headers stream, which no longer exists in IETF QUIC.
    return;
  }

  // Test that a flow control blocked headers stream gets unblocked on recipt of
  // a WINDOW_UPDATE frame.

  // Set the headers stream to be flow control blocked.
  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(&session_);
  QuicFlowControllerPeer::SetSendWindowOffset(headers_stream->flow_controller(),
                                              0);
  EXPECT_TRUE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_TRUE(session_.IsStreamFlowControlBlocked());

  // Unblock the headers stream by supplying a WINDOW_UPDATE.
  QuicWindowUpdateFrame window_update_frame(kInvalidControlFrameId,
                                            headers_stream->id(),
                                            2 * kMinimumFlowControlSendWindow);
  session_.OnWindowUpdateFrame(window_update_frame);
  EXPECT_FALSE(headers_stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(session_.IsConnectionFlowControlBlocked());
  EXPECT_FALSE(session_.IsStreamFlowControlBlocked());
}

TEST_P(QuicSpdySessionTestServer,
       TooManyUnfinishedStreamsCauseServerRejectStream) {
  // If a buggy/malicious peer creates too many streams that are not ended
  // with a FIN or RST then we send an RST to refuse streams for versions other
  // than version 99. In version 99 the connection gets closed.
  const QuicStreamId kMaxStreams = 5;
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(&session_,
                                                            kMaxStreams);
  } else {
    QuicSessionPeer::SetMaxOpenIncomingStreams(&session_, kMaxStreams);
  }
  // GetNth assumes that both the crypto and header streams have been
  // open, but the stream id manager, using GetFirstBidirectional... only
  // assumes that the crypto stream is open. This means that GetNth...(0)
  // Will return stream ID == 8 (with id ==0 for crypto and id==4 for headers).
  // It also means that GetNth(kMax..=5) returns 28 (streams 0/1/2/3/4 are ids
  // 8, 12, 16, 20, 24, respectively, so stream#5 is stream id 28).
  // However, the stream ID manager does not assume stream 4 is for headers.
  // The ID manager would assume that stream#5 is streamid 24.
  // In order to make this all work out properly, kFinalStreamId will
  // be set to GetNth...(kMaxStreams-1)... but only for V99
  const QuicStreamId kFirstStreamId = GetNthClientInitiatedBidirectionalId(0);
  const QuicStreamId kFinalStreamId =
      GetNthClientInitiatedBidirectionalId(kMaxStreams);
  // Create kMaxStreams data streams, and close them all without receiving a
  // FIN or a RST_STREAM from the client.
  const QuicStreamId kNextId = QuicUtils::StreamIdDelta(transport_version());
  for (QuicStreamId i = kFirstStreamId; i < kFinalStreamId; i += kNextId) {
    QuicStreamFrame data1(i, false, 0, quiche::QuicheStringPiece("HT"));
    session_.OnStreamFrame(data1);
    // EXPECT_EQ(1u, session_.GetNumOpenStreams());
    if (!VersionHasIetfQuicFrames(transport_version())) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
    } else {
      // V99 has two frames, RST_STREAM and STOP_SENDING
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .Times(2)
          .WillRepeatedly(Invoke(&ClearControlFrame));
    }
    // Close the stream only if not version 99. If we are version 99
    // then closing the stream opens up the available stream id space,
    // so we never bump into the limit.
    EXPECT_CALL(*connection_, OnStreamReset(i, _));
    session_.CloseStream(i);
  }
  // Try and open a stream that exceeds the limit.
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // On versions other than 99, opening such a stream results in a
    // RST_STREAM.
    EXPECT_CALL(*connection_, SendControlFrame(_)).Times(1);
    EXPECT_CALL(*connection_,
                OnStreamReset(kFinalStreamId, QUIC_REFUSED_STREAM))
        .Times(1);
  } else {
    // On version 99 opening such a stream results in a connection close.
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INVALID_STREAM_ID,
                        testing::MatchesRegex(
                            "Stream id \\d+ would exceed stream count limit 5"),
                        _));
  }
  // Create one more data streams to exceed limit of open stream.
  QuicStreamFrame data1(kFinalStreamId, false, 0,
                        quiche::QuicheStringPiece("HT"));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSpdySessionTestServer, DrainingStreamsDoNotCountAsOpened) {
  // Verify that a draining stream (which has received a FIN but not consumed
  // it) does not count against the open quota (because it is closed from the
  // protocol point of view).
  if (VersionHasIetfQuicFrames(transport_version())) {
    // Simulate receiving a config. so that MAX_STREAMS/etc frames may
    // be transmitted
    QuicSessionPeer::set_is_configured(&session_, true);
    // Version 99 will result in a MAX_STREAMS frame as streams are consumed
    // (via the OnStreamFrame call) and then released (via
    // StreamDraining). Eventually this node will believe that the peer is
    // running low on available stream ids and then send a MAX_STREAMS frame,
    // caught by this EXPECT_CALL.
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
      GetNthClientInitiatedBidirectionalId(kMaxStreams + 1);
  for (QuicStreamId i = kFirstStreamId; i < kFinalStreamId; i += IdDelta()) {
    QuicStreamFrame data1(i, true, 0, quiche::QuicheStringPiece("HT"));
    session_.OnStreamFrame(data1);
    EXPECT_EQ(1u, session_.GetNumOpenIncomingStreams());
    session_.StreamDraining(i);
    EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
  }
}

class QuicSpdySessionTestClient : public QuicSpdySessionTestBase {
 protected:
  QuicSpdySessionTestClient()
      : QuicSpdySessionTestBase(Perspective::IS_CLIENT) {}
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdySessionTestClient,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSpdySessionTestClient, UsesPendingStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  EXPECT_TRUE(session_.UsesPendingStreams());
}

// Regression test for crbug.com/977581.
TEST_P(QuicSpdySessionTestClient, BadStreamFramePendingStream) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());
  QuicStreamId stream_id1 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);
  // A bad stream frame with no data and no fin.
  QuicStreamFrame data1(stream_id1, false, 0, 0);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  session_.OnStreamFrame(data1);
}

TEST_P(QuicSpdySessionTestClient, PendingStreamKeepsConnectionAlive) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_SERVER);

  QuicStreamFrame frame(stream_id, false, 1, "test");
  EXPECT_FALSE(session_.ShouldKeepConnectionAlive());
  session_.OnStreamFrame(frame);
  EXPECT_TRUE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
  EXPECT_TRUE(session_.ShouldKeepConnectionAlive());
}

TEST_P(QuicSpdySessionTestClient, AvailableStreamsClient) {
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(2)) != nullptr);
  // Both server initiated streams with smaller stream IDs should be available.
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedBidirectionalId(0)));
  EXPECT_TRUE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthServerInitiatedBidirectionalId(1)));
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(0)) != nullptr);
  ASSERT_TRUE(session_.GetOrCreateStream(
                  GetNthServerInitiatedBidirectionalId(1)) != nullptr);
  // And client initiated stream ID should be not available.
  EXPECT_FALSE(QuicSessionPeer::IsStreamAvailable(
      &session_, GetNthClientInitiatedBidirectionalId(0)));
}

// Regression test for b/130740258 and https://crbug.com/971779.
// If headers that are too large or empty are received (these cases are handled
// the same way, as QuicHeaderList clears itself when headers exceed the limit),
// then the stream is reset.  No more frames must be sent in this case.
TEST_P(QuicSpdySessionTestClient, TooLargeHeadersMustNotCauseWriteAfterReset) {
  // In IETF QUIC, HEADERS do not carry FIN flag, and OnStreamHeaderList() is
  // never called after an error, including too large headers.
  if (VersionUsesHttp3(transport_version())) {
    return;
  }
  CompleteHandshake();
  TestStream* stream = session_.CreateOutgoingBidirectionalStream();

  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  // Write headers with FIN set to close write side of stream.
  // Header block does not matter.
  stream->WriteHeaders(SpdyHeaderBlock(), /* fin = */ true, nullptr);

  // Receive headers that are too large or empty, with FIN set.
  // This causes the stream to be reset.  No frames must be written after this.
  QuicHeaderList headers;
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(stream->id(), QUIC_HEADERS_TOO_LARGE));
  stream->OnStreamHeaderList(/* fin = */ true,
                             headers.uncompressed_header_bytes(), headers);
}

TEST_P(QuicSpdySessionTestClient, RecordFinAfterReadSideClosed) {
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

TEST_P(QuicSpdySessionTestClient, WritePriority) {
  if (VersionUsesHttp3(transport_version())) {
    // IETF QUIC currently doesn't support PRIORITY.
    return;
  }
  CompleteHandshake();

  TestHeadersStream* headers_stream;
  QuicSpdySessionPeer::SetHeadersStream(&session_, nullptr);
  headers_stream = new TestHeadersStream(&session_);
  QuicSpdySessionPeer::SetHeadersStream(&session_, headers_stream);

  // Make packet writer blocked so |headers_stream| will buffer its write data.
  EXPECT_CALL(*writer_, IsWriteBlocked()).WillRepeatedly(Return(true));

  const QuicStreamId id = 4;
  const QuicStreamId parent_stream_id = 9;
  const SpdyPriority priority = kV3HighestPriority;
  const bool exclusive = true;
  session_.WritePriority(id, parent_stream_id,
                         Spdy3PriorityToHttp2Weight(priority), exclusive);

  QuicStreamSendBuffer& send_buffer =
      QuicStreamPeer::SendBuffer(headers_stream);
  ASSERT_EQ(1u, send_buffer.size());

  SpdyPriorityIR priority_frame(
      id, parent_stream_id, Spdy3PriorityToHttp2Weight(priority), exclusive);
  SpdyFramer spdy_framer(SpdyFramer::ENABLE_COMPRESSION);
  SpdySerializedFrame frame = spdy_framer.SerializeFrame(priority_frame);

  const QuicMemSlice& slice =
      QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer)->slice;
  EXPECT_EQ(quiche::QuicheStringPiece(frame.data(), frame.size()),
            quiche::QuicheStringPiece(slice.data(), slice.length()));
}

TEST_P(QuicSpdySessionTestClient, Http3ServerPush) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  // Push unidirectional stream is type 0x01.
  std::string frame_type1 = quiche::QuicheTextUtils::HexDecode("01");
  QuicStreamId stream_id1 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);
  session_.OnStreamFrame(QuicStreamFrame(stream_id1, /* fin = */ false,
                                         /* offset = */ 0, frame_type1));

  EXPECT_EQ(1u, session_.GetNumOpenIncomingStreams());
  QuicStream* stream = session_.GetOrCreateStream(stream_id1);
  EXPECT_EQ(1u, stream->flow_controller()->bytes_consumed());
  EXPECT_EQ(1u, session_.flow_controller()->bytes_consumed());

  // The same stream type can be encoded differently.
  std::string frame_type2 = quiche::QuicheTextUtils::HexDecode("80000001");
  QuicStreamId stream_id2 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 1);
  session_.OnStreamFrame(QuicStreamFrame(stream_id2, /* fin = */ false,
                                         /* offset = */ 0, frame_type2));

  EXPECT_EQ(2u, session_.GetNumOpenIncomingStreams());
  stream = session_.GetOrCreateStream(stream_id2);
  EXPECT_EQ(4u, stream->flow_controller()->bytes_consumed());
  EXPECT_EQ(5u, session_.flow_controller()->bytes_consumed());
}

TEST_P(QuicSpdySessionTestClient, Http3ServerPushOutofOrderFrame) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  // Push unidirectional stream is type 0x01.
  std::string frame_type = quiche::QuicheTextUtils::HexDecode("01");
  // The first field of a push stream is the Push ID.
  std::string push_id = quiche::QuicheTextUtils::HexDecode("4000");

  QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  QuicStreamFrame data1(stream_id,
                        /* fin = */ false, /* offset = */ 0, frame_type);
  QuicStreamFrame data2(stream_id,
                        /* fin = */ false, /* offset = */ frame_type.size(),
                        push_id);

  // Receiving some stream data without stream type does not open the stream.
  session_.OnStreamFrame(data2);
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  session_.OnStreamFrame(data1);
  EXPECT_EQ(1u, session_.GetNumOpenIncomingStreams());
  QuicStream* stream = session_.GetOrCreateStream(stream_id);
  EXPECT_EQ(3u, stream->flow_controller()->highest_received_byte_offset());
}

TEST_P(QuicSpdySessionTestServer, ZombieStreams) {
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  QuicStreamPeer::SetStreamBytesWritten(3, stream2);
  EXPECT_TRUE(stream2->IsWaitingForAcks());

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(stream2->id(), _));
  session_.CloseStream(stream2->id());
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  ASSERT_EQ(1u, session_.closed_streams()->size());
  EXPECT_EQ(stream2->id(), session_.closed_streams()->front()->id());
  session_.OnStreamDoneWaitingForAcks(2);
  EXPECT_FALSE(QuicContainsKey(session_.zombie_streams(), stream2->id()));
  EXPECT_EQ(1u, session_.closed_streams()->size());
  EXPECT_EQ(stream2->id(), session_.closed_streams()->front()->id());
}

TEST_P(QuicSpdySessionTestServer, OnStreamFrameLost) {
  InSequence s;

  // Drive congestion control manually.
  MockSendAlgorithm* send_algorithm = new StrictMock<MockSendAlgorithm>;
  QuicConnectionPeer::SetSendAlgorithm(session_.connection(), send_algorithm);

  TestCryptoStream* crypto_stream = session_.GetMutableCryptoStream();
  TestStream* stream2 = session_.CreateOutgoingBidirectionalStream();
  TestStream* stream4 = session_.CreateOutgoingBidirectionalStream();

  QuicStreamFrame frame2(stream2->id(), false, 0, 9);
  QuicStreamFrame frame3(stream4->id(), false, 0, 9);

  // Lost data on cryption stream, streams 2 and 4.
  EXPECT_CALL(*stream4, HasPendingRetransmission()).WillOnce(Return(true));
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .WillOnce(Return(true));
  }
  EXPECT_CALL(*stream2, HasPendingRetransmission()).WillOnce(Return(true));
  session_.OnFrameLost(QuicFrame(frame3));
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    QuicStreamFrame frame1(QuicUtils::GetCryptoStreamId(transport_version()),
                           false, 0, 1300);
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
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
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

TEST_P(QuicSpdySessionTestServer, DonotRetransmitDataOfClosedStreams) {
  // Resetting a stream will send a QPACK Stream Cancellation instruction on the
  // decoder stream.  For simplicity, ignore writes on this stream.
  NoopQpackStreamSenderDelegate qpack_stream_sender_delegate;
  if (VersionUsesHttp3(transport_version())) {
    session_.qpack_decoder()->set_qpack_stream_sender_delegate(
        &qpack_stream_sender_delegate);
  }

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

TEST_P(QuicSpdySessionTestServer, RetransmitFrames) {
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

TEST_P(QuicSpdySessionTestServer, OnPriorityFrame) {
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  TestStream* stream = session_.CreateIncomingStream(stream_id);
  session_.OnPriorityFrame(stream_id,
                           spdy::SpdyStreamPrecedence(kV3HighestPriority));
  EXPECT_EQ(spdy::SpdyStreamPrecedence(kV3HighestPriority),
            stream->precedence());
}

TEST_P(QuicSpdySessionTestServer, OnPriorityUpdateFrame) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  // Create control stream.
  QuicStreamId receive_control_stream_id =
      GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
  char type[] = {kControlStream};
  quiche::QuicheStringPiece stream_type(type, 1);
  QuicStreamOffset offset = 0;
  QuicStreamFrame data1(receive_control_stream_id, false, offset, stream_type);
  offset += stream_type.length();
  EXPECT_CALL(debug_visitor,
              OnPeerControlStreamCreated(receive_control_stream_id));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(receive_control_stream_id,
            QuicSpdySessionPeer::GetReceiveControlStream(&session_)->id());

  // Send SETTINGS frame.
  std::string serialized_settings = EncodeSettings({});
  QuicStreamFrame data2(receive_control_stream_id, false, offset,
                        serialized_settings);
  offset += serialized_settings.length();
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(_));
  session_.OnStreamFrame(data2);

  // PRIORITY_UPDATE frame for first request stream.
  const QuicStreamId stream_id1 = GetNthClientInitiatedBidirectionalId(0);
  struct PriorityUpdateFrame priority_update1;
  priority_update1.prioritized_element_type = REQUEST_STREAM;
  priority_update1.prioritized_element_id = stream_id1;
  priority_update1.priority_field_value = "u=2";
  std::string serialized_priority_update1 =
      SerializePriorityUpdateFrame(priority_update1);
  QuicStreamFrame data3(receive_control_stream_id,
                        /* fin = */ false, offset, serialized_priority_update1);
  offset += serialized_priority_update1.size();

  // PRIORITY_UPDATE frame arrives after stream creation.
  TestStream* stream1 = session_.CreateIncomingStream(stream_id1);
  EXPECT_EQ(QuicStream::kDefaultUrgency,
            stream1->precedence().spdy3_priority());
  EXPECT_CALL(debug_visitor, OnPriorityUpdateFrameReceived(priority_update1));
  session_.OnStreamFrame(data3);
  EXPECT_EQ(2u, stream1->precedence().spdy3_priority());

  // PRIORITY_UPDATE frame for second request stream.
  const QuicStreamId stream_id2 = GetNthClientInitiatedBidirectionalId(1);
  struct PriorityUpdateFrame priority_update2;
  priority_update2.prioritized_element_type = REQUEST_STREAM;
  priority_update2.prioritized_element_id = stream_id2;
  priority_update2.priority_field_value = "u=2";
  std::string serialized_priority_update2 =
      SerializePriorityUpdateFrame(priority_update2);
  QuicStreamFrame stream_frame3(receive_control_stream_id,
                                /* fin = */ false, offset,
                                serialized_priority_update2);

  // PRIORITY_UPDATE frame arrives before stream creation,
  // priority value is buffered.
  EXPECT_CALL(debug_visitor, OnPriorityUpdateFrameReceived(priority_update2));
  session_.OnStreamFrame(stream_frame3);
  // Priority is applied upon stream construction.
  TestStream* stream2 = session_.CreateIncomingStream(stream_id2);
  EXPECT_EQ(2u, stream2->precedence().spdy3_priority());
}

TEST_P(QuicSpdySessionTestServer, SimplePendingStreamType) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  char input[] = {0x04,            // type
                  'a', 'b', 'c'};  // data
  quiche::QuicheStringPiece payload(input, QUICHE_ARRAYSIZE(input));

  // This is a server test with a client-initiated unidirectional stream.
  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);

  for (bool fin : {true, false}) {
    QuicStreamFrame frame(stream_id, fin, /* offset = */ 0, payload);

    // A STOP_SENDING frame is sent in response to the unknown stream type.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke([stream_id](const QuicFrame& frame) {
          EXPECT_EQ(STOP_SENDING_FRAME, frame.type);

          QuicStopSendingFrame* stop_sending = frame.stop_sending_frame;
          EXPECT_EQ(stream_id, stop_sending->stream_id);
          EXPECT_EQ(QuicHttp3ErrorCode::IETF_QUIC_HTTP3_STREAM_CREATION_ERROR,
                    static_cast<QuicHttp3ErrorCode>(
                        stop_sending->application_error_code));

          return ClearControlFrame(frame);
        }));
    session_.OnStreamFrame(frame);

    PendingStream* pending =
        QuicSessionPeer::GetPendingStream(&session_, stream_id);
    if (fin) {
      // Stream is closed if FIN is received.
      EXPECT_FALSE(pending);
    } else {
      ASSERT_TRUE(pending);
      // The pending stream must ignore read data.
      EXPECT_TRUE(pending->sequencer()->ignore_read_data());
    }

    stream_id += QuicUtils::StreamIdDelta(transport_version());
  }
}

TEST_P(QuicSpdySessionTestServer, SimplePendingStreamTypeOutOfOrderDelivery) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  char input[] = {0x04,            // type
                  'a', 'b', 'c'};  // data
  quiche::QuicheStringPiece payload(input, QUICHE_ARRAYSIZE(input));

  // This is a server test with a client-initiated unidirectional stream.
  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);

  for (bool fin : {true, false}) {
    QuicStreamFrame frame1(stream_id, /* fin = */ false, /* offset = */ 0,
                           payload.substr(0, 1));
    QuicStreamFrame frame2(stream_id, fin, /* offset = */ 1, payload.substr(1));

    // Deliver frames out of order.
    session_.OnStreamFrame(frame2);
    // A STOP_SENDING frame is sent in response to the unknown stream type.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(&VerifyAndClearStopSendingFrame));
    session_.OnStreamFrame(frame1);

    PendingStream* pending =
        QuicSessionPeer::GetPendingStream(&session_, stream_id);
    if (fin) {
      // Stream is closed if FIN is received.
      EXPECT_FALSE(pending);
    } else {
      ASSERT_TRUE(pending);
      // The pending stream must ignore read data.
      EXPECT_TRUE(pending->sequencer()->ignore_read_data());
    }

    stream_id += QuicUtils::StreamIdDelta(transport_version());
  }
}

TEST_P(QuicSpdySessionTestServer,
       MultipleBytesPendingStreamTypeOutOfOrderDelivery) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  char input[] = {0x41, 0x00,      // type (256)
                  'a', 'b', 'c'};  // data
  quiche::QuicheStringPiece payload(input, QUICHE_ARRAYSIZE(input));

  // This is a server test with a client-initiated unidirectional stream.
  QuicStreamId stream_id = QuicUtils::GetFirstUnidirectionalStreamId(
      transport_version(), Perspective::IS_CLIENT);

  for (bool fin : {true, false}) {
    QuicStreamFrame frame1(stream_id, /* fin = */ false, /* offset = */ 0,
                           payload.substr(0, 1));
    QuicStreamFrame frame2(stream_id, /* fin = */ false, /* offset = */ 1,
                           payload.substr(1, 1));
    QuicStreamFrame frame3(stream_id, fin, /* offset = */ 2, payload.substr(2));

    // Deliver frames out of order.
    session_.OnStreamFrame(frame3);
    // The first byte does not contain the entire type varint.
    session_.OnStreamFrame(frame1);
    // A STOP_SENDING frame is sent in response to the unknown stream type.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(&VerifyAndClearStopSendingFrame));
    session_.OnStreamFrame(frame2);

    PendingStream* pending =
        QuicSessionPeer::GetPendingStream(&session_, stream_id);
    if (fin) {
      // Stream is closed if FIN is received.
      EXPECT_FALSE(pending);
    } else {
      ASSERT_TRUE(pending);
      // The pending stream must ignore read data.
      EXPECT_TRUE(pending->sequencer()->ignore_read_data());
    }

    stream_id += QuicUtils::StreamIdDelta(transport_version());
  }
}

TEST_P(QuicSpdySessionTestServer, ReceiveControlStream) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  CompleteHandshake();
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  // Use an arbitrary stream id.
  QuicStreamId stream_id =
      GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
  char type[] = {kControlStream};

  QuicStreamFrame data1(stream_id, false, 0,
                        quiche::QuicheStringPiece(type, 1));
  EXPECT_CALL(debug_visitor, OnPeerControlStreamCreated(stream_id));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(stream_id,
            QuicSpdySessionPeer::GetReceiveControlStream(&session_)->id());

  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 512;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] = 5;
  settings.values[SETTINGS_QPACK_BLOCKED_STREAMS] = 42;
  std::string data = EncodeSettings(settings);
  QuicStreamFrame frame(stream_id, false, 1, quiche::QuicheStringPiece(data));

  QpackEncoder* qpack_encoder = session_.qpack_encoder();
  QpackHeaderTable* header_table =
      QpackEncoderPeer::header_table(qpack_encoder);

  EXPECT_NE(512u,
            QpackHeaderTablePeer::maximum_dynamic_table_capacity(header_table));
  EXPECT_NE(5u, session_.max_outbound_header_list_size());
  EXPECT_NE(42u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));

  EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(settings));
  session_.OnStreamFrame(frame);

  EXPECT_EQ(512u,
            QpackHeaderTablePeer::maximum_dynamic_table_capacity(header_table));
  EXPECT_EQ(5u, session_.max_outbound_header_list_size());
  EXPECT_EQ(42u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));
}

TEST_P(QuicSpdySessionTestServer, ReceiveControlStreamOutOfOrderDelivery) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  // Use an arbitrary stream id.
  QuicStreamId stream_id =
      GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
  char type[] = {kControlStream};
  SettingsFrame settings;
  settings.values[3] = 2;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] = 5;
  std::string data = EncodeSettings(settings);

  QuicStreamFrame data1(stream_id, false, 1, quiche::QuicheStringPiece(data));
  QuicStreamFrame data2(stream_id, false, 0,
                        quiche::QuicheStringPiece(type, 1));

  session_.OnStreamFrame(data1);
  EXPECT_NE(5u, session_.max_outbound_header_list_size());
  session_.OnStreamFrame(data2);
  EXPECT_EQ(5u, session_.max_outbound_header_list_size());
}

// Regression test for https://crbug.com/1009551.
TEST_P(QuicSpdySessionTestServer, StreamClosedWhileHeaderDecodingBlocked) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  session_.qpack_decoder()->OnSetDynamicTableCapacity(1024);

  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  TestStream* stream = session_.CreateIncomingStream(stream_id);

  // HEADERS frame referencing first dynamic table entry.
  std::string headers_payload = quiche::QuicheTextUtils::HexDecode("020080");
  std::unique_ptr<char[]> headers_buffer;
  QuicByteCount headers_frame_header_length =
      HttpEncoder::SerializeHeadersFrameHeader(headers_payload.length(),
                                               &headers_buffer);
  quiche::QuicheStringPiece headers_frame_header(headers_buffer.get(),
                                                 headers_frame_header_length);
  std::string headers =
      quiche::QuicheStrCat(headers_frame_header, headers_payload);
  stream->OnStreamFrame(QuicStreamFrame(stream_id, false, 0, headers));

  // Decoding is blocked because dynamic table entry has not been received yet.
  EXPECT_FALSE(stream->headers_decompressed());

  // Stream is closed and destroyed.
  CloseStream(stream_id);
  session_.CleanUpClosedStreams();

  // Dynamic table entry arrived on the decoder stream.
  // The destroyed stream object must not be referenced.
  session_.qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
}

// Regression test for https://crbug.com/1011294.
TEST_P(QuicSpdySessionTestServer, SessionDestroyedWhileHeaderDecodingBlocked) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  session_.qpack_decoder()->OnSetDynamicTableCapacity(1024);

  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  TestStream* stream = session_.CreateIncomingStream(stream_id);

  // HEADERS frame referencing first dynamic table entry.
  std::string headers_payload = quiche::QuicheTextUtils::HexDecode("020080");
  std::unique_ptr<char[]> headers_buffer;
  QuicByteCount headers_frame_header_length =
      HttpEncoder::SerializeHeadersFrameHeader(headers_payload.length(),
                                               &headers_buffer);
  quiche::QuicheStringPiece headers_frame_header(headers_buffer.get(),
                                                 headers_frame_header_length);
  std::string headers =
      quiche::QuicheStrCat(headers_frame_header, headers_payload);
  stream->OnStreamFrame(QuicStreamFrame(stream_id, false, 0, headers));

  // Decoding is blocked because dynamic table entry has not been received yet.
  EXPECT_FALSE(stream->headers_decompressed());

  // |session_| gets destoyed.  That destroys QpackDecoder, a member of
  // QuicSpdySession (derived class), which destroys QpackHeaderTable.
  // Then |*stream|, owned by QuicSession (base class) get destroyed, which
  // destroys QpackProgessiveDecoder, a registered Observer of QpackHeaderTable.
  // This must not cause a crash.
}

TEST_P(QuicSpdySessionTestClient, ResetAfterInvalidIncomingStreamType) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  ASSERT_TRUE(session_.UsesPendingStreams());

  const QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  // Payload consists of two bytes.  The first byte is an unknown unidirectional
  // stream type.  The second one would be the type of a push stream, but it
  // must not be interpreted as stream type.
  std::string payload = quiche::QuicheTextUtils::HexDecode("3f01");
  QuicStreamFrame frame(stream_id, /* fin = */ false, /* offset = */ 0,
                        payload);

  // A STOP_SENDING frame is sent in response to the unknown stream type.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&VerifyAndClearStopSendingFrame));
  session_.OnStreamFrame(frame);

  // There are no active streams.
  EXPECT_EQ(0u, session_.GetNumOpenIncomingStreams());

  // The pending stream is still around, because it did not receive a FIN.
  PendingStream* pending =
      QuicSessionPeer::GetPendingStream(&session_, stream_id);
  ASSERT_TRUE(pending);

  // The pending stream must ignore read data.
  EXPECT_TRUE(pending->sequencer()->ignore_read_data());

  // If the stream frame is received again, it should be ignored.
  session_.OnStreamFrame(frame);

  // Receive RESET_STREAM.
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream_id,
                               QUIC_STREAM_CANCELLED,
                               /* bytes_written = */ payload.size());

  session_.OnRstStream(rst_frame);

  // The stream is closed.
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
}

TEST_P(QuicSpdySessionTestClient, FinAfterInvalidIncomingStreamType) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  ASSERT_TRUE(session_.UsesPendingStreams());

  const QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  // Payload consists of two bytes.  The first byte is an unknown unidirectional
  // stream type.  The second one would be the type of a push stream, but it
  // must not be interpreted as stream type.
  std::string payload = quiche::QuicheTextUtils::HexDecode("3f01");
  QuicStreamFrame frame(stream_id, /* fin = */ false, /* offset = */ 0,
                        payload);

  // A STOP_SENDING frame is sent in response to the unknown stream type.
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&VerifyAndClearStopSendingFrame));
  session_.OnStreamFrame(frame);

  // The pending stream is still around, because it did not receive a FIN.
  PendingStream* pending =
      QuicSessionPeer::GetPendingStream(&session_, stream_id);
  EXPECT_TRUE(pending);

  // The pending stream must ignore read data.
  EXPECT_TRUE(pending->sequencer()->ignore_read_data());

  // If the stream frame is received again, it should be ignored.
  session_.OnStreamFrame(frame);

  // Receive FIN.
  session_.OnStreamFrame(QuicStreamFrame(stream_id, /* fin = */ true,
                                         /* offset = */ payload.size(), ""));

  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
}

TEST_P(QuicSpdySessionTestClient, ResetInMiddleOfStreamType) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  ASSERT_TRUE(session_.UsesPendingStreams());

  const QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  // Payload is the first byte of a two byte varint encoding.
  std::string payload = quiche::QuicheTextUtils::HexDecode("40");
  QuicStreamFrame frame(stream_id, /* fin = */ false, /* offset = */ 0,
                        payload);

  session_.OnStreamFrame(frame);
  EXPECT_TRUE(QuicSessionPeer::GetPendingStream(&session_, stream_id));

  // Receive RESET_STREAM.
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream_id,
                               QUIC_STREAM_CANCELLED,
                               /* bytes_written = */ payload.size());

  session_.OnRstStream(rst_frame);

  // The stream is closed.
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
}

TEST_P(QuicSpdySessionTestClient, FinInMiddleOfStreamType) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  ASSERT_TRUE(session_.UsesPendingStreams());

  const QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  // Payload is the first byte of a two byte varint encoding with a FIN.
  std::string payload = quiche::QuicheTextUtils::HexDecode("40");
  QuicStreamFrame frame(stream_id, /* fin = */ true, /* offset = */ 0, payload);

  session_.OnStreamFrame(frame);
  EXPECT_FALSE(QuicSessionPeer::GetPendingStream(&session_, stream_id));
}

TEST_P(QuicSpdySessionTestClient, DuplicateHttp3UnidirectionalStreams) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  QuicStreamId id1 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);
  char type1[] = {kControlStream};

  QuicStreamFrame data1(id1, false, 0, quiche::QuicheStringPiece(type1, 1));
  EXPECT_CALL(debug_visitor, OnPeerControlStreamCreated(id1));
  session_.OnStreamFrame(data1);
  QuicStreamId id2 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 1);
  QuicStreamFrame data2(id2, false, 0, quiche::QuicheStringPiece(type1, 1));
  EXPECT_CALL(debug_visitor, OnPeerControlStreamCreated(id2)).Times(0);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM,
                              "Control stream is received twice.", _));
  EXPECT_QUIC_PEER_BUG(
      session_.OnStreamFrame(data2),
      "Received a duplicate Control stream: Closing connection.");

  QuicStreamId id3 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 2);
  char type2[]{kQpackEncoderStream};

  QuicStreamFrame data3(id3, false, 0, quiche::QuicheStringPiece(type2, 1));
  EXPECT_CALL(debug_visitor, OnPeerQpackEncoderStreamCreated(id3));
  session_.OnStreamFrame(data3);

  QuicStreamId id4 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 3);
  QuicStreamFrame data4(id4, false, 0, quiche::QuicheStringPiece(type2, 1));
  EXPECT_CALL(debug_visitor, OnPeerQpackEncoderStreamCreated(id4)).Times(0);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM,
                              "QPACK encoder stream is received twice.", _));
  EXPECT_QUIC_PEER_BUG(
      session_.OnStreamFrame(data4),
      "Received a duplicate QPACK encoder stream: Closing connection.");

  QuicStreamId id5 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 4);
  char type3[]{kQpackDecoderStream};

  QuicStreamFrame data5(id5, false, 0, quiche::QuicheStringPiece(type3, 1));
  EXPECT_CALL(debug_visitor, OnPeerQpackDecoderStreamCreated(id5));
  session_.OnStreamFrame(data5);

  QuicStreamId id6 =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 5);
  QuicStreamFrame data6(id6, false, 0, quiche::QuicheStringPiece(type3, 1));
  EXPECT_CALL(debug_visitor, OnPeerQpackDecoderStreamCreated(id6)).Times(0);
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM,
                              "QPACK decoder stream is received twice.", _));
  EXPECT_QUIC_PEER_BUG(
      session_.OnStreamFrame(data6),
      "Received a duplicate QPACK decoder stream: Closing connection.");
}

TEST_P(QuicSpdySessionTestClient, EncoderStreamError) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  std::string data = quiche::QuicheTextUtils::HexDecode(
      "02"    // Encoder stream.
      "00");  // Duplicate entry 0, but no entries exist.

  QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  QuicStreamFrame frame(stream_id, /* fin = */ false, /* offset = */ 0, data);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_ENCODER_STREAM_ERROR,
                      "Encoder stream error: Invalid relative index.", _));
  session_.OnStreamFrame(frame);
}

TEST_P(QuicSpdySessionTestClient, DecoderStreamError) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  std::string data = quiche::QuicheTextUtils::HexDecode(
      "03"    // Decoder stream.
      "00");  // Insert Count Increment with forbidden increment value of zero.

  QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);

  QuicStreamFrame frame(stream_id, /* fin = */ false, /* offset = */ 0, data);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECODER_STREAM_ERROR,
                      "Decoder stream error: Invalid increment value 0.", _));
  session_.OnStreamFrame(frame);
}

TEST_P(QuicSpdySessionTestClient, InvalidHttp3GoAway) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  EXPECT_CALL(
      *connection_,
      CloseConnection(
          QUIC_INVALID_STREAM_ID,
          "GOAWAY's last stream id has to point to a request stream", _));
  QuicStreamId stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 0);
  session_.OnHttp3GoAway(stream_id);
}

// Test that receipt of CANCEL_PUSH frame does not result in closing the
// connection.
// TODO(b/151841240): Handle CANCEL_PUSH frames instead of ignoring them.
TEST_P(QuicSpdySessionTestClient, IgnoreCancelPush) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  // Create control stream.
  QuicStreamId receive_control_stream_id =
      GetNthServerInitiatedUnidirectionalStreamId(transport_version(), 3);
  char type[] = {kControlStream};
  quiche::QuicheStringPiece stream_type(type, 1);
  QuicStreamOffset offset = 0;
  QuicStreamFrame data1(receive_control_stream_id, /* fin = */ false, offset,
                        stream_type);
  offset += stream_type.length();
  EXPECT_CALL(debug_visitor,
              OnPeerControlStreamCreated(receive_control_stream_id));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(receive_control_stream_id,
            QuicSpdySessionPeer::GetReceiveControlStream(&session_)->id());

  // First frame has to be SETTINGS.
  std::string serialized_settings = EncodeSettings({});
  QuicStreamFrame data2(receive_control_stream_id, /* fin = */ false, offset,
                        serialized_settings);
  offset += serialized_settings.length();
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(_));
  session_.OnStreamFrame(data2);

  CancelPushFrame cancel_push{/* push_id = */ 0};
  std::unique_ptr<char[]> buffer;
  auto frame_length =
      HttpEncoder::SerializeCancelPushFrame(cancel_push, &buffer);
  QuicStreamFrame data3(receive_control_stream_id, /* fin = */ false, offset,
                        quiche::QuicheStringPiece(buffer.get(), frame_length));
  EXPECT_CALL(debug_visitor, OnCancelPushFrameReceived(_));
  session_.OnStreamFrame(data3);
}

TEST_P(QuicSpdySessionTestServer, ServerPushEnabledDefaultValue) {
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_FALSE(session_.server_push_enabled());
  } else {
    EXPECT_TRUE(session_.server_push_enabled());
  }
}

TEST_P(QuicSpdySessionTestServer, OnSetting) {
  CompleteHandshake();
  if (VersionUsesHttp3(transport_version())) {
    EXPECT_EQ(std::numeric_limits<size_t>::max(),
              session_.max_outbound_header_list_size());
    session_.OnSetting(SETTINGS_MAX_HEADER_LIST_SIZE, 5);
    EXPECT_EQ(5u, session_.max_outbound_header_list_size());

    EXPECT_CALL(*writer_, WritePacket(_, _, _, _, _))
        .WillRepeatedly(Return(WriteResult(WRITE_STATUS_OK, 0)));
    QpackEncoder* qpack_encoder = session_.qpack_encoder();
    EXPECT_EQ(0u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));
    session_.OnSetting(SETTINGS_QPACK_BLOCKED_STREAMS, 12);
    EXPECT_EQ(12u, QpackEncoderPeer::maximum_blocked_streams(qpack_encoder));

    QpackHeaderTable* header_table =
        QpackEncoderPeer::header_table(qpack_encoder);
    EXPECT_EQ(0u, header_table->maximum_dynamic_table_capacity());
    session_.OnSetting(SETTINGS_QPACK_MAX_TABLE_CAPACITY, 37);
    EXPECT_EQ(37u, header_table->maximum_dynamic_table_capacity());

    return;
  }

  EXPECT_EQ(std::numeric_limits<size_t>::max(),
            session_.max_outbound_header_list_size());
  session_.OnSetting(SETTINGS_MAX_HEADER_LIST_SIZE, 5);
  EXPECT_EQ(5u, session_.max_outbound_header_list_size());

  EXPECT_TRUE(session_.server_push_enabled());
  session_.OnSetting(spdy::SETTINGS_ENABLE_PUSH, 0);
  EXPECT_FALSE(session_.server_push_enabled());

  spdy::HpackEncoder* hpack_encoder =
      QuicSpdySessionPeer::GetSpdyFramer(&session_)->GetHpackEncoder();
  EXPECT_EQ(4096u, hpack_encoder->CurrentHeaderTableSizeSetting());
  session_.OnSetting(spdy::SETTINGS_HEADER_TABLE_SIZE, 59);
  EXPECT_EQ(59u, hpack_encoder->CurrentHeaderTableSizeSetting());
}

TEST_P(QuicSpdySessionTestServer, FineGrainedHpackErrorCodes) {
  if (VersionUsesHttp3(transport_version())) {
    // HPACK is not used in HTTP/3.
    return;
  }

  QuicFlagSaver flag_saver;
  SetQuicReloadableFlag(spdy_enable_granular_decompress_errors, true);

  QuicStreamId request_stream_id = 5;
  session_.CreateIncomingStream(request_stream_id);

  // Index 126 does not exist (static table has 61 entries and dynamic table is
  // empty).
  std::string headers_frame = quiche::QuicheTextUtils::HexDecode(
      "000006"    // length
      "01"        // type
      "24"        // flags: PRIORITY | END_HEADERS
      "00000005"  // stream_id
      "00000000"  // stream dependency
      "10"        // weight
      "fe");      // payload: reference to index 126.
  QuicStreamId headers_stream_id =
      QuicUtils::GetHeadersStreamId(transport_version());
  QuicStreamFrame data(headers_stream_id, false, 0, headers_frame);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HPACK_INVALID_INDEX,
                      "SPDY framing error: HPACK_INVALID_INDEX",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));
  session_.OnStreamFrame(data);
}

TEST_P(QuicSpdySessionTestServer, PeerClosesCriticalReceiveStream) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  struct {
    char type;
    const char* error_details;
  } kTestData[] = {
      {kControlStream, "RESET_STREAM received for receive control stream"},
      {kQpackEncoderStream, "RESET_STREAM received for QPACK receive stream"},
      {kQpackDecoderStream, "RESET_STREAM received for QPACK receive stream"},
  };
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kTestData); ++i) {
    QuicStreamId stream_id =
        GetNthClientInitiatedUnidirectionalStreamId(transport_version(), i + 1);
    const QuicByteCount data_length = 1;
    QuicStreamFrame data(
        stream_id, false, 0,
        quiche::QuicheStringPiece(&kTestData[i].type, data_length));
    session_.OnStreamFrame(data);

    EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM,
                                              kTestData[i].error_details, _));

    QuicRstStreamFrame rst(kInvalidControlFrameId, stream_id,
                           QUIC_STREAM_CANCELLED, data_length);
    session_.OnRstStream(rst);
  }
}

TEST_P(QuicSpdySessionTestServer, PeerClosesCriticalSendStream) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  QuicSendControlStream* control_stream =
      QuicSpdySessionPeer::GetSendControlStream(&session_);
  ASSERT_TRUE(control_stream);

  QuicStopSendingFrame stop_sending_control_stream(
      kInvalidControlFrameId, control_stream->id(),
      static_cast<QuicApplicationErrorCode>(QUIC_STREAM_CANCELLED));
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM,
                      "STOP_SENDING received for send control stream", _));
  session_.OnStopSendingFrame(stop_sending_control_stream);

  QpackSendStream* decoder_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(&session_);
  ASSERT_TRUE(decoder_stream);

  QuicStopSendingFrame stop_sending_decoder_stream(
      kInvalidControlFrameId, decoder_stream->id(),
      static_cast<QuicApplicationErrorCode>(QUIC_STREAM_CANCELLED));
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM,
                      "STOP_SENDING received for QPACK send stream", _));
  session_.OnStopSendingFrame(stop_sending_decoder_stream);

  QpackSendStream* encoder_stream =
      QuicSpdySessionPeer::GetQpackEncoderSendStream(&session_);
  ASSERT_TRUE(encoder_stream);

  QuicStopSendingFrame stop_sending_encoder_stream(
      kInvalidControlFrameId, encoder_stream->id(),
      static_cast<QuicApplicationErrorCode>(QUIC_STREAM_CANCELLED));
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_CLOSED_CRITICAL_STREAM,
                      "STOP_SENDING received for QPACK send stream", _));
  session_.OnStopSendingFrame(stop_sending_encoder_stream);
}

// Test that receipt of CANCEL_PUSH frame does not result in closing the
// connection.
// TODO(b/151841240): Handle CANCEL_PUSH frames instead of ignoring them.
TEST_P(QuicSpdySessionTestServer, IgnoreCancelPush) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  // Create control stream.
  QuicStreamId receive_control_stream_id =
      GetNthClientInitiatedUnidirectionalStreamId(transport_version(), 3);
  char type[] = {kControlStream};
  quiche::QuicheStringPiece stream_type(type, 1);
  QuicStreamOffset offset = 0;
  QuicStreamFrame data1(receive_control_stream_id, /* fin = */ false, offset,
                        stream_type);
  offset += stream_type.length();
  EXPECT_CALL(debug_visitor,
              OnPeerControlStreamCreated(receive_control_stream_id));
  session_.OnStreamFrame(data1);
  EXPECT_EQ(receive_control_stream_id,
            QuicSpdySessionPeer::GetReceiveControlStream(&session_)->id());

  // First frame has to be SETTINGS.
  std::string serialized_settings = EncodeSettings({});
  QuicStreamFrame data2(receive_control_stream_id, /* fin = */ false, offset,
                        serialized_settings);
  offset += serialized_settings.length();
  EXPECT_CALL(debug_visitor, OnSettingsFrameReceived(_));
  session_.OnStreamFrame(data2);

  CancelPushFrame cancel_push{/* push_id = */ 0};
  std::unique_ptr<char[]> buffer;
  auto frame_length =
      HttpEncoder::SerializeCancelPushFrame(cancel_push, &buffer);
  QuicStreamFrame data3(receive_control_stream_id, /* fin = */ false, offset,
                        quiche::QuicheStringPiece(buffer.get(), frame_length));
  EXPECT_CALL(debug_visitor, OnCancelPushFrameReceived(_));
  session_.OnStreamFrame(data3);
}

TEST_P(QuicSpdySessionTestClient, SendInitialMaxPushIdIfSet) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  const QuicStreamId max_push_id = 5;
  session_.SetMaxPushId(max_push_id);

  InSequence s;
  EXPECT_CALL(debug_visitor, OnSettingsFrameSent(_));
  const MaxPushIdFrame max_push_id_frame{max_push_id};
  EXPECT_CALL(debug_visitor, OnMaxPushIdFrameSent(max_push_id_frame));

  CompleteHandshake();
}

TEST_P(QuicSpdySessionTestClient, DoNotSendInitialMaxPushIdIfNotSet) {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_.set_debug_visitor(&debug_visitor);

  InSequence s;
  EXPECT_CALL(debug_visitor, OnSettingsFrameSent(_));

  CompleteHandshake();
}

}  // namespace
}  // namespace test
}  // namespace quic
