// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/http_frames.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_server_push_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_creator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_session_cache.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using spdy::SpdyHeaderBlock;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::Truly;

namespace quic {
namespace test {
namespace {

const char kServerHostname[] = "test.example.com";
const uint16_t kPort = 443;

class TestQuicSpdyClientSession : public QuicSpdyClientSession {
 public:
  explicit TestQuicSpdyClientSession(
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection,
      const QuicServerId& server_id,
      QuicCryptoClientConfig* crypto_config,
      QuicClientPushPromiseIndex* push_promise_index)
      : QuicSpdyClientSession(config,
                              supported_versions,
                              connection,
                              server_id,
                              crypto_config,
                              push_promise_index) {}

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override {
    return std::make_unique<MockQuicSpdyClientStream>(
        GetNextOutgoingBidirectionalStreamId(), this, BIDIRECTIONAL);
  }

  MockQuicSpdyClientStream* CreateIncomingStream(QuicStreamId id) override {
    if (!ShouldCreateIncomingStream(id)) {
      return nullptr;
    }
    MockQuicSpdyClientStream* stream =
        new MockQuicSpdyClientStream(id, this, READ_UNIDIRECTIONAL);
    ActivateStream(QuicWrapUnique(stream));
    return stream;
  }
};

class QuicSpdyClientSessionTest : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  QuicSpdyClientSessionTest()
      : promised_stream_id_(
            QuicUtils::GetInvalidStreamId(GetParam().transport_version)),
        associated_stream_id_(
            QuicUtils::GetInvalidStreamId(GetParam().transport_version)) {
    auto client_cache = std::make_unique<test::SimpleSessionCache>();
    client_session_cache_ = client_cache.get();
    SetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2, true);
    client_crypto_config_ = std::make_unique<QuicCryptoClientConfig>(
        crypto_test_utils::ProofVerifierForTesting(), std::move(client_cache));
    server_crypto_config_ = crypto_test_utils::CryptoServerConfigForTesting();
    Initialize();
    // Advance the time, because timers do not like uninitialized times.
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }

  ~QuicSpdyClientSessionTest() override {
    // Session must be destroyed before promised_by_url_
    session_.reset(nullptr);
  }

  void Initialize() {
    session_.reset();
    connection_ = new ::testing::NiceMock<PacketSavingConnection>(
        &helper_, &alarm_factory_, Perspective::IS_CLIENT,
        SupportedVersions(GetParam()));
    session_ = std::make_unique<TestQuicSpdyClientSession>(
        DefaultQuicConfig(), SupportedVersions(GetParam()), connection_,
        QuicServerId(kServerHostname, kPort, false),
        client_crypto_config_.get(), &push_promise_index_);
    session_->Initialize();
    crypto_stream_ = static_cast<QuicCryptoClientStream*>(
        session_->GetMutableCryptoStream());
    push_promise_[":path"] = "/bar";
    push_promise_[":authority"] = "www.google.com";
    push_promise_[":method"] = "GET";
    push_promise_[":scheme"] = "https";
    promise_url_ =
        SpdyServerPushUtils::GetPromisedUrlFromHeaders(push_promise_);
    promised_stream_id_ = GetNthServerInitiatedUnidirectionalStreamId(
        connection_->transport_version(), 0);
    associated_stream_id_ = GetNthClientInitiatedBidirectionalStreamId(
        connection_->transport_version(), 0);
  }

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

 public:
  bool ClearStreamsBlockedControlFrame(const QuicFrame& frame) {
    if (frame.type == STREAMS_BLOCKED_FRAME) {
      DeleteFrame(&const_cast<QuicFrame&>(frame));
      return true;
    }
    return false;
  }

 protected:
  void CompleteCryptoHandshake() {
    CompleteCryptoHandshake(kDefaultMaxStreamsPerConnection);
  }

  void CompleteCryptoHandshake(uint32_t server_max_incoming_streams) {
    if (VersionHasIetfQuicFrames(connection_->transport_version())) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .Times(::testing::AnyNumber())
          .WillRepeatedly(Invoke(
              this, &QuicSpdyClientSessionTest::ClearMaxStreamsControlFrame));
    }
    session_->CryptoConnect();
    QuicConfig config = DefaultQuicConfig();
    if (VersionHasIetfQuicFrames(connection_->transport_version())) {
      config.SetMaxUnidirectionalStreamsToSend(server_max_incoming_streams);
      config.SetMaxBidirectionalStreamsToSend(server_max_incoming_streams);
    } else {
      config.SetMaxBidirectionalStreamsToSend(server_max_incoming_streams);
    }
    crypto_test_utils::HandshakeWithFakeServer(
        &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
        connection_, crypto_stream_, AlpnForVersion(connection_->version()));
  }

  void CreateConnection() {
    connection_ = new ::testing::NiceMock<PacketSavingConnection>(
        &helper_, &alarm_factory_, Perspective::IS_CLIENT,
        SupportedVersions(GetParam()));
    // Advance the time, because timers do not like uninitialized times.
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
    session_ = std::make_unique<TestQuicSpdyClientSession>(
        DefaultQuicConfig(), SupportedVersions(GetParam()), connection_,
        QuicServerId(kServerHostname, kPort, false),
        client_crypto_config_.get(), &push_promise_index_);
    session_->Initialize();
    crypto_stream_ = static_cast<QuicCryptoClientStream*>(
        session_->GetMutableCryptoStream());
  }

  void CompleteFirstConnection() {
    CompleteCryptoHandshake();
    EXPECT_FALSE(session_->GetCryptoStream()->IsResumption());
    if (session_->version().UsesHttp3()) {
      SettingsFrame settings;
      settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 2;
      settings.values[SETTINGS_MAX_FIELD_SECTION_SIZE] = 5;
      settings.values[256] = 4;  // unknown setting
      session_->OnSettingsFrame(settings);
    }
  }

  // Owned by |session_|.
  QuicCryptoClientStream* crypto_stream_;
  std::unique_ptr<QuicCryptoServerConfig> server_crypto_config_;
  std::unique_ptr<QuicCryptoClientConfig> client_crypto_config_;
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  ::testing::NiceMock<PacketSavingConnection>* connection_;
  std::unique_ptr<TestQuicSpdyClientSession> session_;
  QuicClientPushPromiseIndex push_promise_index_;
  SpdyHeaderBlock push_promise_;
  std::string promise_url_;
  QuicStreamId promised_stream_id_;
  QuicStreamId associated_stream_id_;
  test::SimpleSessionCache* client_session_cache_;
};

