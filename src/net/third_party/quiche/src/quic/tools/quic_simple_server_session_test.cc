// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_simple_server_session.h"

#include <algorithm>
#include <memory>

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_session_visitor.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sustained_bandwidth_recorder_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/tools/quic_backend_response.h"
#include "net/third_party/quiche/src/quic/tools/quic_memory_cache_backend.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_stream.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

typedef QuicSimpleServerSession::PromisedStreamInfo PromisedStreamInfo;

const QuicByteCount kHeadersFrameHeaderLength = 2;
const QuicByteCount kHeadersFramePayloadLength = 9;

}  // namespace

class QuicSimpleServerSessionPeer {
 public:
  static void SetCryptoStream(QuicSimpleServerSession* s,
                              QuicCryptoServerStream* crypto_stream) {
    s->crypto_stream_.reset(crypto_stream);
  }

  static QuicSpdyStream* CreateIncomingStream(QuicSimpleServerSession* s,
                                              QuicStreamId id) {
    return s->CreateIncomingStream(id);
  }

  static QuicSimpleServerStream* CreateOutgoingUnidirectionalStream(
      QuicSimpleServerSession* s) {
    return s->CreateOutgoingUnidirectionalStream();
  }
};

namespace {

const size_t kMaxStreamsForTest = 10;

class MockQuicCryptoServerStream : public QuicCryptoServerStream {
 public:
  explicit MockQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicServerSessionBase* session,
      QuicCryptoServerStream::Helper* helper)
      : QuicCryptoServerStream(
            crypto_config,
            compressed_certs_cache,
            session,
            helper) {}
  MockQuicCryptoServerStream(const MockQuicCryptoServerStream&) = delete;
  MockQuicCryptoServerStream& operator=(const MockQuicCryptoServerStream&) =
      delete;
  ~MockQuicCryptoServerStream() override {}

  MOCK_METHOD1(SendServerConfigUpdate,
               void(const CachedNetworkParameters* cached_network_parameters));

  void set_encryption_established(bool has_established) {
    encryption_established_override_ = has_established;
  }

  bool encryption_established() const override {
    return QuicCryptoServerStream::encryption_established() ||
           encryption_established_override_;
  }

 private:
  bool encryption_established_override_ = false;
};

class MockQuicConnectionWithSendStreamData : public MockQuicConnection {
 public:
  MockQuicConnectionWithSendStreamData(
      MockQuicConnectionHelper* helper,
      MockAlarmFactory* alarm_factory,
      Perspective perspective,
      const ParsedQuicVersionVector& supported_versions)
      : MockQuicConnection(helper,
                           alarm_factory,
                           perspective,
                           supported_versions) {
    auto consume_all_data = [](QuicStreamId /*id*/, size_t write_length,
                               QuicStreamOffset /*offset*/,
                               StreamSendingState state) {
      return QuicConsumedData(write_length, state != NO_FIN);
    };
    ON_CALL(*this, SendStreamData(_, _, _, _))
        .WillByDefault(Invoke(consume_all_data));
  }

  MOCK_METHOD4(SendStreamData,
               QuicConsumedData(QuicStreamId id,
                                size_t write_length,
                                QuicStreamOffset offset,
                                StreamSendingState state));
};

class MockQuicSimpleServerSession : public QuicSimpleServerSession {
 public:
  MockQuicSimpleServerSession(
      const QuicConfig& config,
      QuicConnection* connection,
      QuicSession::Visitor* visitor,
      QuicCryptoServerStream::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicSimpleServerBackend* quic_simple_server_backend)
      : QuicSimpleServerSession(config,
                                CurrentSupportedVersions(),
                                connection,
                                visitor,
                                helper,
                                crypto_config,
                                compressed_certs_cache,
                                quic_simple_server_backend) {}
  // Methods taking non-copyable types like SpdyHeaderBlock by value cannot be
  // mocked directly.
  void WritePushPromise(QuicStreamId original_stream_id,
                        QuicStreamId promised_stream_id,
                        spdy::SpdyHeaderBlock headers) override {
    return WritePushPromiseMock(original_stream_id, promised_stream_id,
                                headers);
  }
  MOCK_METHOD3(WritePushPromiseMock,
               void(QuicStreamId original_stream_id,
                    QuicStreamId promised_stream_id,
                    const spdy::SpdyHeaderBlock& headers));

  MOCK_METHOD1(SendBlocked, void(QuicStreamId));
};

class QuicSimpleServerSessionTest
    : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  // The function ensures that A) the MAX_STREAMS frames get properly deleted
  // (since the test uses a 'did we leak memory' check ... if we just lose the
  // frame, the test fails) and B) returns true (instead of the default, false)
  // which ensures that the rest of the system thinks that the frame actually
  // was transmitted.
  bool ClearMaxStreamsControlFrame(const QuicFrame& frame) {
    if (frame.type == MAX_STREAMS_FRAME) {
      DeleteFrame(&const_cast<QuicFrame&>(frame));
      return true;
    }
    return false;
  }

 protected:
  QuicSimpleServerSessionTest()
      : crypto_config_(QuicCryptoServerConfig::TESTING,
                       QuicRandom::GetInstance(),
                       crypto_test_utils::ProofSourceForTesting(),
                       KeyExchangeSource::Default()),
        compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize) {
    SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
    config_.SetMaxIncomingBidirectionalStreamsToSend(kMaxStreamsForTest);
    QuicConfigPeer::SetReceivedMaxIncomingBidirectionalStreams(
        &config_, kMaxStreamsForTest);
    config_.SetMaxIncomingUnidirectionalStreamsToSend(kMaxStreamsForTest);

    config_.SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    config_.SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);
    if (VersionUsesQpack(GetParam().transport_version)) {
      QuicConfigPeer::SetReceivedMaxIncomingUnidirectionalStreams(
          &config_, kMaxStreamsForTest + 3);
    } else {
      QuicConfigPeer::SetReceivedMaxIncomingUnidirectionalStreams(
          &config_, kMaxStreamsForTest);
    }

    ParsedQuicVersionVector supported_versions = SupportedVersions(GetParam());
    connection_ = new StrictMock<MockQuicConnectionWithSendStreamData>(
        &helper_, &alarm_factory_, Perspective::IS_SERVER, supported_versions);
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
    session_ = QuicMakeUnique<MockQuicSimpleServerSession>(
        config_, connection_, &owner_, &stream_helper_, &crypto_config_,
        &compressed_certs_cache_, &memory_cache_backend_);
    MockClock clock;
    handshake_message_ = crypto_config_.AddDefaultConfig(
        QuicRandom::GetInstance(), &clock,
        QuicCryptoServerConfig::ConfigOptions());
    session_->Initialize();
    QuicSessionPeer::GetMutableCryptoStream(session_.get())
        ->OnSuccessfulVersionNegotiation(supported_versions.front());
    visitor_ = QuicConnectionPeer::GetVisitor(connection_);

    if (VersionHasIetfQuicFrames(transport_version())) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillRepeatedly(Invoke(
              this, &QuicSimpleServerSessionTest::ClearMaxStreamsControlFrame));
    }
    session_->OnConfigNegotiated();
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return GetNthClientInitiatedBidirectionalStreamId(
        connection_->transport_version(), n);
  }

  QuicStreamId GetNthServerInitiatedUnidirectionalId(int n) {
    return quic::test::GetNthServerInitiatedUnidirectionalStreamId(
        connection_->transport_version(), n);
  }

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  void InjectStopSending(QuicStreamId stream_id,
                         QuicRstStreamErrorCode rst_stream_code) {
    // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
    // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes
    // a one-way close.
    if (!VersionHasIetfQuicFrames(transport_version())) {
      // Only needed for version 99/IETF QUIC.
      return;
    }
    EXPECT_CALL(owner_, OnStopSendingReceived(_)).Times(1);
    QuicStopSendingFrame stop_sending(
        kInvalidControlFrameId, stream_id,
        static_cast<QuicApplicationErrorCode>(rst_stream_code));
    // Expect the RESET_STREAM that is generated in response to receiving a
    // STOP_SENDING.
    EXPECT_CALL(*connection_, OnStreamReset(stream_id, rst_stream_code));
    session_->OnStopSendingFrame(stop_sending);
  }

  StrictMock<MockQuicSessionVisitor> owner_;
  StrictMock<MockQuicCryptoServerStreamHelper> stream_helper_;
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnectionWithSendStreamData>* connection_;
  QuicConfig config_;
  QuicCryptoServerConfig crypto_config_;
  QuicCompressedCertsCache compressed_certs_cache_;
  QuicMemoryCacheBackend memory_cache_backend_;
  std::unique_ptr<MockQuicSimpleServerSession> session_;
  std::unique_ptr<CryptoHandshakeMessage> handshake_message_;
  QuicConnectionVisitorInterface* visitor_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSimpleServerSessionTest,
                         ::testing::ValuesIn(AllSupportedVersions()));

TEST_P(QuicSimpleServerSessionTest, CloseStreamDueToReset) {
  // Open a stream, then reset it.
  // Send two bytes of payload to open it.
  QuicStreamFrame data1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        QuicStringPiece("HT"));
  session_->OnStreamFrame(data1);
  EXPECT_EQ(1u, session_->GetNumOpenIncomingStreams());

  // Receive a reset (and send a RST in response).
  QuicRstStreamFrame rst1(kInvalidControlFrameId,
                          GetNthClientInitiatedBidirectionalId(0),
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(owner_, OnRstStreamReceived(_)).Times(1);
  EXPECT_CALL(*connection_, SendControlFrame(_));
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // For version 99, this is covered in InjectStopSending()
    EXPECT_CALL(*connection_,
                OnStreamReset(GetNthClientInitiatedBidirectionalId(0),
                              QUIC_RST_ACKNOWLEDGEMENT));
  }
  visitor_->OnRstStream(rst1);
  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes
  // a one-way close.
  InjectStopSending(GetNthClientInitiatedBidirectionalId(0),
                    QUIC_ERROR_PROCESSING_STREAM);
  EXPECT_EQ(0u, session_->GetNumOpenIncomingStreams());

  // Send the same two bytes of payload in a new packet.
  visitor_->OnStreamFrame(data1);

  // The stream should not be re-opened.
  EXPECT_EQ(0u, session_->GetNumOpenIncomingStreams());
  EXPECT_TRUE(connection_->connected());
}