std::string ParamNameFormatter(
    const testing::TestParamInfo<QuicSpdyClientSessionTest::ParamType>& info) {
  const ParsedQuicVersion& version = info.param;
  return quiche::QuicheStrCat(
      QuicVersionToString(version.transport_version), "_",
      HandshakeProtocolToString(version.handshake_protocol));
}

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdyClientSessionTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ParamNameFormatter);

TEST_P(QuicSpdyClientSessionTest, CryptoConnect) {
  CompleteCryptoHandshake();
}

TEST_P(QuicSpdyClientSessionTest, NoEncryptionAfterInitialEncryption) {
  if (GetParam().handshake_protocol == PROTOCOL_TLS1_3) {
    // This test relies on resumption and is QUIC crypto specific, so it is
    // disabled for TLS.
    return;
  }
  // Complete a handshake in order to prime the crypto config for 0-RTT.
  CompleteCryptoHandshake();

  // Now create a second session using the same crypto config.
  Initialize();

  // Starting the handshake should move immediately to encryption
  // established and will allow streams to be created.
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream != nullptr);
  EXPECT_FALSE(QuicUtils::IsCryptoStreamId(connection_->transport_version(),
                                           stream->id()));

  // Process an "inchoate" REJ from the server which will cause
  // an inchoate CHLO to be sent and will leave the encryption level
  // at NONE.
  CryptoHandshakeMessage rej;
  crypto_test_utils::FillInDummyReject(&rej);
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  crypto_test_utils::SendHandshakeMessageToStream(
      session_->GetMutableCryptoStream(), rej, Perspective::IS_CLIENT);
  EXPECT_FALSE(session_->IsEncryptionEstablished());
  EXPECT_EQ(ENCRYPTION_INITIAL,
            QuicPacketCreatorPeer::GetEncryptionLevel(
                QuicConnectionPeer::GetPacketCreator(connection_)));
  // Verify that no new streams may be created.
  EXPECT_TRUE(session_->CreateOutgoingBidirectionalStream() == nullptr);
  // Verify that no data may be send on existing streams.
  char data[] = "hello world";
  EXPECT_QUIC_BUG(
      session_->WritevData(stream->id(), QUICHE_ARRAYSIZE(data), 0, NO_FIN,
                           NOT_RETRANSMISSION, QUICHE_NULLOPT),
      "Client: Try to send data of stream");
}

TEST_P(QuicSpdyClientSessionTest, MaxNumStreamsWithNoFinOrRst) {
  uint32_t kServerMaxIncomingStreams = 1;
  CompleteCryptoHandshake(kServerMaxIncomingStreams);

  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream);
  EXPECT_FALSE(session_->CreateOutgoingBidirectionalStream());

  // Close the stream, but without having received a FIN or a RST_STREAM
  // or MAX_STREAMS (V99) and check that a new one can not be created.
  session_->ResetStream(stream->id(), QUIC_STREAM_CANCELLED);
  EXPECT_EQ(1u, QuicSessionPeer::GetNumOpenDynamicStreams(session_.get()));

  stream = session_->CreateOutgoingBidirectionalStream();
  EXPECT_FALSE(stream);
}

TEST_P(QuicSpdyClientSessionTest, MaxNumStreamsWithRst) {
  uint32_t kServerMaxIncomingStreams = 1;
  CompleteCryptoHandshake(kServerMaxIncomingStreams);

  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_NE(nullptr, stream);
  EXPECT_EQ(nullptr, session_->CreateOutgoingBidirectionalStream());

  // Close the stream and receive an RST frame to remove the unfinished stream
  session_->ResetStream(stream->id(), QUIC_STREAM_CANCELLED);
  session_->OnRstStream(QuicRstStreamFrame(kInvalidControlFrameId, stream->id(),
                                           QUIC_RST_ACKNOWLEDGEMENT, 0));
  // Check that a new one can be created.
  EXPECT_EQ(0u, QuicSessionPeer::GetNumOpenDynamicStreams(session_.get()));
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // In V99 the stream limit increases only if we get a MAX_STREAMS
    // frame; pretend we got one.

    QuicMaxStreamsFrame frame(0, 2,
                              /*unidirectional=*/false);
    session_->OnMaxStreamsFrame(frame);
  }
  stream = session_->CreateOutgoingBidirectionalStream();
  EXPECT_NE(nullptr, stream);
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // Ensure that we have 2 total streams, 1 open and 1 closed.
    QuicStreamCount expected_stream_count = 2;
    EXPECT_EQ(expected_stream_count,
              QuicSessionPeer::v99_bidirectional_stream_id_manager(&*session_)
                  ->outgoing_stream_count());
  }
}

TEST_P(QuicSpdyClientSessionTest, ResetAndTrailers) {
  // Tests the situation in which the client sends a RST at the same time that
  // the server sends trailing headers (trailers). Receipt of the trailers by
  // the client should result in all outstanding stream state being tidied up
  // (including flow control, and number of available outgoing streams).
  uint32_t kServerMaxIncomingStreams = 1;
  CompleteCryptoHandshake(kServerMaxIncomingStreams);

  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_NE(nullptr, stream);

  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // For v99, trying to open a stream and failing due to lack
    // of stream ids will result in a STREAMS_BLOCKED. Make
    // sure we get one. Also clear out the frame because if it's
    // left sitting, the later SendRstStream will not actually
    // transmit the RST_STREAM because the connection will be in write-blocked
    // state. This means that the SendControlFrame that is expected w.r.t. the
    // RST_STREAM, below, will not be satisfied.
    EXPECT_CALL(*connection_, SendControlFrame(_))
        .WillOnce(Invoke(
            this, &QuicSpdyClientSessionTest::ClearStreamsBlockedControlFrame));
  }

  EXPECT_EQ(nullptr, session_->CreateOutgoingBidirectionalStream());

  QuicStreamId stream_id = stream->id();

  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(AtLeast(1))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(_, _)).Times(1);
  session_->ResetStream(stream_id, QUIC_STREAM_PEER_GOING_AWAY);

  // A new stream cannot be created as the reset stream still counts as an open
  // outgoing stream until closed by the server.
  EXPECT_EQ(1u, QuicSessionPeer::GetNumOpenDynamicStreams(session_.get()));
  stream = session_->CreateOutgoingBidirectionalStream();
  EXPECT_EQ(nullptr, stream);

  // The stream receives trailers with final byte offset: this is one of three
  // ways that a peer can signal the end of a stream (the others being RST,
  // stream data + FIN).
  QuicHeaderList trailers;
  trailers.OnHeaderBlockStart();
  trailers.OnHeader(kFinalOffsetHeaderKey, "0");
  trailers.OnHeaderBlockEnd(0, 0);
  session_->OnStreamHeaderList(stream_id, /*fin=*/false, 0, trailers);

  // The stream is now complete from the client's perspective, and it should
  // be able to create a new outgoing stream.
  EXPECT_EQ(0u, QuicSessionPeer::GetNumOpenDynamicStreams(session_.get()));
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    QuicMaxStreamsFrame frame(0, 2,
                              /*unidirectional=*/false);

    session_->OnMaxStreamsFrame(frame);
  }
  stream = session_->CreateOutgoingBidirectionalStream();
  EXPECT_NE(nullptr, stream);
  if (VersionHasIetfQuicFrames(GetParam().transport_version)) {
    // Ensure that we have 2 open streams.
    QuicStreamCount expected_stream_count = 2;
    EXPECT_EQ(expected_stream_count,
              QuicSessionPeer::v99_bidirectional_stream_id_manager(&*session_)
                  ->outgoing_stream_count());
  }
}