TEST_P(QuicSimpleServerSessionTest, NeverOpenStreamDueToReset) {
  // Send a reset (and expect the peer to send a RST in response).
  QuicRstStreamFrame rst1(kInvalidControlFrameId,
                          GetNthClientInitiatedBidirectionalId(0),
                          QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(owner_, OnRstStreamReceived(_)).Times(1);
  if (!VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(*connection_, SendControlFrame(_));
    // For version 99, this is covered in InjectStopSending()
    EXPECT_CALL(*connection_,
                OnStreamReset(GetNthClientInitiatedBidirectionalId(0),
                              QUIC_RST_ACKNOWLEDGEMENT));
  }
  visitor_->OnRstStream(rst1);
  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes
  // a one-way close.
  InjectStopSending(GetNthClientInitiatedBidirectionalId(0),
                    QUIC_ERROR_PROCESSING_STREAM);

  EXPECT_EQ(0u, session_->GetNumOpenIncomingStreams());

  // Send two bytes of payload.
  QuicStreamFrame data1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        QuicStringPiece("HT"));
  visitor_->OnStreamFrame(data1);

  // The stream should never be opened, now that the reset is received.
  EXPECT_EQ(0u, session_->GetNumOpenIncomingStreams());
  EXPECT_TRUE(connection_->connected());
}

TEST_P(QuicSimpleServerSessionTest, AcceptClosedStream) {
  // Send (empty) compressed headers followed by two bytes of data.
  QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                         QuicStringPiece("\1\0\0\0\0\0\0\0HT"));
  QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(1), false, 0,
                         QuicStringPiece("\2\0\0\0\0\0\0\0HT"));
  visitor_->OnStreamFrame(frame1);
  visitor_->OnStreamFrame(frame2);
  EXPECT_EQ(2u, session_->GetNumOpenIncomingStreams());

  // Send a reset (and expect the peer to send a RST in response).
  QuicRstStreamFrame rst(kInvalidControlFrameId,
                         GetNthClientInitiatedBidirectionalId(0),
                         QUIC_ERROR_PROCESSING_STREAM, 0);
  EXPECT_CALL(owner_, OnRstStreamReceived(_)).Times(1);
  if (!VersionHasIetfQuicFrames(transport_version())) {
    EXPECT_CALL(*connection_, SendControlFrame(_));
    // For version 99, this is covered in InjectStopSending()
    EXPECT_CALL(*connection_,
                OnStreamReset(GetNthClientInitiatedBidirectionalId(0),
                              QUIC_RST_ACKNOWLEDGEMENT));
  }
  visitor_->OnRstStream(rst);
  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes
  // a one-way close.
  InjectStopSending(GetNthClientInitiatedBidirectionalId(0),
                    QUIC_ERROR_PROCESSING_STREAM);

  // If we were tracking, we'd probably want to reject this because it's data
  // past the reset point of stream 3.  As it's a closed stream we just drop the
  // data on the floor, but accept the packet because it has data for stream 5.
  QuicStreamFrame frame3(GetNthClientInitiatedBidirectionalId(0), false, 2,
                         QuicStringPiece("TP"));
  QuicStreamFrame frame4(GetNthClientInitiatedBidirectionalId(1), false, 2,
                         QuicStringPiece("TP"));
  visitor_->OnStreamFrame(frame3);
  visitor_->OnStreamFrame(frame4);
  // The stream should never be opened, now that the reset is received.
  EXPECT_EQ(1u, session_->GetNumOpenIncomingStreams());
  EXPECT_TRUE(connection_->connected());
}

TEST_P(QuicSimpleServerSessionTest, CreateIncomingStreamDisconnected) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (GetParam() != AllSupportedVersions()[0]) {
    return;
  }

  // Tests that incoming stream creation fails when connection is not connected.
  size_t initial_num_open_stream = session_->GetNumOpenIncomingStreams();
  QuicConnectionPeer::TearDownLocalConnectionState(connection_);
  EXPECT_QUIC_BUG(QuicSimpleServerSessionPeer::CreateIncomingStream(
                      session_.get(), GetNthClientInitiatedBidirectionalId(0)),
                  "ShouldCreateIncomingStream called when disconnected");
  EXPECT_EQ(initial_num_open_stream, session_->GetNumOpenIncomingStreams());
}

TEST_P(QuicSimpleServerSessionTest, CreateEvenIncomingDynamicStream) {
  // Tests that incoming stream creation fails when given stream id is even.
  size_t initial_num_open_stream = session_->GetNumOpenIncomingStreams();
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID,
                              "Client created even numbered stream", _));
  QuicSimpleServerSessionPeer::CreateIncomingStream(
      session_.get(), GetNthServerInitiatedUnidirectionalId(0));
  EXPECT_EQ(initial_num_open_stream, session_->GetNumOpenIncomingStreams());
}

TEST_P(QuicSimpleServerSessionTest, CreateIncomingStream) {
  QuicSpdyStream* stream = QuicSimpleServerSessionPeer::CreateIncomingStream(
      session_.get(), GetNthClientInitiatedBidirectionalId(0));
  EXPECT_NE(nullptr, stream);
  EXPECT_EQ(GetNthClientInitiatedBidirectionalId(0), stream->id());
}

TEST_P(QuicSimpleServerSessionTest, CreateOutgoingDynamicStreamDisconnected) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (GetParam() != AllSupportedVersions()[0]) {
    return;
  }

  // Tests that outgoing stream creation fails when connection is not connected.
  size_t initial_num_open_stream = session_->GetNumOpenOutgoingStreams();
  QuicConnectionPeer::TearDownLocalConnectionState(connection_);
  EXPECT_QUIC_BUG(
      QuicSimpleServerSessionPeer::CreateOutgoingUnidirectionalStream(
          session_.get()),
      "ShouldCreateOutgoingUnidirectionalStream called when disconnected");

  EXPECT_EQ(initial_num_open_stream, session_->GetNumOpenOutgoingStreams());
}

TEST_P(QuicSimpleServerSessionTest, CreateOutgoingDynamicStreamUnencrypted) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (GetParam() != AllSupportedVersions()[0]) {
    return;
  }

  // Tests that outgoing stream creation fails when encryption has not yet been
  // established.
  size_t initial_num_open_stream = session_->GetNumOpenOutgoingStreams();
  EXPECT_QUIC_BUG(
      QuicSimpleServerSessionPeer::CreateOutgoingUnidirectionalStream(
          session_.get()),
      "Encryption not established so no outgoing stream created.");
  EXPECT_EQ(initial_num_open_stream, session_->GetNumOpenOutgoingStreams());
}

TEST_P(QuicSimpleServerSessionTest, CreateOutgoingDynamicStreamUptoLimit) {
  // Tests that outgoing stream creation should not be affected by existing
  // incoming stream and vice-versa. But when reaching the limit of max outgoing
  // stream allowed, creation should fail.

  // Receive some data to initiate a incoming stream which should not effect
  // creating outgoing streams.
  QuicStreamFrame data1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        QuicStringPiece("HT"));
  session_->OnStreamFrame(data1);
  EXPECT_EQ(1u, session_->GetNumOpenIncomingStreams());
  EXPECT_EQ(0u, session_->GetNumOpenOutgoingStreams());

  if (!VersionUsesQpack(connection_->transport_version())) {
    session_->UnregisterStreamPriority(
        QuicUtils::GetHeadersStreamId(connection_->transport_version()),
        /*is_static=*/true);
  }
  // Assume encryption already established.
  QuicSimpleServerSessionPeer::SetCryptoStream(session_.get(), nullptr);
  MockQuicCryptoServerStream* crypto_stream =
      new MockQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                     session_.get(), &stream_helper_);
  crypto_stream->set_encryption_established(true);
  QuicSimpleServerSessionPeer::SetCryptoStream(session_.get(), crypto_stream);
  if (!VersionUsesQpack(connection_->transport_version())) {
    session_->RegisterStreamPriority(
        QuicUtils::GetHeadersStreamId(connection_->transport_version()),
        /*is_static=*/true,
        spdy::SpdyStreamPrecedence(QuicStream::kDefaultPriority));
  }

  // Create push streams till reaching the upper limit of allowed open streams.
  for (size_t i = 0; i < kMaxStreamsForTest; ++i) {
    QuicSpdyStream* created_stream =
        QuicSimpleServerSessionPeer::CreateOutgoingUnidirectionalStream(
            session_.get());
    if (VersionHasStreamType(connection_->transport_version())) {
      EXPECT_EQ(GetNthServerInitiatedUnidirectionalId(i + 3),
                created_stream->id());
    } else {
      EXPECT_EQ(GetNthServerInitiatedUnidirectionalId(i), created_stream->id());
    }
    EXPECT_EQ(i + 1, session_->GetNumOpenOutgoingStreams());
  }

  // Continuing creating push stream would fail.
  EXPECT_EQ(nullptr,
            QuicSimpleServerSessionPeer::CreateOutgoingUnidirectionalStream(
                session_.get()));
  EXPECT_EQ(kMaxStreamsForTest, session_->GetNumOpenOutgoingStreams());

  // Create peer initiated stream should have no problem.
  QuicStreamFrame data2(GetNthClientInitiatedBidirectionalId(1), false, 0,
                        QuicStringPiece("HT"));
  session_->OnStreamFrame(data2);
  EXPECT_EQ(2u, session_->GetNumOpenIncomingStreams());
}