TEST_P(QuicSpdyClientSessionTest, ReceivedMalformedTrailersAfterSendingRst) {
  // Tests the situation where the client has sent a RST to the server, and has
  // received trailing headers with a malformed final byte offset value.
  CompleteCryptoHandshake();

  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_NE(nullptr, stream);

  // Send the RST, which results in the stream being closed locally (but some
  // state remains while the client waits for a response from the server).
  QuicStreamId stream_id = stream->id();
  EXPECT_CALL(*connection_, SendControlFrame(_))
      .Times(AtLeast(1))
      .WillRepeatedly(Invoke(&ClearControlFrame));
  EXPECT_CALL(*connection_, OnStreamReset(_, _)).Times(1);
  session_->ResetStream(stream_id, QUIC_STREAM_PEER_GOING_AWAY);

  // The stream receives trailers with final byte offset, but the header value
  // is non-numeric and should be treated as malformed.
  QuicHeaderList trailers;
  trailers.OnHeaderBlockStart();
  trailers.OnHeader(kFinalOffsetHeaderKey, "invalid non-numeric value");
  trailers.OnHeaderBlockEnd(0, 0);

  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(1);
  session_->OnStreamHeaderList(stream_id, /*fin=*/false, 0, trailers);
}

TEST_P(QuicSpdyClientSessionTest, OnStreamHeaderListWithStaticStream) {
  // Test situation where OnStreamHeaderList is called by stream with static id.
  CompleteCryptoHandshake();

  QuicHeaderList trailers;
  trailers.OnHeaderBlockStart();
  trailers.OnHeader(kFinalOffsetHeaderKey, "0");
  trailers.OnHeaderBlockEnd(0, 0);

  // Initialize H/3 control stream.
  QuicStreamId id;
  if (VersionUsesHttp3(connection_->transport_version())) {
    id = GetNthServerInitiatedUnidirectionalStreamId(
        connection_->transport_version(), 3);
    char type[] = {0x00};

    QuicStreamFrame data1(id, false, 0, quiche::QuicheStringPiece(type, 1));
    session_->OnStreamFrame(data1);
  } else {
    id = QuicUtils::GetHeadersStreamId(connection_->transport_version());
  }

  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA,
                                            "stream is static", _))
      .Times(1);
  session_->OnStreamHeaderList(id,
                               /*fin=*/false, 0, trailers);
}

TEST_P(QuicSpdyClientSessionTest, OnPromiseHeaderListWithStaticStream) {
  // Test situation where OnPromiseHeaderList is called by stream with static
  // id.
  CompleteCryptoHandshake();

  QuicHeaderList trailers;
  trailers.OnHeaderBlockStart();
  trailers.OnHeader(kFinalOffsetHeaderKey, "0");
  trailers.OnHeaderBlockEnd(0, 0);

  // Initialize H/3 control stream.
  QuicStreamId id;
  if (VersionUsesHttp3(connection_->transport_version())) {
    id = GetNthServerInitiatedUnidirectionalStreamId(
        connection_->transport_version(), 3);
    char type[] = {0x00};

    QuicStreamFrame data1(id, false, 0, quiche::QuicheStringPiece(type, 1));
    session_->OnStreamFrame(data1);
  } else {
    id = QuicUtils::GetHeadersStreamId(connection_->transport_version());
  }
  EXPECT_CALL(*connection_, CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA,
                                            "stream is static", _))
      .Times(1);
  session_->OnPromiseHeaderList(id, promised_stream_id_, 0, trailers);
}

TEST_P(QuicSpdyClientSessionTest, GoAwayReceived) {
  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    return;
  }
  CompleteCryptoHandshake();

  // After receiving a GoAway, I should no longer be able to create outgoing
  // streams.
  session_->connection()->OnGoAwayFrame(QuicGoAwayFrame(
      kInvalidControlFrameId, QUIC_PEER_GOING_AWAY, 1u, "Going away."));
  EXPECT_EQ(nullptr, session_->CreateOutgoingBidirectionalStream());
}

static bool CheckForDecryptionError(QuicFramer* framer) {
  return framer->error() == QUIC_DECRYPTION_FAILURE;
}

// Various sorts of invalid packets that should not cause a connection
// to be closed.
TEST_P(QuicSpdyClientSessionTest, InvalidPacketReceived) {
  QuicSocketAddress server_address(TestPeerIPAddress(), kTestPort);
  QuicSocketAddress client_address(TestPeerIPAddress(), kTestPort);

  EXPECT_CALL(*connection_, ProcessUdpPacket(server_address, client_address, _))
      .WillRepeatedly(Invoke(static_cast<MockQuicConnection*>(connection_),
                             &MockQuicConnection::ReallyProcessUdpPacket));
  EXPECT_CALL(*connection_, OnCanWrite()).Times(AnyNumber());
  EXPECT_CALL(*connection_, OnError(_)).Times(1);

  // Verify that empty packets don't close the connection.
  QuicReceivedPacket zero_length_packet(nullptr, 0, QuicTime::Zero(), false);
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  session_->ProcessUdpPacket(client_address, server_address,
                             zero_length_packet);

  // Verifiy that small, invalid packets don't close the connection.
  char buf[2] = {0x00, 0x01};
  QuicConnectionId connection_id = session_->connection()->connection_id();
  QuicReceivedPacket valid_packet(buf, 2, QuicTime::Zero(), false);
  // Close connection shouldn't be called.
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*connection_, OnError(_)).Times(AtMost(1));
  session_->ProcessUdpPacket(client_address, server_address, valid_packet);

  // Verify that a non-decryptable packet doesn't close the connection.
  QuicFramerPeer::SetLastSerializedServerConnectionId(
      QuicConnectionPeer::GetFramer(connection_), connection_id);
  ParsedQuicVersionVector versions = SupportedVersions(GetParam());
  QuicConnectionId destination_connection_id = EmptyQuicConnectionId();
  QuicConnectionId source_connection_id = connection_id;
  std::unique_ptr<QuicEncryptedPacket> packet(ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, false, false, 100,
      "data", true, CONNECTION_ID_ABSENT, CONNECTION_ID_ABSENT,
      PACKET_4BYTE_PACKET_NUMBER, &versions, Perspective::IS_SERVER));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*packet, QuicTime::Zero()));
  // Change the last byte of the encrypted data.
  *(const_cast<char*>(received->data() + received->length() - 1)) += 1;
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*connection_, OnError(Truly(CheckForDecryptionError))).Times(1);
  session_->ProcessUdpPacket(client_address, server_address, *received);
}