TEST_P(QuicSimpleServerSessionTest, OnStreamFrameWithEvenStreamId) {
  QuicStreamFrame frame(GetNthServerInitiatedUnidirectionalId(0), false, 0,
                        QuicStringPiece());
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_STREAM_ID,
                              "Client sent data on server push stream", _));
  session_->OnStreamFrame(frame);
}

TEST_P(QuicSimpleServerSessionTest, GetEvenIncomingError) {
  // Tests that calling GetOrCreateStream() on an outgoing stream not
  // promised yet should result close connection.
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_STREAM_ID,
                                            "Data for nonexistent stream", _));
  EXPECT_EQ(nullptr,
            QuicSessionPeer::GetOrCreateStream(
                session_.get(), GetNthServerInitiatedUnidirectionalId(3)));
}

// In order to test the case where server push stream creation goes beyond
// limit, server push streams need to be hanging there instead of
// immediately closing after sending back response.
// To achieve this goal, this class resets flow control windows so that large
// responses will not be sent fully in order to prevent push streams from being
// closed immediately.
// Also adjust connection-level flow control window to ensure a large response
// can cause stream-level flow control blocked but not connection-level.
class QuicSimpleServerSessionServerPushTest
    : public QuicSimpleServerSessionTest {
 protected:
  const size_t kStreamFlowControlWindowSize = 32 * 1024;  // 32KB.

  QuicSimpleServerSessionServerPushTest() {
    // Reset stream level flow control window to be 32KB.
    QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(
        &config_, kStreamFlowControlWindowSize);
    // Reset connection level flow control window to be 1.5 MB which is large
    // enough that it won't block any stream to write before stream level flow
    // control blocks it.
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        &config_, kInitialSessionFlowControlWindowForTest);

    ParsedQuicVersionVector supported_versions = SupportedVersions(GetParam());
    connection_ = new StrictMock<MockQuicConnectionWithSendStreamData>(
        &helper_, &alarm_factory_, Perspective::IS_SERVER, supported_versions);
    session_ = QuicMakeUnique<MockQuicSimpleServerSession>(
        config_, connection_, &owner_, &stream_helper_, &crypto_config_,
        &compressed_certs_cache_, &memory_cache_backend_);
    session_->Initialize();
    QuicSessionPeer::GetMutableCryptoStream(session_.get())
        ->OnSuccessfulVersionNegotiation(supported_versions.front());
    // Needed to make new session flow control window and server push work.

    if (VersionHasIetfQuicFrames(transport_version())) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillRepeatedly(Invoke(this, &QuicSimpleServerSessionServerPushTest::
                                           ClearMaxStreamsControlFrame));
    }
    session_->OnConfigNegotiated();

    visitor_ = QuicConnectionPeer::GetVisitor(connection_);

    if (!VersionUsesQpack(connection_->transport_version())) {
      session_->UnregisterStreamPriority(
          QuicUtils::GetHeadersStreamId(connection_->transport_version()),
          /*is_static=*/true);
    }
    QuicSimpleServerSessionPeer::SetCryptoStream(session_.get(), nullptr);
    // Assume encryption already established.
    MockQuicCryptoServerStream* crypto_stream = new MockQuicCryptoServerStream(
        &crypto_config_, &compressed_certs_cache_, session_.get(),
        &stream_helper_);

    crypto_stream->set_encryption_established(true);
    QuicSimpleServerSessionPeer::SetCryptoStream(session_.get(), crypto_stream);
    if (!VersionUsesQpack(connection_->transport_version())) {
      session_->RegisterStreamPriority(
          QuicUtils::GetHeadersStreamId(connection_->transport_version()),
          /*is_static=*/true,
          spdy::SpdyStreamPrecedence(QuicStream::kDefaultPriority));
    }
  }

  // Given |num_resources|, create this number of fake push resources and push
  // them by sending PUSH_PROMISE for all and sending push responses for as much
  // as possible(limited by kMaxStreamsForTest).
  // If |num_resources| > kMaxStreamsForTest, the left over will be queued.
  // Returns the length of the DATA frame header, or 0 if the version does not
  // use DATA frames.
  QuicByteCount PromisePushResources(size_t num_resources) {
    // testing::InSequence seq;
    // To prevent push streams from being closed the response need to be larger
    // than stream flow control window so stream won't send the full body.
    size_t body_size = 2 * kStreamFlowControlWindowSize;  // 64KB.

    std::string request_url = "mail.google.com/";
    spdy::SpdyHeaderBlock request_headers;
    std::string resource_host = "www.google.com";
    std::string partial_push_resource_path = "/server_push_src";
    std::list<QuicBackendResponse::ServerPushInfo> push_resources;
    std::string scheme = "http";
    QuicByteCount data_frame_header_length = 0;
    for (unsigned int i = 1; i <= num_resources; ++i) {
      QuicStreamId stream_id;
      if (VersionHasStreamType(connection_->transport_version())) {
        stream_id = GetNthServerInitiatedUnidirectionalId(i + 2);
      } else {
        stream_id = GetNthServerInitiatedUnidirectionalId(i - 1);
      }
      std::string path =
          partial_push_resource_path + QuicTextUtils::Uint64ToString(i);
      std::string url = scheme + "://" + resource_host + path;
      QuicUrl resource_url = QuicUrl(url);
      std::string body(body_size, 'a');
      std::string data;
      data_frame_header_length = 0;
      if (VersionHasDataFrameHeader(connection_->transport_version())) {
        HttpEncoder encoder;
        std::unique_ptr<char[]> buffer;
        data_frame_header_length =
            encoder.SerializeDataFrameHeader(body.length(), &buffer);
        std::string header(buffer.get(), data_frame_header_length);
        data = header + body;
      } else {
        data = body;
      }

      memory_cache_backend_.AddSimpleResponse(resource_host, path, 200, data);
      push_resources.push_back(QuicBackendResponse::ServerPushInfo(
          resource_url, spdy::SpdyHeaderBlock(), QuicStream::kDefaultPriority,
          body));
      // PUSH_PROMISED are sent for all the resources.
      EXPECT_CALL(*session_,
                  WritePushPromiseMock(GetNthClientInitiatedBidirectionalId(0),
                                       stream_id, _));
      if (i <= kMaxStreamsForTest) {
        // |kMaxStreamsForTest| promised responses should be sent.
        // Since flow control window is smaller than response body, not the
        // whole body will be sent.
        QuicStreamOffset offset = 0;
        if (VersionHasStreamType(connection_->transport_version())) {
          EXPECT_CALL(*connection_,
                      SendStreamData(stream_id, 1, offset, NO_FIN));
          offset++;
        }

        if (VersionUsesQpack(connection_->transport_version())) {
          EXPECT_CALL(*connection_,
                      SendStreamData(stream_id, kHeadersFrameHeaderLength,
                                     offset, NO_FIN));
          offset += kHeadersFrameHeaderLength;
          EXPECT_CALL(*connection_,
                      SendStreamData(stream_id, kHeadersFramePayloadLength,
                                     offset, NO_FIN));
          offset += kHeadersFramePayloadLength;
        }
        if (VersionHasDataFrameHeader(connection_->transport_version())) {
          EXPECT_CALL(*connection_,
                      SendStreamData(stream_id, data_frame_header_length,
                                     offset, NO_FIN));
          offset += data_frame_header_length;
        }
        EXPECT_CALL(*connection_, SendStreamData(stream_id, _, offset, NO_FIN))
            .WillOnce(Return(QuicConsumedData(
                kStreamFlowControlWindowSize - offset, false)));
        EXPECT_CALL(*session_, SendBlocked(stream_id));
      }
    }
    session_->PromisePushResources(
        request_url, push_resources, GetNthClientInitiatedBidirectionalId(0),
        spdy::SpdyStreamPrecedence(0, spdy::kHttp2DefaultStreamWeight, false),
        request_headers);
    return data_frame_header_length;
  }

  void MaybeConsumeHeadersStreamData() {
    if (!VersionUsesQpack(connection_->transport_version())) {
      QuicStreamId headers_stream_id =
          QuicUtils::GetHeadersStreamId(connection_->transport_version());
      EXPECT_CALL(*connection_, SendStreamData(headers_stream_id, _, _, _))
          .Times(AtLeast(1));
    }
  }
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSimpleServerSessionServerPushTest,
                         ::testing::ValuesIn(AllSupportedVersions()));

// Tests that given more than kMaxStreamsForTest resources, all their
// PUSH_PROMISE's will be sent out and only kMaxStreamsForTest streams will be
// opened and send push response.
TEST_P(QuicSimpleServerSessionServerPushTest, TestPromisePushResources) {
  MaybeConsumeHeadersStreamData();
  session_->set_max_allowed_push_id(kMaxQuicStreamId);
  size_t num_resources = kMaxStreamsForTest + 5;
  PromisePushResources(num_resources);
  EXPECT_EQ(kMaxStreamsForTest, session_->GetNumOpenOutgoingStreams());
}

// Tests that after promised stream queued up, when an opened stream is marked
// draining, a queued promised stream will become open and send push response.
TEST_P(QuicSimpleServerSessionServerPushTest,
       HandlePromisedPushRequestsAfterStreamDraining) {
  MaybeConsumeHeadersStreamData();
  session_->set_max_allowed_push_id(kMaxQuicStreamId);
  size_t num_resources = kMaxStreamsForTest + 1;
  QuicByteCount data_frame_header_length = PromisePushResources(num_resources);
  QuicStreamId next_out_going_stream_id;
  if (VersionHasStreamType(connection_->transport_version())) {
    next_out_going_stream_id =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest + 3);
  } else {
    next_out_going_stream_id =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest);
  }

  // After an open stream is marked draining, a new stream is expected to be
  // created and a response sent on the stream.
  QuicStreamOffset offset = 0;
  if (VersionHasStreamType(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(next_out_going_stream_id, 1, offset, NO_FIN));
    offset++;
  }
  if (VersionUsesQpack(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(next_out_going_stream_id,
                               kHeadersFrameHeaderLength, offset, NO_FIN));
    offset += kHeadersFrameHeaderLength;
    EXPECT_CALL(*connection_,
                SendStreamData(next_out_going_stream_id,
                               kHeadersFramePayloadLength, offset, NO_FIN));
    offset += kHeadersFramePayloadLength;
  }
  if (VersionHasDataFrameHeader(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(next_out_going_stream_id,
                               data_frame_header_length, offset, NO_FIN));
    offset += data_frame_header_length;
  }
  EXPECT_CALL(*connection_,
              SendStreamData(next_out_going_stream_id, _, offset, NO_FIN))
      .WillOnce(Return(
          QuicConsumedData(kStreamFlowControlWindowSize - offset, false)));
  EXPECT_CALL(*session_, SendBlocked(next_out_going_stream_id));

  if (VersionHasIetfQuicFrames(transport_version())) {
    // The PromisePushedResources call, above, will have used all available
    // stream ids.  For version 99, stream ids are not made available until
    // a MAX_STREAMS frame is received. This emulates the reception of one.
    // For pre-v-99, the node monitors its own stream usage and makes streams
    // available as it closes/etc them.
    // Version 99 also has unidirectional static streams, so we need to send
    // MaxStreamFrame of the number of resources + number of static streams.
    session_->OnMaxStreamsFrame(
        QuicMaxStreamsFrame(0, num_resources + 3, /*unidirectional=*/true));
  }

  if (VersionHasStreamType(connection_->transport_version())) {
    session_->StreamDraining(GetNthServerInitiatedUnidirectionalId(3));
  } else {
    session_->StreamDraining(GetNthServerInitiatedUnidirectionalId(0));
  }
  // Number of open outgoing streams should still be the same, because a new
  // stream is opened. And the queue should be empty.
  EXPECT_EQ(kMaxStreamsForTest, session_->GetNumOpenOutgoingStreams());
}