// A packet with invalid framing should cause a connection to be closed.
TEST_P(QuicSpdyClientSessionTest, InvalidFramedPacketReceived) {
  const ParsedQuicVersion version = GetParam();
  QuicSocketAddress server_address(TestPeerIPAddress(), kTestPort);
  QuicSocketAddress client_address(TestPeerIPAddress(), kTestPort);
  if (version.KnowsWhichDecrypterToUse()) {
    connection_->InstallDecrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullDecrypter>(Perspective::IS_CLIENT));
  } else {
    connection_->SetDecrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullDecrypter>(Perspective::IS_CLIENT));
  }

  EXPECT_CALL(*connection_, ProcessUdpPacket(server_address, client_address, _))
      .WillRepeatedly(Invoke(static_cast<MockQuicConnection*>(connection_),
                             &MockQuicConnection::ReallyProcessUdpPacket));
  EXPECT_CALL(*connection_, OnError(_)).Times(1);

  // Verify that a decryptable packet with bad frames does close the connection.
  QuicConnectionId destination_connection_id =
      session_->connection()->connection_id();
  QuicConnectionId source_connection_id = EmptyQuicConnectionId();
  QuicFramerPeer::SetLastSerializedServerConnectionId(
      QuicConnectionPeer::GetFramer(connection_), destination_connection_id);
  bool version_flag = false;
  QuicConnectionIdIncluded scid_included = CONNECTION_ID_ABSENT;
  if (VersionHasIetfInvariantHeader(version.transport_version)) {
    version_flag = true;
    source_connection_id = destination_connection_id;
    scid_included = CONNECTION_ID_PRESENT;
  }
  std::unique_ptr<QuicEncryptedPacket> packet(ConstructMisFramedEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, false, 100,
      "data", CONNECTION_ID_ABSENT, scid_included, PACKET_4BYTE_PACKET_NUMBER,
      version, Perspective::IS_SERVER));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*packet, QuicTime::Zero()));
  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(1);
  session_->ProcessUdpPacket(client_address, server_address, *received);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseOnPromiseHeaders) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    session_->SetMaxPushId(10);
  }

  MockQuicSpdyClientStream* stream = static_cast<MockQuicSpdyClientStream*>(
      session_->CreateOutgoingBidirectionalStream());

  EXPECT_CALL(*stream, OnPromiseHeaderList(_, _, _));
  session_->OnPromiseHeaderList(associated_stream_id_, promised_stream_id_, 0,
                                QuicHeaderList());
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseStreamIdTooHigh) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  QuicStreamId stream_id =
      QuicSessionPeer::GetNextOutgoingBidirectionalStreamId(session_.get());
  QuicSessionPeer::ActivateStream(
      session_.get(), std::make_unique<QuicSpdyClientStream>(
                          stream_id, session_.get(), BIDIRECTIONAL));

  QuicHeaderList headers;
  headers.OnHeaderBlockStart();
  headers.OnHeader(":path", "/bar");
  headers.OnHeader(":authority", "www.google.com");
  headers.OnHeader(":method", "GET");
  headers.OnHeader(":scheme", "https");
  headers.OnHeaderBlockEnd(0, 0);

  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    session_->SetMaxPushId(10);
    // TODO(b/136295430) Use PushId to represent Push IDs instead of
    // QuicStreamId.
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INVALID_STREAM_ID,
                        "Received push stream id higher than MAX_PUSH_ID.", _));
    const PushId promise_id = 11;
    session_->OnPromiseHeaderList(stream_id, promise_id, 0, headers);
    return;
  }
  const QuicStreamId promise_id = GetNthServerInitiatedUnidirectionalStreamId(
      connection_->transport_version(), 11);
  session_->OnPromiseHeaderList(stream_id, promise_id, 0, headers);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseOnPromiseHeadersAlreadyClosed) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_REFUSED_STREAM));
  session_->ResetPromised(promised_stream_id_, QUIC_REFUSED_STREAM);

  session_->OnPromiseHeaderList(associated_stream_id_, promised_stream_id_, 0,
                                QuicHeaderList());
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseOutOfOrder) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    session_->SetMaxPushId(10);
  }

  MockQuicSpdyClientStream* stream = static_cast<MockQuicSpdyClientStream*>(
      session_->CreateOutgoingBidirectionalStream());

  EXPECT_CALL(*stream, OnPromiseHeaderList(promised_stream_id_, _, _));
  session_->OnPromiseHeaderList(associated_stream_id_, promised_stream_id_, 0,
                                QuicHeaderList());
  associated_stream_id_ +=
      QuicUtils::StreamIdDelta(connection_->transport_version());
  if (!VersionUsesHttp3(session_->transport_version())) {
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_INVALID_STREAM_ID,
                                "Received push stream id lesser or equal to the"
                                " last accepted before",
                                _));
  }
  session_->OnPromiseHeaderList(associated_stream_id_, promised_stream_id_, 0,
                                QuicHeaderList());
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseOutgoingStreamId) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  MockQuicSpdyClientStream* stream = static_cast<MockQuicSpdyClientStream*>(
      session_->CreateOutgoingBidirectionalStream());

  // Promise an illegal (outgoing) stream id.
  promised_stream_id_ = GetNthClientInitiatedBidirectionalStreamId(
      connection_->transport_version(), 0);
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_INVALID_STREAM_ID,
                      "Received push stream id for outgoing stream.", _));

  session_->OnPromiseHeaderList(stream->id(), promised_stream_id_, 0,
                                QuicHeaderList());
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseHandlePromise) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();

  EXPECT_TRUE(session_->HandlePromised(associated_stream_id_,
                                       promised_stream_id_, push_promise_));

  EXPECT_NE(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_NE(session_->GetPromisedByUrl(promise_url_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseAlreadyClosed) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();
  session_->GetOrCreateStream(promised_stream_id_);

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_REFUSED_STREAM));

  session_->ResetPromised(promised_stream_id_, QUIC_REFUSED_STREAM);
  SpdyHeaderBlock promise_headers;
  EXPECT_FALSE(session_->HandlePromised(associated_stream_id_,
                                        promised_stream_id_, promise_headers));

  // Verify that the promise was not created.
  EXPECT_EQ(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_EQ(session_->GetPromisedByUrl(promise_url_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseDuplicateUrl) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();

  EXPECT_TRUE(session_->HandlePromised(associated_stream_id_,
                                       promised_stream_id_, push_promise_));

  EXPECT_NE(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_NE(session_->GetPromisedByUrl(promise_url_), nullptr);

  promised_stream_id_ +=
      QuicUtils::StreamIdDelta(connection_->transport_version());
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_DUPLICATE_PROMISE_URL));

  EXPECT_FALSE(session_->HandlePromised(associated_stream_id_,
                                        promised_stream_id_, push_promise_));

  // Verify that the promise was not created.
  EXPECT_EQ(session_->GetPromisedById(promised_stream_id_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, ReceivingPromiseEnhanceYourCalm) {
  for (size_t i = 0u; i < session_->get_max_promises(); i++) {
    push_promise_[":path"] = quiche::QuicheStringPrintf("/bar%zu", i);

    QuicStreamId id =
        promised_stream_id_ +
        i * QuicUtils::StreamIdDelta(connection_->transport_version());

    EXPECT_TRUE(
        session_->HandlePromised(associated_stream_id_, id, push_promise_));

    // Verify that the promise is in the unclaimed streams map.
    std::string promise_url(
        SpdyServerPushUtils::GetPromisedUrlFromHeaders(push_promise_));
    EXPECT_NE(session_->GetPromisedByUrl(promise_url), nullptr);
    EXPECT_NE(session_->GetPromisedById(id), nullptr);
  }

  // One more promise, this should be refused.
  int i = session_->get_max_promises();
  push_promise_[":path"] = quiche::QuicheStringPrintf("/bar%d", i);

  QuicStreamId id =
      promised_stream_id_ +
      i * QuicUtils::StreamIdDelta(connection_->transport_version());
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_, OnStreamReset(id, QUIC_REFUSED_STREAM));
  EXPECT_FALSE(
      session_->HandlePromised(associated_stream_id_, id, push_promise_));

  // Verify that the promise was not created.
  std::string promise_url(
      SpdyServerPushUtils::GetPromisedUrlFromHeaders(push_promise_));
  EXPECT_EQ(session_->GetPromisedById(id), nullptr);
  EXPECT_EQ(session_->GetPromisedByUrl(promise_url), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, IsClosedTrueAfterResetPromisedAlreadyOpen) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->GetOrCreateStream(promised_stream_id_);
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_REFUSED_STREAM));
  session_->ResetPromised(promised_stream_id_, QUIC_REFUSED_STREAM);
  EXPECT_TRUE(session_->IsClosedStream(promised_stream_id_));
}

TEST_P(QuicSpdyClientSessionTest, IsClosedTrueAfterResetPromisedNonexistant) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_REFUSED_STREAM));
  session_->ResetPromised(promised_stream_id_, QUIC_REFUSED_STREAM);
  EXPECT_TRUE(session_->IsClosedStream(promised_stream_id_));
}

TEST_P(QuicSpdyClientSessionTest, OnInitialHeadersCompleteIsPush) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  session_->GetOrCreateStream(promised_stream_id_);
  EXPECT_TRUE(session_->HandlePromised(associated_stream_id_,
                                       promised_stream_id_, push_promise_));
  EXPECT_NE(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_NE(session_->GetPromisedStream(promised_stream_id_), nullptr);
  EXPECT_NE(session_->GetPromisedByUrl(promise_url_), nullptr);

  session_->OnInitialHeadersComplete(promised_stream_id_, SpdyHeaderBlock());
}

TEST_P(QuicSpdyClientSessionTest, OnInitialHeadersCompleteIsNotPush) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  session_->CreateOutgoingBidirectionalStream();
  session_->OnInitialHeadersComplete(promised_stream_id_, SpdyHeaderBlock());
}

TEST_P(QuicSpdyClientSessionTest, DeletePromised) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  session_->GetOrCreateStream(promised_stream_id_);
  EXPECT_TRUE(session_->HandlePromised(associated_stream_id_,
                                       promised_stream_id_, push_promise_));
  QuicClientPromisedInfo* promised =
      session_->GetPromisedById(promised_stream_id_);
  EXPECT_NE(promised, nullptr);
  EXPECT_NE(session_->GetPromisedStream(promised_stream_id_), nullptr);
  EXPECT_NE(session_->GetPromisedByUrl(promise_url_), nullptr);

  session_->DeletePromised(promised);
  EXPECT_EQ(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_EQ(session_->GetPromisedByUrl(promise_url_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, ResetPromised) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  session_->GetOrCreateStream(promised_stream_id_);
  EXPECT_TRUE(session_->HandlePromised(associated_stream_id_,
                                       promised_stream_id_, push_promise_));
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_STREAM_PEER_GOING_AWAY));
  session_->ResetStream(promised_stream_id_, QUIC_STREAM_PEER_GOING_AWAY);
  QuicClientPromisedInfo* promised =
      session_->GetPromisedById(promised_stream_id_);
  EXPECT_NE(promised, nullptr);
  EXPECT_NE(session_->GetPromisedByUrl(promise_url_), nullptr);
  EXPECT_EQ(session_->GetPromisedStream(promised_stream_id_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseInvalidMethod) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_INVALID_PROMISE_METHOD));

  push_promise_[":method"] = "POST";
  EXPECT_FALSE(session_->HandlePromised(associated_stream_id_,
                                        promised_stream_id_, push_promise_));

  EXPECT_EQ(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_EQ(session_->GetPromisedByUrl(promise_url_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest, PushPromiseInvalidHost) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();

  session_->CreateOutgoingBidirectionalStream();

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(promised_stream_id_, QUIC_INVALID_PROMISE_URL));

  push_promise_[":authority"] = "";
  EXPECT_FALSE(session_->HandlePromised(associated_stream_id_,
                                        promised_stream_id_, push_promise_));

  EXPECT_EQ(session_->GetPromisedById(promised_stream_id_), nullptr);
  EXPECT_EQ(session_->GetPromisedByUrl(promise_url_), nullptr);
}