// Tests that after all resources are promised, a RST frame from client can
// prevent a promised resource to be send out.
TEST_P(QuicSimpleServerSessionServerPushTest,
       ResetPromisedStreamToCancelServerPush) {
  MaybeConsumeHeadersStreamData();
  session_->set_max_allowed_push_id(kMaxQuicStreamId);

  // Having two extra resources to be send later. One of them will be reset, so
  // when opened stream become close, only one will become open.
  size_t num_resources = kMaxStreamsForTest + 2;
  if (VersionHasIetfQuicFrames(transport_version())) {
    // V99 will send out a STREAMS_BLOCKED frame when it tries to exceed the
    // limit. This will clear the frames so that they do not block the later
    // rst-stream frame.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(&ClearControlFrame));
  }
  QuicByteCount data_frame_header_length = PromisePushResources(num_resources);

  // Reset the last stream in the queue. It should be marked cancelled.
  QuicStreamId stream_got_reset;
  if (VersionHasStreamType(connection_->transport_version())) {
    stream_got_reset =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest + 4);
  } else {
    stream_got_reset =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest + 1);
  }
  QuicRstStreamFrame rst(kInvalidControlFrameId, stream_got_reset,
                         QUIC_STREAM_CANCELLED, 0);
  EXPECT_CALL(owner_, OnRstStreamReceived(_)).Times(1);
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .WillOnce(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_,
              OnStreamReset(stream_got_reset, QUIC_RST_ACKNOWLEDGEMENT));
  visitor_->OnRstStream(rst);

  // When the first 2 streams becomes draining, the two queued up stream could
  // be created. But since one of them was marked cancelled due to RST frame,
  // only one queued resource will be sent out.
  QuicStreamId stream_not_reset;
  if (VersionHasStreamType(connection_->transport_version())) {
    stream_not_reset =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest + 3);
  } else {
    stream_not_reset =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest);
  }
  InSequence s;
  QuicStreamOffset offset = 0;
  if (VersionHasStreamType(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_not_reset, 1, offset, NO_FIN));
    offset++;
  }
  if (VersionUsesQpack(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_not_reset, kHeadersFrameHeaderLength,
                               offset, NO_FIN));
    offset += kHeadersFrameHeaderLength;
    EXPECT_CALL(*connection_,
                SendStreamData(stream_not_reset, kHeadersFramePayloadLength,
                               offset, NO_FIN));
    offset += kHeadersFramePayloadLength;
  }
  if (VersionHasDataFrameHeader(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_not_reset, data_frame_header_length,
                               offset, NO_FIN));
    offset += data_frame_header_length;
  }
  EXPECT_CALL(*connection_, SendStreamData(stream_not_reset, _, offset, NO_FIN))
      .WillOnce(Return(
          QuicConsumedData(kStreamFlowControlWindowSize - offset, false)));
  EXPECT_CALL(*session_, SendBlocked(stream_not_reset));

  if (VersionHasIetfQuicFrames(transport_version())) {
    // The PromisePushedResources call, above, will have used all available
    // stream ids.  For version 99, stream ids are not made available until
    // a MAX_STREAMS frame is received. This emulates the reception of one.
    // For pre-v-99, the node monitors its own stream usage and makes streams
    // available as it closes/etc them.
    session_->OnMaxStreamsFrame(
        QuicMaxStreamsFrame(0, num_resources + 3, /*unidirectional=*/true));
  }
  session_->StreamDraining(GetNthServerInitiatedUnidirectionalId(3));
  session_->StreamDraining(GetNthServerInitiatedUnidirectionalId(4));
}