TEST_P(QuicSpdyClientSessionTest,
       TryToCreateServerInitiatedBidirectionalStream) {
  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_HTTP_SERVER_INITIATED_BIDIRECTIONAL_STREAM, _, _));
  } else {
    EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  }
  session_->GetOrCreateStream(GetNthServerInitiatedBidirectionalStreamId(
      connection_->transport_version(), 0));
}

TEST_P(QuicSpdyClientSessionTest, TooManyPushPromises) {
  // Initialize crypto before the client session will create a stream.
  CompleteCryptoHandshake();
  QuicStreamId stream_id =
      QuicSessionPeer::GetNextOutgoingBidirectionalStreamId(session_.get());
  QuicSessionPeer::ActivateStream(
      session_.get(), std::make_unique<QuicSpdyClientStream>(
                          stream_id, session_.get(), BIDIRECTIONAL));

  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    session_->SetMaxPushId(kMaxQuicStreamId);
  }

  EXPECT_CALL(*connection_, OnStreamReset(_, QUIC_REFUSED_STREAM));

  for (size_t promise_count = 0; promise_count <= session_->get_max_promises();
       promise_count++) {
    auto promise_id = GetNthServerInitiatedUnidirectionalStreamId(
        connection_->transport_version(), promise_count);
    auto headers = QuicHeaderList();
    headers.OnHeaderBlockStart();
    headers.OnHeader(":path", quiche::QuicheStrCat("/", promise_count));
    headers.OnHeader(":authority", "www.google.com");
    headers.OnHeader(":method", "GET");
    headers.OnHeader(":scheme", "https");
    headers.OnHeaderBlockEnd(0, 0);
    session_->OnPromiseHeaderList(stream_id, promise_id, 0, headers);
  }
}

// Test that upon receiving HTTP/3 SETTINGS, the settings are serialized and
// stored into client session cache.
TEST_P(QuicSpdyClientSessionTest, OnSettingsFrame) {
  // This feature is HTTP/3 only
  if (!VersionUsesHttp3(session_->transport_version())) {
    return;
  }
  CompleteCryptoHandshake();
  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 2;
  settings.values[SETTINGS_MAX_FIELD_SECTION_SIZE] = 5;
  settings.values[256] = 4;   // unknown setting
  char application_state[] = {// type (SETTINGS)
                              0x04,
                              // length
                              0x07,
                              // identifier (SETTINGS_QPACK_MAX_TABLE_CAPACITY)
                              0x01,
                              // content
                              0x02,
                              // identifier (SETTINGS_MAX_FIELD_SECTION_SIZE)
                              0x06,
                              // content
                              0x05,
                              // identifier (256 in variable length integer)
                              0x40 + 0x01, 0x00,
                              // content
                              0x04};
  ApplicationState expected(std::begin(application_state),
                            std::end(application_state));
  session_->OnSettingsFrame(settings);
  EXPECT_EQ(expected,
            *client_session_cache_
                 ->Lookup(QuicServerId(kServerHostname, kPort, false), nullptr)
                 ->application_state);
}

TEST_P(QuicSpdyClientSessionTest, IetfZeroRttSetup) {
  // This feature is TLS-only.
  if (session_->version().UsesQuicCrypto()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  // Session configs should be in initial state.
  if (session_->version().UsesHttp3()) {
    EXPECT_EQ(0u, session_->flow_controller()->send_window_offset());
    EXPECT_EQ(std::numeric_limits<size_t>::max(),
              session_->max_outbound_header_list_size());
  } else {
    EXPECT_EQ(kMinimumFlowControlSendWindow,
              session_->flow_controller()->send_window_offset());
  }
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, session_->connection()->encryption_level());

  // The client session should have a basic setup ready before the handshake
  // succeeds.
  EXPECT_EQ(kInitialSessionFlowControlWindowForTest,
            session_->flow_controller()->send_window_offset());
  if (session_->version().UsesHttp3()) {
    auto* id_manager = QuicSessionPeer::v99_streamid_manager(session_.get());
    EXPECT_EQ(kDefaultMaxStreamsPerConnection,
              id_manager->max_outgoing_bidirectional_streams());
    EXPECT_EQ(
        kDefaultMaxStreamsPerConnection + kHttp3StaticUnidirectionalStreamCount,
        id_manager->max_outgoing_unidirectional_streams());
    auto* control_stream =
        QuicSpdySessionPeer::GetSendControlStream(session_.get());
    EXPECT_EQ(kInitialStreamFlowControlWindowForTest,
              QuicStreamPeer::SendWindowOffset(control_stream));
    EXPECT_EQ(5u, session_->max_outbound_header_list_size());
  } else {
    auto* id_manager = QuicSessionPeer::GetStreamIdManager(session_.get());
    EXPECT_EQ(kDefaultMaxStreamsPerConnection,
              id_manager->max_open_outgoing_streams());
  }

  // Complete the handshake with a different config.
  QuicConfig config = DefaultQuicConfig();
  config.SetInitialMaxStreamDataBytesUnidirectionalToSend(
      kInitialStreamFlowControlWindowForTest + 1);
  config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest + 1);
  config.SetMaxBidirectionalStreamsToSend(kDefaultMaxStreamsPerConnection + 1);
  config.SetMaxUnidirectionalStreamsToSend(kDefaultMaxStreamsPerConnection + 1);
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));

  EXPECT_TRUE(session_->GetCryptoStream()->IsResumption());
  EXPECT_EQ(kInitialSessionFlowControlWindowForTest + 1,
            session_->flow_controller()->send_window_offset());
  if (session_->version().UsesHttp3()) {
    auto* id_manager = QuicSessionPeer::v99_streamid_manager(session_.get());
    auto* control_stream =
        QuicSpdySessionPeer::GetSendControlStream(session_.get());
    EXPECT_EQ(kDefaultMaxStreamsPerConnection + 1,
              id_manager->max_outgoing_bidirectional_streams());
    EXPECT_EQ(kDefaultMaxStreamsPerConnection +
                  kHttp3StaticUnidirectionalStreamCount + 1,
              id_manager->max_outgoing_unidirectional_streams());
    EXPECT_EQ(kInitialStreamFlowControlWindowForTest + 1,
              QuicStreamPeer::SendWindowOffset(control_stream));
  } else {
    auto* id_manager = QuicSessionPeer::GetStreamIdManager(session_.get());
    EXPECT_EQ(kDefaultMaxStreamsPerConnection + 1,
              id_manager->max_open_outgoing_streams());
  }

  EXPECT_CALL(*connection_, CloseConnection(_, _, _)).Times(0);
  // Let the session receive a new SETTINGS frame to complete the second
  // connection.
  if (session_->version().UsesHttp3()) {
    SettingsFrame settings;
    settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 2;
    settings.values[SETTINGS_MAX_FIELD_SECTION_SIZE] = 5;
    settings.values[256] = 4;  // unknown setting
    session_->OnSettingsFrame(settings);
  }
}

// Regression test for b/159168475
TEST_P(QuicSpdyClientSessionTest, RetransmitDataOnZeroRttReject) {
  // This feature is TLS-only.
  if (session_->version().UsesQuicCrypto()) {
    return;
  }

  CompleteFirstConnection();

  // Create a second connection, but disable 0-RTT on the server.
  CreateConnection();
  ON_CALL(*connection_, OnCanWrite())
      .WillByDefault(
          testing::Invoke(connection_, &MockQuicConnection::ReallyOnCanWrite));
  EXPECT_CALL(*connection_, OnCanWrite()).Times(0);

  QuicConfig config = DefaultQuicConfig();
  config.SetMaxUnidirectionalStreamsToSend(kDefaultMaxStreamsPerConnection);
  config.SetMaxBidirectionalStreamsToSend(kDefaultMaxStreamsPerConnection);
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);

  // Packets will be written: CHLO, HTTP/3 SETTINGS (H/3 only), and request
  // data.
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_INITIAL, NOT_RETRANSMISSION));
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_ZERO_RTT, NOT_RETRANSMISSION))
      .Times(session_->version().UsesHttp3() ? 2 : 1);
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, session_->connection()->encryption_level());
  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream);
  stream->WriteOrBufferData("hello", true, nullptr);

  // When handshake is done, the client sends 2 packet: HANDSHAKE FINISHED, and
  // coalesced retransmission of HTTP/3 SETTINGS and request data.
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_HANDSHAKE, NOT_RETRANSMISSION));
  // TODO(b/158027651): change transmission type to ALL_ZERO_RTT_RETRANSMISSION.
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_FORWARD_SECURE, LOSS_RETRANSMISSION));
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));
  EXPECT_TRUE(session_->GetCryptoStream()->IsResumption());
}

// When IETF QUIC 0-RTT is rejected, a server-sent fresh transport params is
// available. If the new transport params reduces stream/flow control limit to
// lower than what the client has already used, connection will be closed.
TEST_P(QuicSpdyClientSessionTest, ZeroRttRejectReducesStreamLimitTooMuch) {
  // This feature is TLS-only.
  if (session_->version().UsesQuicCrypto()) {
    return;
  }

  CompleteFirstConnection();

  // Create a second connection, but disable 0-RTT on the server.
  CreateConnection();
  QuicConfig config = DefaultQuicConfig();
  // Server doesn't allow any bidirectional streams.
  config.SetMaxBidirectionalStreamsToSend(0);
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream);

  if (session_->version().UsesHttp3()) {
    EXPECT_CALL(
        *connection_,
        CloseConnection(
            QUIC_ZERO_RTT_UNRETRANSMITTABLE,
            "Server rejected 0-RTT, aborting because new bidirectional initial "
            "stream limit 0 is less than current open streams: 1",
            _))
        .WillOnce(testing::Invoke(connection_,
                                  &MockQuicConnection::ReallyCloseConnection));
  } else {
    EXPECT_CALL(
        *connection_,
        CloseConnection(QUIC_INTERNAL_ERROR,
                        "Server rejected 0-RTT, aborting because new stream "
                        "limit 0 is less than current open streams: 1",
                        _))
        .WillOnce(testing::Invoke(connection_,
                                  &MockQuicConnection::ReallyCloseConnection));
  }
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));
}

TEST_P(QuicSpdyClientSessionTest,
       ZeroRttRejectReducesStreamFlowControlTooMuch) {
  // This feature is TLS-only.
  if (session_->version().UsesQuicCrypto()) {
    return;
  }

  CompleteFirstConnection();

  // Create a second connection, but disable 0-RTT on the server.
  CreateConnection();
  QuicConfig config = DefaultQuicConfig();
  // Server doesn't allow any outgoing streams.
  config.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(2);
  config.SetInitialMaxStreamDataBytesUnidirectionalToSend(1);
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream);
  // Let the stream write more than 1 byte of data.
  stream->WriteOrBufferData("hello", true, nullptr);

  if (session_->version().UsesHttp3()) {
    // Both control stream and the request stream will report errors.
    // Open question: should both streams be closed with the same error code?
    EXPECT_CALL(*connection_, CloseConnection(_, _, _))
        .WillOnce(testing::Invoke(connection_,
                                  &MockQuicConnection::ReallyCloseConnection));
    EXPECT_CALL(*connection_,
                CloseConnection(QUIC_ZERO_RTT_UNRETRANSMITTABLE, _, _))
        .WillOnce(testing::Invoke(connection_,
                                  &MockQuicConnection::ReallyCloseConnection))
        .RetiresOnSaturation();
  } else {
    EXPECT_CALL(*connection_,
                CloseConnection(
                    QUIC_ZERO_RTT_UNRETRANSMITTABLE,
                    "Server rejected 0-RTT, aborting because new stream max "
                    "data 2 for stream 3 is less than currently used: 5",
                    _))
        .Times(1)
        .WillOnce(testing::Invoke(connection_,
                                  &MockQuicConnection::ReallyCloseConnection));
  }
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));
}

TEST_P(QuicSpdyClientSessionTest,
       ZeroRttRejectReducesSessionFlowControlTooMuch) {
  // This feature is TLS-only.
  if (session_->version().UsesQuicCrypto()) {
    return;
  }

  CompleteFirstConnection();

  // Create a second connection, but disable 0-RTT on the server.
  CreateConnection();
  QuicConfig config = DefaultQuicConfig();
  // Server doesn't allow minimum data in session.
  config.SetInitialSessionFlowControlWindowToSend(
      kMinimumFlowControlSendWindow);
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  QuicSpdyClientStream* stream = session_->CreateOutgoingBidirectionalStream();
  ASSERT_TRUE(stream);
  std::string data_to_send(kMinimumFlowControlSendWindow + 1, 'x');
  // Let the stream write some data.
  stream->WriteOrBufferData(data_to_send, true, nullptr);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_ZERO_RTT_UNRETRANSMITTABLE, _, _))
      .WillOnce(testing::Invoke(connection_,
                                &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));
}