// Tests that closing a open outgoing stream can trigger a promised resource in
// the queue to be send out.
TEST_P(QuicSimpleServerSessionServerPushTest,
       CloseStreamToHandleMorePromisedStream) {
  MaybeConsumeHeadersStreamData();
  session_->set_max_allowed_push_id(kMaxQuicStreamId);
  size_t num_resources = kMaxStreamsForTest + 1;
  if (VersionHasIetfQuicFrames(transport_version())) {
    // V99 will send out a stream-id-blocked frame when the we desired to exceed
    // the limit. This will clear the frames so that they do not block the later
    // rst-stream frame.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(&ClearControlFrame));
  }
  QuicByteCount data_frame_header_length = PromisePushResources(num_resources);
  QuicStreamId stream_to_open;
  if (VersionHasStreamType(connection_->transport_version())) {
    stream_to_open =
        GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest + 3);
  } else {
    stream_to_open = GetNthServerInitiatedUnidirectionalId(kMaxStreamsForTest);
  }

  // Resetting an open stream will close the stream and give space for extra
  // stream to be opened.
  QuicStreamId stream_got_reset = GetNthServerInitiatedUnidirectionalId(3);
  EXPECT_CALL(owner_, OnRstStreamReceived(_)).Times(1);
  EXPECT_CALL(*connection_, SendControlFrame(_));
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // For version 99, this is covered in InjectStopSending()
    EXPECT_CALL(*connection_,
                OnStreamReset(stream_got_reset, QUIC_RST_ACKNOWLEDGEMENT));
  }
  QuicStreamOffset offset = 0;
  if (VersionHasStreamType(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_to_open, 1, offset, NO_FIN));
    offset++;
  }
  if (VersionUsesQpack(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_to_open, kHeadersFrameHeaderLength,
                               offset, NO_FIN));
    offset += kHeadersFrameHeaderLength;
    EXPECT_CALL(*connection_,
                SendStreamData(stream_to_open, kHeadersFramePayloadLength,
                               offset, NO_FIN));
    offset += kHeadersFramePayloadLength;
  }
  if (VersionHasDataFrameHeader(connection_->transport_version())) {
    EXPECT_CALL(*connection_,
                SendStreamData(stream_to_open, data_frame_header_length, offset,
                               NO_FIN));
    offset += data_frame_header_length;
  }
  EXPECT_CALL(*connection_, SendStreamData(stream_to_open, _, offset, NO_FIN))
      .WillOnce(Return(
          QuicConsumedData(kStreamFlowControlWindowSize - offset, false)));

  EXPECT_CALL(*session_, SendBlocked(stream_to_open));
  QuicRstStreamFrame rst(kInvalidControlFrameId, stream_got_reset,
                         QUIC_STREAM_CANCELLED, 0);
  if (VersionHasIetfQuicFrames(transport_version())) {
    // The PromisePushedResources call, above, will have used all available
    // stream ids.  For version 99, stream ids are not made available until
    // a MAX_STREAMS frame is received. This emulates the reception of one.
    // For pre-v-99, the node monitors its own stream usage and makes streams
    // available as it closes/etc them.
    session_->OnMaxStreamsFrame(
        QuicMaxStreamsFrame(0, num_resources + 3, /*unidirectional=*/true));
  }
  visitor_->OnRstStream(rst);
  // Create and inject a STOP_SENDING frame. In GOOGLE QUIC, receiving a
  // RST_STREAM frame causes a two-way close. For IETF QUIC, RST_STREAM causes
  // a one-way close.
  InjectStopSending(stream_got_reset, QUIC_STREAM_CANCELLED);
}

}  // namespace
}  // namespace test
}  // namespace quic