TEST_P(QuicSpdyClientSessionTest, SetMaxPushIdBeforeEncryptionEstablished) {
  // 0-RTT is TLS-only, MAX_PUSH_ID frame is HTTP/3-only.
  if (!session_->version().UsesTls() || !session_->version().UsesHttp3()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  // No MAX_PUSH_ID frame is sent before encryption is established.
  session_->SetMaxPushId(5);

  EXPECT_FALSE(session_->IsEncryptionEstablished());
  EXPECT_FALSE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_INITIAL, session_->connection()->encryption_level());

  // MAX_PUSH_ID frame is sent upon encryption establishment with the value set
  // by the earlier SetMaxPushId() call.
  EXPECT_CALL(debug_visitor, OnSettingsFrameSent(_));
  EXPECT_CALL(debug_visitor, OnMaxPushIdFrameSent(_))
      .WillOnce(Invoke(
          [](const MaxPushIdFrame& frame) { EXPECT_EQ(5u, frame.push_id); }));
  session_->CryptoConnect();
  testing::Mock::VerifyAndClearExpectations(&debug_visitor);

  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_FALSE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, session_->connection()->encryption_level());

  // Another SetMaxPushId() call with the same value does not trigger sending
  // another MAX_PUSH_ID frame.
  session_->SetMaxPushId(5);

  // Calling SetMaxPushId() with a different value results in sending another
  // MAX_PUSH_ID frame.
  EXPECT_CALL(debug_visitor, OnMaxPushIdFrameSent(_))
      .WillOnce(Invoke(
          [](const MaxPushIdFrame& frame) { EXPECT_EQ(10u, frame.push_id); }));
  session_->SetMaxPushId(10);
  testing::Mock::VerifyAndClearExpectations(&debug_visitor);

  QuicConfig config = DefaultQuicConfig();
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));

  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_TRUE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE,
            session_->connection()->encryption_level());
  EXPECT_TRUE(session_->GetCryptoStream()->IsResumption());
}

TEST_P(QuicSpdyClientSessionTest, SetMaxPushIdAfterEncryptionEstablished) {
  // 0-RTT is TLS-only, MAX_PUSH_ID frame is HTTP/3-only.
  if (!session_->version().UsesTls() || !session_->version().UsesHttp3()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  EXPECT_FALSE(session_->IsEncryptionEstablished());
  EXPECT_FALSE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_INITIAL, session_->connection()->encryption_level());

  // No MAX_PUSH_ID frame is sent if SetMaxPushId() has not been called.
  EXPECT_CALL(debug_visitor, OnSettingsFrameSent(_));
  session_->CryptoConnect();
  testing::Mock::VerifyAndClearExpectations(&debug_visitor);

  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_FALSE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, session_->connection()->encryption_level());

  // Calling SetMaxPushId() for the first time after encryption is established
  // results in sending a MAX_PUSH_ID frame.
  EXPECT_CALL(debug_visitor, OnMaxPushIdFrameSent(_))
      .WillOnce(Invoke(
          [](const MaxPushIdFrame& frame) { EXPECT_EQ(5u, frame.push_id); }));
  session_->SetMaxPushId(5);
  testing::Mock::VerifyAndClearExpectations(&debug_visitor);

  // Another SetMaxPushId() call with the same value does not trigger sending
  // another MAX_PUSH_ID frame.
  session_->SetMaxPushId(5);

  // Calling SetMaxPushId() with a different value results in sending another
  // MAX_PUSH_ID frame.
  EXPECT_CALL(debug_visitor, OnMaxPushIdFrameSent(_))
      .WillOnce(Invoke(
          [](const MaxPushIdFrame& frame) { EXPECT_EQ(10u, frame.push_id); }));
  session_->SetMaxPushId(10);
  testing::Mock::VerifyAndClearExpectations(&debug_visitor);

  QuicConfig config = DefaultQuicConfig();
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));

  EXPECT_TRUE(session_->IsEncryptionEstablished());
  EXPECT_TRUE(session_->OneRttKeysAvailable());
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE,
            session_->connection()->encryption_level());
  EXPECT_TRUE(session_->GetCryptoStream()->IsResumption());
}

TEST_P(QuicSpdyClientSessionTest, BadSettingsInZeroRttResumption) {
  if (!session_->version().UsesHttp3()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  CompleteCryptoHandshake();
  EXPECT_TRUE(session_->GetCryptoStream()->EarlyDataAccepted());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH, _, _))
      .WillOnce(testing::Invoke(connection_,
                                &MockQuicConnection::ReallyCloseConnection));
  // Let the session receive a different SETTINGS frame.
  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 1;
  settings.values[SETTINGS_MAX_FIELD_SECTION_SIZE] = 5;
  settings.values[256] = 4;  // unknown setting
  session_->OnSettingsFrame(settings);
}

TEST_P(QuicSpdyClientSessionTest, BadSettingsInZeroRttRejection) {
  if (!session_->version().UsesHttp3()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  session_->CryptoConnect();
  EXPECT_TRUE(session_->IsEncryptionEstablished());
  QuicConfig config = DefaultQuicConfig();
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &helper_, &alarm_factory_,
      connection_, crypto_stream_, AlpnForVersion(connection_->version()));
  EXPECT_FALSE(session_->GetCryptoStream()->EarlyDataAccepted());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH, _, _))
      .WillOnce(testing::Invoke(connection_,
                                &MockQuicConnection::ReallyCloseConnection));
  // Let the session receive a different SETTINGS frame.
  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 2;
  // setting on SETTINGS_MAX_FIELD_SECTION_SIZE is reduced.
  settings.values[SETTINGS_MAX_FIELD_SECTION_SIZE] = 4;
  settings.values[256] = 4;  // unknown setting
  session_->OnSettingsFrame(settings);
}

TEST_P(QuicSpdyClientSessionTest, ServerAcceptsZeroRttButOmitSetting) {
  if (!session_->version().UsesHttp3()) {
    return;
  }

  CompleteFirstConnection();

  CreateConnection();
  CompleteCryptoHandshake();
  EXPECT_TRUE(session_->GetMutableCryptoStream()->EarlyDataAccepted());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH, _, _))
      .WillOnce(testing::Invoke(connection_,
                                &MockQuicConnection::ReallyCloseConnection));
  // Let the session receive a different SETTINGS frame.
  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 1;
  // Intentionally omit SETTINGS_MAX_FIELD_SECTION_SIZE which was previously
  // sent with a non-zero value.
  settings.values[256] = 4;  // unknown setting
  session_->OnSettingsFrame(settings);
}

}  // namespace
}  // namespace test
}  // namespace quic
