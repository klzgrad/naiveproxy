// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"

#include <memory>
#include <ostream>
#include <string>

#include "net/third_party/quiche/src/quic/core/chlo_extractor.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer_wrapper.h"
#include "net/third_party/quiche/src/quic/core/quic_time_wait_list_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/fake_proof_source.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_time_wait_list_manager.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_buffered_packet_store_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_crypto_server_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_dispatcher_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_time_wait_list_manager_peer.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_crypto_server_stream_helper.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArg;
using testing::WithoutArgs;

static const size_t kDefaultMaxConnectionsInStore = 100;
static const size_t kMaxConnectionsWithoutCHLO =
    kDefaultMaxConnectionsInStore / 2;
static const int16_t kMaxNumSessionsToCreate = 16;

namespace quic {
namespace test {
namespace {

class TestQuicSpdyServerSession : public QuicServerSessionBase {
 public:
  TestQuicSpdyServerSession(const QuicConfig& config,
                            QuicConnection* connection,
                            const QuicCryptoServerConfig* crypto_config,
                            QuicCompressedCertsCache* compressed_certs_cache)
      : QuicServerSessionBase(config,
                              CurrentSupportedVersions(),
                              connection,
                              nullptr,
                              nullptr,
                              crypto_config,
                              compressed_certs_cache),
        crypto_stream_(QuicServerSessionBase::GetMutableCryptoStream()) {}
  TestQuicSpdyServerSession(const TestQuicSpdyServerSession&) = delete;
  TestQuicSpdyServerSession& operator=(const TestQuicSpdyServerSession&) =
      delete;

  ~TestQuicSpdyServerSession() override { delete connection(); }

  MOCK_METHOD2(OnConnectionClosed,
               void(const QuicConnectionCloseFrame& frame,
                    ConnectionCloseSource source));
  MOCK_METHOD1(CreateIncomingStream, QuicSpdyStream*(QuicStreamId id));
  MOCK_METHOD1(CreateIncomingStream, QuicSpdyStream*(PendingStream* pending));
  MOCK_METHOD0(CreateOutgoingBidirectionalStream, QuicSpdyStream*());
  MOCK_METHOD0(CreateOutgoingUnidirectionalStream, QuicSpdyStream*());

  QuicCryptoServerStreamBase* CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override {
    return new QuicCryptoServerStream(crypto_config, compressed_certs_cache,
                                      this, stream_helper());
  }

  void SetCryptoStream(QuicCryptoServerStream* crypto_stream) {
    crypto_stream_ = crypto_stream;
  }

  QuicCryptoServerStreamBase* GetMutableCryptoStream() override {
    return crypto_stream_;
  }

  const QuicCryptoServerStreamBase* GetCryptoStream() const override {
    return crypto_stream_;
  }

  QuicCryptoServerStream::Helper* stream_helper() {
    return QuicServerSessionBase::stream_helper();
  }

 private:
  QuicCryptoServerStreamBase* crypto_stream_;
};

class TestDispatcher : public QuicDispatcher {
 public:
  TestDispatcher(const QuicConfig* config,
                 const QuicCryptoServerConfig* crypto_config,
                 QuicVersionManager* version_manager,
                 QuicRandom* random)
      : QuicDispatcher(config,
                       crypto_config,
                       version_manager,
                       QuicMakeUnique<MockQuicConnectionHelper>(),
                       std::unique_ptr<QuicCryptoServerStream::Helper>(
                           new QuicSimpleCryptoServerStreamHelper()),
                       QuicMakeUnique<MockAlarmFactory>(),
                       kQuicDefaultConnectionIdLength),
        random_(random) {}

  MOCK_METHOD4(CreateQuicSession,
               QuicServerSessionBase*(QuicConnectionId connection_id,
                                      const QuicSocketAddress& peer_address,
                                      QuicStringPiece alpn,
                                      const quic::ParsedQuicVersion& version));

  MOCK_METHOD1(ShouldCreateOrBufferPacketForConnection,
               bool(const ReceivedPacketInfo& packet_info));

  struct TestQuicPerPacketContext : public QuicPerPacketContext {
    std::string custom_packet_context;
  };

  std::unique_ptr<QuicPerPacketContext> GetPerPacketContext() const override {
    auto test_context = QuicMakeUnique<TestQuicPerPacketContext>();
    test_context->custom_packet_context = custom_packet_context_;
    return std::move(test_context);
  }

  void RestorePerPacketContext(
      std::unique_ptr<QuicPerPacketContext> context) override {
    TestQuicPerPacketContext* test_context =
        static_cast<TestQuicPerPacketContext*>(context.get());
    custom_packet_context_ = test_context->custom_packet_context;
  }

  std::string custom_packet_context_;

  using QuicDispatcher::SetAllowShortInitialServerConnectionIds;
  using QuicDispatcher::writer;

  QuicRandom* random_;
};

// A Connection class which unregisters the session from the dispatcher when
// sending connection close.
// It'd be slightly more realistic to do this from the Session but it would
// involve a lot more mocking.
class MockServerConnection : public MockQuicConnection {
 public:
  MockServerConnection(QuicConnectionId connection_id,
                       MockQuicConnectionHelper* helper,
                       MockAlarmFactory* alarm_factory,
                       QuicDispatcher* dispatcher)
      : MockQuicConnection(connection_id,
                           helper,
                           alarm_factory,
                           Perspective::IS_SERVER),
        dispatcher_(dispatcher) {}

  void UnregisterOnConnectionClosed() {
    QUIC_LOG(ERROR) << "Unregistering " << connection_id();
    dispatcher_->OnConnectionClosed(connection_id(), QUIC_NO_ERROR,
                                    "Unregistering.",
                                    ConnectionCloseSource::FROM_SELF);
  }

 private:
  QuicDispatcher* dispatcher_;
};

class QuicDispatcherTest : public QuicTest {
 public:
  QuicDispatcherTest()
      : QuicDispatcherTest(crypto_test_utils::ProofSourceForTesting()) {}

  explicit QuicDispatcherTest(std::unique_ptr<ProofSource> proof_source)
      : version_manager_(AllSupportedVersions()),
        crypto_config_(QuicCryptoServerConfig::TESTING,
                       QuicRandom::GetInstance(),
                       std::move(proof_source),
                       KeyExchangeSource::Default()),
        server_address_(QuicIpAddress::Any4(), 5),
        dispatcher_(
            new NiceMock<TestDispatcher>(&config_,
                                         &crypto_config_,
                                         &version_manager_,
                                         mock_helper_.GetRandomGenerator())),
        time_wait_list_manager_(nullptr),
        session1_(nullptr),
        session2_(nullptr),
        store_(nullptr),
        connection_id_(1) {}

  void SetUp() override {
    dispatcher_->InitializeWithWriter(new MockPacketWriter());
    // Set the counter to some value to start with.
    QuicDispatcherPeer::set_new_sessions_allowed_per_event_loop(
        dispatcher_.get(), kMaxNumSessionsToCreate);
    ON_CALL(*dispatcher_, ShouldCreateOrBufferPacketForConnection(_))
        .WillByDefault(Return(true));
  }

  MockQuicConnection* connection1() {
    if (session1_ == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<MockQuicConnection*>(session1_->connection());
  }

  MockQuicConnection* connection2() {
    if (session2_ == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<MockQuicConnection*>(session2_->connection());
  }

  // Process a packet with an 8 byte connection id,
  // 6 byte packet number, default path id, and packet number 1,
  // using the first supported version.
  void ProcessPacket(QuicSocketAddress peer_address,
                     QuicConnectionId server_connection_id,
                     bool has_version_flag,
                     const std::string& data) {
    ProcessPacket(peer_address, server_connection_id, has_version_flag, data,
                  CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER);
  }

  // Process a packet with a default path id, and packet number 1,
  // using the first supported version.
  void ProcessPacket(QuicSocketAddress peer_address,
                     QuicConnectionId server_connection_id,
                     bool has_version_flag,
                     const std::string& data,
                     QuicConnectionIdIncluded server_connection_id_included,
                     QuicPacketNumberLength packet_number_length) {
    ProcessPacket(peer_address, server_connection_id, has_version_flag, data,
                  server_connection_id_included, packet_number_length, 1);
  }

  // Process a packet using the first supported version.
  void ProcessPacket(QuicSocketAddress peer_address,
                     QuicConnectionId server_connection_id,
                     bool has_version_flag,
                     const std::string& data,
                     QuicConnectionIdIncluded server_connection_id_included,
                     QuicPacketNumberLength packet_number_length,
                     uint64_t packet_number) {
    ProcessPacket(peer_address, server_connection_id, has_version_flag,
                  CurrentSupportedVersions().front(), data,
                  server_connection_id_included, packet_number_length,
                  packet_number);
  }

  // Processes a packet.
  void ProcessPacket(QuicSocketAddress peer_address,
                     QuicConnectionId server_connection_id,
                     bool has_version_flag,
                     ParsedQuicVersion version,
                     const std::string& data,
                     QuicConnectionIdIncluded server_connection_id_included,
                     QuicPacketNumberLength packet_number_length,
                     uint64_t packet_number) {
    ProcessPacket(peer_address, server_connection_id, EmptyQuicConnectionId(),
                  has_version_flag, version, data,
                  server_connection_id_included, CONNECTION_ID_ABSENT,
                  packet_number_length, packet_number);
  }

  // Processes a packet.
  void ProcessPacket(QuicSocketAddress peer_address,
                     QuicConnectionId server_connection_id,
                     QuicConnectionId client_connection_id,
                     bool has_version_flag,
                     ParsedQuicVersion version,
                     const std::string& data,
                     QuicConnectionIdIncluded server_connection_id_included,
                     QuicConnectionIdIncluded client_connection_id_included,
                     QuicPacketNumberLength packet_number_length,
                     uint64_t packet_number) {
    ParsedQuicVersionVector versions(SupportedVersions(version));
    std::unique_ptr<QuicEncryptedPacket> packet(ConstructEncryptedPacket(
        server_connection_id, client_connection_id, has_version_flag, false,
        packet_number, data, server_connection_id_included,
        client_connection_id_included, packet_number_length, &versions));
    std::unique_ptr<QuicReceivedPacket> received_packet(
        ConstructReceivedPacket(*packet, mock_helper_.GetClock()->Now()));

    if (ChloExtractor::Extract(*packet, versions, {}, nullptr,
                               server_connection_id.length())) {
      // Add CHLO packet to the beginning to be verified first, because it is
      // also processed first by new session.
      data_connection_map_[server_connection_id].push_front(
          std::string(packet->data(), packet->length()));
    } else {
      // For non-CHLO, always append to last.
      data_connection_map_[server_connection_id].push_back(
          std::string(packet->data(), packet->length()));
    }
    dispatcher_->ProcessPacket(server_address_, peer_address, *received_packet);
  }

  void ValidatePacket(QuicConnectionId conn_id,
                      const QuicEncryptedPacket& packet) {
    EXPECT_EQ(data_connection_map_[conn_id].front().length(),
              packet.AsStringPiece().length());
    EXPECT_EQ(data_connection_map_[conn_id].front(), packet.AsStringPiece());
    data_connection_map_[conn_id].pop_front();
  }

  QuicServerSessionBase* CreateSession(
      TestDispatcher* dispatcher,
      const QuicConfig& config,
      QuicConnectionId connection_id,
      const QuicSocketAddress& /*peer_address*/,
      MockQuicConnectionHelper* helper,
      MockAlarmFactory* alarm_factory,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      TestQuicSpdyServerSession** session) {
    MockServerConnection* connection = new MockServerConnection(
        connection_id, helper, alarm_factory, dispatcher);
    connection->SetQuicPacketWriter(dispatcher->writer(),
                                    /*owns_writer=*/false);
    *session = new TestQuicSpdyServerSession(config, connection, crypto_config,
                                             compressed_certs_cache);
    connection->set_visitor(*session);
    ON_CALL(*connection, CloseConnection(_, _, _))
        .WillByDefault(WithoutArgs(Invoke(
            connection, &MockServerConnection::UnregisterOnConnectionClosed)));
    return *session;
  }

  void CreateTimeWaitListManager() {
    time_wait_list_manager_ = new MockTimeWaitListManager(
        QuicDispatcherPeer::GetWriter(dispatcher_.get()), dispatcher_.get(),
        mock_helper_.GetClock(), &mock_alarm_factory_);
    // dispatcher_ takes the ownership of time_wait_list_manager_.
    QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                               time_wait_list_manager_);
  }

  std::string SerializeCHLO() {
    CryptoHandshakeMessage client_hello;
    client_hello.set_tag(kCHLO);
    client_hello.SetStringPiece(kALPN, "hq");
    return std::string(client_hello.GetSerialized().AsStringPiece());
  }

  void MarkSession1Deleted() { session1_ = nullptr; }

  void VerifyVersionSupported(ParsedQuicVersion version) {
    QuicConnectionId connection_id = TestConnectionId(++connection_id_);
    QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
    EXPECT_CALL(*dispatcher_, CreateQuicSession(connection_id, client_address,
                                                QuicStringPiece("hq"), _))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, connection_id, client_address,
            &mock_helper_, &mock_alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
                ProcessUdpPacket(_, _, _))
        .WillOnce(WithArg<2>(
            Invoke([this, connection_id](const QuicEncryptedPacket& packet) {
              ValidatePacket(connection_id, packet);
            })));
    EXPECT_CALL(*dispatcher_,
                ShouldCreateOrBufferPacketForConnection(
                    ReceivedPacketInfoConnectionIdEquals(connection_id)));
    ProcessPacket(client_address, connection_id, true, version, SerializeCHLO(),
                  CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
  }

  void VerifyVersionNotSupported(ParsedQuicVersion version) {
    QuicConnectionId connection_id = TestConnectionId(++connection_id_);
    QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
    EXPECT_CALL(*dispatcher_, CreateQuicSession(connection_id, client_address,
                                                QuicStringPiece("hq"), _))
        .Times(0);
    ProcessPacket(client_address, connection_id, true, version, SerializeCHLO(),
                  CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
  }

  MockQuicConnectionHelper mock_helper_;
  MockAlarmFactory mock_alarm_factory_;
  QuicConfig config_;
  QuicVersionManager version_manager_;
  QuicCryptoServerConfig crypto_config_;
  QuicSocketAddress server_address_;
  std::unique_ptr<NiceMock<TestDispatcher>> dispatcher_;
  MockTimeWaitListManager* time_wait_list_manager_;
  TestQuicSpdyServerSession* session1_;
  TestQuicSpdyServerSession* session2_;
  std::map<QuicConnectionId, std::list<std::string>> data_connection_map_;
  QuicBufferedPacketStore* store_;
  uint64_t connection_id_;
};

TEST_F(QuicDispatcherTest, TlsClientHelloCreatesSession) {
  if (!QuicVersionUsesCryptoFrames(
          CurrentSupportedVersions().front().transport_version)) {
    // TLS is only supported in versions 47 and greater.
    return;
  }
  SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece(""), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(
      client_address, TestConnectionId(1), true,
      ParsedQuicVersion(PROTOCOL_TLS1_3,
                        CurrentSupportedVersions().front().transport_version),
      SerializeCHLO(), CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
}

TEST_F(QuicDispatcherTest, ProcessPackets) {
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(client_address, TestConnectionId(1), true, SerializeCHLO());

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(2), client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(2), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session2_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session2_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(2), packet);
      })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(2))));
  ProcessPacket(client_address, TestConnectionId(2), true, SerializeCHLO());

  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(1)
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));
  ProcessPacket(client_address, TestConnectionId(1), false, "data");
}

// Regression test of b/93325907.
TEST_F(QuicDispatcherTest, DispatcherDoesNotRejectPacketNumberZero) {
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  // Verify both packets 1 and 2 are processed by connection 1.
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(2)
      .WillRepeatedly(
          WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
            ValidatePacket(TestConnectionId(1), packet);
          })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(
      client_address, TestConnectionId(1), true,
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO,
                        CurrentSupportedVersions().front().transport_version),
      SerializeCHLO(), CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
  // Packet number 256 with packet number length 1 would be considered as 0 in
  // dispatcher.
  ProcessPacket(
      client_address, TestConnectionId(1), false,
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO,
                        CurrentSupportedVersions().front().transport_version),
      "", CONNECTION_ID_PRESENT, PACKET_1BYTE_PACKET_NUMBER, 256);
}

TEST_F(QuicDispatcherTest, StatelessVersionNegotiation) {
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  CreateTimeWaitListManager();
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(
      *time_wait_list_manager_,
      SendVersionNegotiationPacket(TestConnectionId(1), _, _, _, _, _, _, _))
      .Times(1);
  // Pad the CHLO message with enough data to make the packet large enough
  // to trigger version negotiation.
  std::string chlo = SerializeCHLO() + std::string(1200, 'a');
  DCHECK_LE(1200u, chlo.length());
  ProcessPacket(client_address, TestConnectionId(1), true,
                QuicVersionReservedForNegotiation(), chlo,
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
}

TEST_F(QuicDispatcherTest, StatelessVersionNegotiationWithClientConnectionId) {
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  CreateTimeWaitListManager();
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              SendVersionNegotiationPacket(
                  TestConnectionId(1), TestConnectionId(2), _, _, _, _, _, _))
      .Times(1);
  // Pad the CHLO message with enough data to make the packet large enough
  // to trigger version negotiation.
  std::string chlo = SerializeCHLO() + std::string(1200, 'a');
  DCHECK_LE(1200u, chlo.length());
  ProcessPacket(client_address, TestConnectionId(1), TestConnectionId(2), true,
                QuicVersionReservedForNegotiation(), chlo,
                CONNECTION_ID_PRESENT, CONNECTION_ID_PRESENT,
                PACKET_4BYTE_PACKET_NUMBER, 1);
}

TEST_F(QuicDispatcherTest, NoVersionNegotiationWithSmallPacket) {
  CreateTimeWaitListManager();
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              SendVersionNegotiationPacket(_, _, _, _, _, _, _, _))
      .Times(0);
  std::string chlo = SerializeCHLO() + std::string(1200, 'a');
  // Truncate to 1100 bytes of payload which results in a packet just
  // under 1200 bytes after framing, packet, and encryption overhead.
  DCHECK_LE(1200u, chlo.length());
  std::string truncated_chlo = chlo.substr(0, 1100);
  DCHECK_EQ(1100u, truncated_chlo.length());
  ProcessPacket(client_address, TestConnectionId(1), true,
                QuicVersionReservedForNegotiation(), truncated_chlo,
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
}

// Disabling CHLO size validation allows the dispatcher to send version
// negotiation packets in response to a CHLO that is otherwise too small.
TEST_F(QuicDispatcherTest, VersionNegotiationWithoutChloSizeValidation) {
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  crypto_config_.set_validate_chlo_size(false);

  CreateTimeWaitListManager();
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              SendVersionNegotiationPacket(_, _, _, _, _, _, _, _))
      .Times(1);
  std::string chlo = SerializeCHLO() + std::string(1200, 'a');
  // Truncate to 1100 bytes of payload which results in a packet just
  // under 1200 bytes after framing, packet, and encryption overhead.
  DCHECK_LE(1200u, chlo.length());
  std::string truncated_chlo = chlo.substr(0, 1100);
  DCHECK_EQ(1100u, truncated_chlo.length());
  ProcessPacket(client_address, TestConnectionId(1), true,
                QuicVersionReservedForNegotiation(), truncated_chlo,
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER, 1);
}

TEST_F(QuicDispatcherTest, Shutdown) {
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(_, client_address, QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));

  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(client_address, TestConnectionId(1), true, SerializeCHLO());

  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              CloseConnection(QUIC_PEER_GOING_AWAY, _, _));

  dispatcher_->Shutdown();
}

TEST_F(QuicDispatcherTest, TimeWaitListManager) {
  CreateTimeWaitListManager();

  // Create a new session.
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId connection_id = TestConnectionId(1);
  EXPECT_CALL(*dispatcher_, CreateQuicSession(connection_id, client_address,
                                              QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, connection_id, client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));

  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(client_address, connection_id, true, SerializeCHLO());

  // Now close the connection, which should add it to the time wait list.
  session1_->connection()->CloseConnection(
      QUIC_INVALID_VERSION,
      "Server: Packet 2 without version flag before version negotiated.",
      ConnectionCloseBehavior::SILENT_CLOSE);
  EXPECT_TRUE(time_wait_list_manager_->IsConnectionIdInTimeWait(connection_id));

  // Dispatcher forwards subsequent packets for this connection_id to the time
  // wait list manager.
  EXPECT_CALL(*time_wait_list_manager_,
              ProcessPacket(_, _, connection_id, _, _))
      .Times(1);
  EXPECT_CALL(*time_wait_list_manager_,
              AddConnectionIdToTimeWait(_, _, _, _, _))
      .Times(0);
  ProcessPacket(client_address, connection_id, true, "data");
}

TEST_F(QuicDispatcherTest, NoVersionPacketToTimeWaitListManager) {
  CreateTimeWaitListManager();

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId connection_id = TestConnectionId(1);
  // Dispatcher forwards all packets for this connection_id to the time wait
  // list manager.
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, QuicStringPiece("hq"), _))
      .Times(0);
  if (GetQuicReloadableFlag(quic_reject_unprocessable_packets_statelessly)) {
    EXPECT_CALL(*time_wait_list_manager_,
                ProcessPacket(_, _, connection_id, _, _))
        .Times(0);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*time_wait_list_manager_, SendPublicReset(_, _, _, _, _))
        .Times(1);
  } else {
    EXPECT_CALL(*time_wait_list_manager_,
                ProcessPacket(_, _, connection_id, _, _))
        .Times(1);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(1);
  }
  ProcessPacket(client_address, connection_id, false, SerializeCHLO());
}

TEST_F(QuicDispatcherTest,
       DonotTimeWaitPacketsWithUnknownConnectionIdAndNoVersion) {
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  CreateTimeWaitListManager();

  char short_packet[22] = {0x70, 0xa7, 0x02, 0x6b};
  QuicReceivedPacket packet(short_packet, 22, QuicTime::Zero());
  char valid_size_packet[23] = {0x70, 0xa7, 0x02, 0x6c};
  QuicReceivedPacket packet2(valid_size_packet, 23, QuicTime::Zero());
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  if (GetQuicReloadableFlag(quic_reject_unprocessable_packets_statelessly)) {
    EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(0);
  } else {
    EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _))
        .Times(2);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(2);
  }
  if (GetQuicReloadableFlag(quic_reject_unprocessable_packets_statelessly)) {
    // Verify small packet is silently dropped.
    EXPECT_CALL(*time_wait_list_manager_, SendPublicReset(_, _, _, _, _))
        .Times(0);
  }
  dispatcher_->ProcessPacket(server_address_, client_address, packet);
  if (GetQuicReloadableFlag(quic_reject_unprocessable_packets_statelessly)) {
    EXPECT_CALL(*time_wait_list_manager_, SendPublicReset(_, _, _, _, _))
        .Times(1);
  }
  dispatcher_->ProcessPacket(server_address_, client_address, packet2);
}

// Makes sure nine-byte connection IDs are replaced by 8-byte ones.
TEST_F(QuicDispatcherTest, LongConnectionIdLengthReplaced) {
  if (!QuicUtils::VariableLengthConnectionIdAllowedForVersion(
          CurrentSupportedVersions()[0].transport_version)) {
    // When variable length connection IDs are not supported, the connection
    // fails. See StrayPacketTruncatedConnectionId.
    return;
  }
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  QuicConnectionId bad_connection_id = TestConnectionIdNineBytesLong(2);
  QuicConnectionId fixed_connection_id =
      QuicUtils::CreateReplacementConnectionId(bad_connection_id);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(fixed_connection_id, client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, fixed_connection_id, client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(
          Invoke([this, bad_connection_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(bad_connection_id, packet);
          })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(bad_connection_id)));
  ProcessPacket(client_address, bad_connection_id, true, SerializeCHLO());
}

// Makes sure zero-byte connection IDs are replaced by 8-byte ones.
TEST_F(QuicDispatcherTest, InvalidShortConnectionIdLengthReplaced) {
  if (!QuicUtils::VariableLengthConnectionIdAllowedForVersion(
          CurrentSupportedVersions()[0].transport_version)) {
    // When variable length connection IDs are not supported, the connection
    // fails. See StrayPacketTruncatedConnectionId.
    return;
  }
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  QuicConnectionId bad_connection_id = EmptyQuicConnectionId();
  QuicConnectionId fixed_connection_id =
      QuicUtils::CreateReplacementConnectionId(bad_connection_id);

  // Disable validation of invalid short connection IDs.
  dispatcher_->SetAllowShortInitialServerConnectionIds(true);
  // Note that StrayPacketTruncatedConnectionId covers the case where the
  // validation is still enabled.

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(fixed_connection_id, client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, fixed_connection_id, client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(
          Invoke([this, bad_connection_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(bad_connection_id, packet);
          })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(bad_connection_id)));
  ProcessPacket(client_address, bad_connection_id, true, SerializeCHLO());
}

// Makes sure TestConnectionId(1) creates a new connection and
// TestConnectionIdNineBytesLong(2) gets replaced.
TEST_F(QuicDispatcherTest, MixGoodAndBadConnectionIdLengthPackets) {
  if (!QuicUtils::VariableLengthConnectionIdAllowedForVersion(
          CurrentSupportedVersions()[0].transport_version)) {
    return;
  }

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId bad_connection_id = TestConnectionIdNineBytesLong(2);
  QuicConnectionId fixed_connection_id =
      QuicUtils::CreateReplacementConnectionId(bad_connection_id);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
  ProcessPacket(client_address, TestConnectionId(1), true, SerializeCHLO());

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(fixed_connection_id, client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, fixed_connection_id, client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session2_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session2_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(
          Invoke([this, bad_connection_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(bad_connection_id, packet);
          })));
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(bad_connection_id)));
  ProcessPacket(client_address, bad_connection_id, true, SerializeCHLO());

  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(1)
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));
  ProcessPacket(client_address, TestConnectionId(1), false, "data");
}

TEST_F(QuicDispatcherTest, ProcessPacketWithZeroPort) {
  CreateTimeWaitListManager();

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 0);

  // dispatcher_ should drop this packet.
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece("hq"), _))
      .Times(0);
  EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              AddConnectionIdToTimeWait(_, _, _, _, _))
      .Times(0);
  ProcessPacket(client_address, TestConnectionId(1), true, SerializeCHLO());
}

TEST_F(QuicDispatcherTest, ProcessPacketWithInvalidShortInitialConnectionId) {
  SetQuicReloadableFlag(quic_drop_invalid_small_initial_connection_id, true);
  // Enable v47 otherwise we cannot create a packet with a short connection ID.
  SetQuicReloadableFlag(quic_enable_version_47, true);
  CreateTimeWaitListManager();

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

  // dispatcher_ should drop this packet.
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(_, client_address, QuicStringPiece("hq"), _))
      .Times(0);
  EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              AddConnectionIdToTimeWait(_, _, _, _, _))
      .Times(0);
  ProcessPacket(client_address, EmptyQuicConnectionId(), true, SerializeCHLO());
}

TEST_F(QuicDispatcherTest, OKSeqNoPacketProcessed) {
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId connection_id = TestConnectionId(1);

  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(1), client_address,
                                QuicStringPiece("hq"), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, TestConnectionId(1), client_address,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
        ValidatePacket(TestConnectionId(1), packet);
      })));

  // A packet whose packet number is the largest that is allowed to start a
  // connection.
  EXPECT_CALL(*dispatcher_,
              ShouldCreateOrBufferPacketForConnection(
                  ReceivedPacketInfoConnectionIdEquals(connection_id)));
  ProcessPacket(client_address, connection_id, true, SerializeCHLO(),
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER,
                QuicDispatcher::kMaxReasonableInitialPacketNumber);
}

TEST_F(QuicDispatcherTest, SupportedTransportVersionsChangeInFlight) {
  SetQuicRestartFlag(quic_dispatcher_hands_chlo_extractor_one_version, true);
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, true);
  SetQuicReloadableFlag(quic_enable_version_48_2, true);
  SetQuicReloadableFlag(quic_enable_version_99, true);

  VerifyVersionNotSupported(QuicVersionReservedForNegotiation());

  VerifyVersionSupported(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO,
                                           QuicVersionMin().transport_version));
  VerifyVersionSupported(QuicVersionMax());

  // Turn off version 48.
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  VerifyVersionNotSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));

  // Turn on version 48.
  SetQuicReloadableFlag(quic_enable_version_48_2, true);
  VerifyVersionSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));

  // Turn off version 47.
  SetQuicReloadableFlag(quic_enable_version_47, false);
  VerifyVersionNotSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47));

  // Turn on version 47.
  SetQuicReloadableFlag(quic_enable_version_47, true);
  VerifyVersionSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47));

  // Turn off version 39.
  SetQuicReloadableFlag(quic_disable_version_39, true);
  VerifyVersionNotSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39));

  // Turn on version 39.
  SetQuicReloadableFlag(quic_disable_version_39, false);
  VerifyVersionSupported(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39));
}

TEST_F(QuicDispatcherTest, RejectDeprecatedVersionsWithVersionNegotiation) {
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Please add deprecated versions to this test");
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  CreateTimeWaitListManager();

  char packet45[kMinPacketSizeForVersionNegotiation] = {
      0xC0, 'Q', '0', '4', '5', /*connection ID length byte*/ 0x50};
  QuicReceivedPacket packet(packet45, kMinPacketSizeForVersionNegotiation,
                            QuicTime::Zero());
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              SendVersionNegotiationPacket(_, _, _, _, _, _, _, _))
      .Times(1);
  dispatcher_->ProcessPacket(server_address_, client_address, packet);

  char packet44[kMinPacketSizeForVersionNegotiation] = {
      0xFF, 'Q', '0', '4', '4', /*connection ID length byte*/ 0x50};
  QuicReceivedPacket packet2(packet44, kMinPacketSizeForVersionNegotiation,
                             QuicTime::Zero());
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);
  EXPECT_CALL(*time_wait_list_manager_,
              SendVersionNegotiationPacket(_, _, _, _, _, _, _, _))
      .Times(1);
  dispatcher_->ProcessPacket(server_address_, client_address, packet2);
}

TEST_F(QuicDispatcherTest, VersionNegotiationProbeOld) {
  SetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids, false);
  SetQuicReloadableFlag(quic_use_length_prefix_from_packet_info, true);
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  CreateTimeWaitListManager();
  char packet[1200];
  char destination_connection_id_bytes[] = {0x56, 0x4e, 0x20, 0x70,
                                            0x6c, 0x7a, 0x20, 0x21};
  EXPECT_TRUE(QuicFramer::WriteClientVersionNegotiationProbePacket(
      packet, sizeof(packet), destination_connection_id_bytes,
      sizeof(destination_connection_id_bytes)));
  QuicEncryptedPacket encrypted(packet, sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  QuicConnectionId client_connection_id = EmptyQuicConnectionId();
  QuicConnectionId server_connection_id(
      destination_connection_id_bytes, sizeof(destination_connection_id_bytes));
  bool ietf_quic = true;
  bool use_length_prefix =
      GetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids);
  EXPECT_CALL(
      *time_wait_list_manager_,
      SendVersionNegotiationPacket(server_connection_id, client_connection_id,
                                   ietf_quic, use_length_prefix, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
}

TEST_F(QuicDispatcherTest, VersionNegotiationProbe) {
  SetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids, true);
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  SetQuicReloadableFlag(quic_use_length_prefix_from_packet_info, true);
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  CreateTimeWaitListManager();
  char packet[1200];
  char destination_connection_id_bytes[] = {0x56, 0x4e, 0x20, 0x70,
                                            0x6c, 0x7a, 0x20, 0x21};
  EXPECT_TRUE(QuicFramer::WriteClientVersionNegotiationProbePacket(
      packet, sizeof(packet), destination_connection_id_bytes,
      sizeof(destination_connection_id_bytes)));
  QuicEncryptedPacket encrypted(packet, sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  QuicConnectionId client_connection_id = EmptyQuicConnectionId();
  QuicConnectionId server_connection_id(
      destination_connection_id_bytes, sizeof(destination_connection_id_bytes));
  bool ietf_quic = true;
  bool use_length_prefix =
      GetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids);
  EXPECT_CALL(
      *time_wait_list_manager_,
      SendVersionNegotiationPacket(server_connection_id, client_connection_id,
                                   ietf_quic, use_length_prefix, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
}

// Testing packet writer that saves all packets instead of sending them.
// Useful for tests that need access to sent packets.
class SavingWriter : public QuicPacketWriterWrapper {
 public:
  bool IsWriteBlocked() const override { return false; }

  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& /*self_client_address*/,
                          const QuicSocketAddress& /*peer_client_address*/,
                          PerPacketOptions* /*options*/) override {
    packets_.push_back(
        QuicEncryptedPacket(buffer, buf_len, /*owns_buffer=*/false).Clone());
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  std::vector<std::unique_ptr<QuicEncryptedPacket>>* packets() {
    return &packets_;
  }

 private:
  std::vector<std::unique_ptr<QuicEncryptedPacket>> packets_;
};

TEST_F(QuicDispatcherTest, VersionNegotiationProbeEndToEndOld) {
  SetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids, false);
  SetQuicReloadableFlag(quic_use_length_prefix_from_packet_info, true);

  SavingWriter* saving_writer = new SavingWriter();
  // dispatcher_ takes ownership of saving_writer.
  QuicDispatcherPeer::UseWriter(dispatcher_.get(), saving_writer);

  QuicTimeWaitListManager* time_wait_list_manager = new QuicTimeWaitListManager(
      saving_writer, dispatcher_.get(), mock_helper_.GetClock(),
      &mock_alarm_factory_);
  // dispatcher_ takes ownership of time_wait_list_manager.
  QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                             time_wait_list_manager);
  char packet[1200] = {};
  char destination_connection_id_bytes[] = {0x56, 0x4e, 0x20, 0x70,
                                            0x6c, 0x7a, 0x20, 0x21};
  EXPECT_TRUE(QuicFramer::WriteClientVersionNegotiationProbePacket(
      packet, sizeof(packet), destination_connection_id_bytes,
      sizeof(destination_connection_id_bytes)));
  QuicEncryptedPacket encrypted(packet, sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
  ASSERT_EQ(1u, saving_writer->packets()->size());

  char source_connection_id_bytes[255] = {};
  uint8_t source_connection_id_length = 0;
  std::string detailed_error = "foobar";
  EXPECT_TRUE(QuicFramer::ParseServerVersionNegotiationProbeResponse(
      (*(saving_writer->packets()))[0]->data(),
      (*(saving_writer->packets()))[0]->length(), source_connection_id_bytes,
      &source_connection_id_length, &detailed_error));
  EXPECT_EQ("", detailed_error);

  // The source connection ID of the probe response should match the
  // destination connection ID of the probe request.
  test::CompareCharArraysWithHexError(
      "parsed probe", source_connection_id_bytes, source_connection_id_length,
      destination_connection_id_bytes, sizeof(destination_connection_id_bytes));
}

TEST_F(QuicDispatcherTest, VersionNegotiationProbeEndToEnd) {
  SetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids, true);
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  SetQuicReloadableFlag(quic_use_length_prefix_from_packet_info, true);

  SavingWriter* saving_writer = new SavingWriter();
  // dispatcher_ takes ownership of saving_writer.
  QuicDispatcherPeer::UseWriter(dispatcher_.get(), saving_writer);

  QuicTimeWaitListManager* time_wait_list_manager = new QuicTimeWaitListManager(
      saving_writer, dispatcher_.get(), mock_helper_.GetClock(),
      &mock_alarm_factory_);
  // dispatcher_ takes ownership of time_wait_list_manager.
  QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                             time_wait_list_manager);
  char packet[1200] = {};
  char destination_connection_id_bytes[] = {0x56, 0x4e, 0x20, 0x70,
                                            0x6c, 0x7a, 0x20, 0x21};
  EXPECT_TRUE(QuicFramer::WriteClientVersionNegotiationProbePacket(
      packet, sizeof(packet), destination_connection_id_bytes,
      sizeof(destination_connection_id_bytes)));
  QuicEncryptedPacket encrypted(packet, sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
  ASSERT_EQ(1u, saving_writer->packets()->size());

  char source_connection_id_bytes[255] = {};
  uint8_t source_connection_id_length = 0;
  std::string detailed_error = "foobar";
  EXPECT_TRUE(QuicFramer::ParseServerVersionNegotiationProbeResponse(
      (*(saving_writer->packets()))[0]->data(),
      (*(saving_writer->packets()))[0]->length(), source_connection_id_bytes,
      &source_connection_id_length, &detailed_error));
  EXPECT_EQ("", detailed_error);

  // The source connection ID of the probe response should match the
  // destination connection ID of the probe request.
  test::CompareCharArraysWithHexError(
      "parsed probe", source_connection_id_bytes, source_connection_id_length,
      destination_connection_id_bytes, sizeof(destination_connection_id_bytes));
}

TEST_F(QuicDispatcherTest, AndroidConformanceTestOld) {
  // TODO(b/139691956) Remove this test once the workaround is removed.
  // This test requires the workaround behind this flag to pass.
  SetQuicReloadableFlag(quic_reply_to_old_android_conformance_test, true);
  SavingWriter* saving_writer = new SavingWriter();
  // dispatcher_ takes ownership of saving_writer.
  QuicDispatcherPeer::UseWriter(dispatcher_.get(), saving_writer);

  QuicTimeWaitListManager* time_wait_list_manager = new QuicTimeWaitListManager(
      saving_writer, dispatcher_.get(), mock_helper_.GetClock(),
      &mock_alarm_factory_);
  // dispatcher_ takes ownership of time_wait_list_manager.
  QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                             time_wait_list_manager);
  // clang-format off
  static const unsigned char packet[] = {
    // Android UDP network conformance test packet as it was before this change:
    // https://android-review.googlesource.com/c/platform/cts/+/1104285
    0x0c,  // public flags: 8-byte connection ID, 1-byte packet number
    0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,  // 8-byte connection ID
    0x01,  // 1-byte packet number
    0x00,  // private flags
    0x07,  // PING frame
  };
  // clang-format on

  QuicEncryptedPacket encrypted(reinterpret_cast<const char*>(packet),
                                sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
  ASSERT_EQ(1u, saving_writer->packets()->size());

  // The Android UDP network conformance test directly checks that bytes 1-9
  // of the response match the connection ID that was sent.
  static const char connection_id_bytes[] = {0x71, 0x72, 0x73, 0x74,
                                             0x75, 0x76, 0x77, 0x78};
  ASSERT_GE((*(saving_writer->packets()))[0]->length(),
            1u + sizeof(connection_id_bytes));
  test::CompareCharArraysWithHexError(
      "response connection ID", &(*(saving_writer->packets()))[0]->data()[1],
      sizeof(connection_id_bytes), connection_id_bytes,
      sizeof(connection_id_bytes));
}

TEST_F(QuicDispatcherTest, AndroidConformanceTestNewWithWorkaround) {
  // TODO(b/139691956) Remove this test once the workaround is removed.
  // This test doesn't need the workaround but we make sure that it passes even
  // when the flag is true, also see AndroidConformanceTest below.
  SetQuicReloadableFlag(quic_reply_to_old_android_conformance_test, true);
  SavingWriter* saving_writer = new SavingWriter();
  // dispatcher_ takes ownership of saving_writer.
  QuicDispatcherPeer::UseWriter(dispatcher_.get(), saving_writer);

  QuicTimeWaitListManager* time_wait_list_manager = new QuicTimeWaitListManager(
      saving_writer, dispatcher_.get(), mock_helper_.GetClock(),
      &mock_alarm_factory_);
  // dispatcher_ takes ownership of time_wait_list_manager.
  QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                             time_wait_list_manager);
  // clang-format off
  static const unsigned char packet[1200] = {
    // Android UDP network conformance test packet as it was after this change:
    // https://android-review.googlesource.com/c/platform/cts/+/1104285
    0x0d,  // public flags: version, 8-byte connection ID, 1-byte packet number
    0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,  // 8-byte connection ID
    0xaa, 0xda, 0xca, 0xaa,  // reserved-space version number
    0x01,  // 1-byte packet number
    0x00,  // private flags
    0x07,  // PING frame
  };
  // clang-format on

  QuicEncryptedPacket encrypted(reinterpret_cast<const char*>(packet),
                                sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
  ASSERT_EQ(1u, saving_writer->packets()->size());

  // The Android UDP network conformance test directly checks that bytes 1-9
  // of the response match the connection ID that was sent.
  static const char connection_id_bytes[] = {0x71, 0x72, 0x73, 0x74,
                                             0x75, 0x76, 0x77, 0x78};
  ASSERT_GE((*(saving_writer->packets()))[0]->length(),
            1u + sizeof(connection_id_bytes));
  test::CompareCharArraysWithHexError(
      "response connection ID", &(*(saving_writer->packets()))[0]->data()[1],
      sizeof(connection_id_bytes), connection_id_bytes,
      sizeof(connection_id_bytes));
}

TEST_F(QuicDispatcherTest, AndroidConformanceTest) {
  // WARNING: do not remove or modify this test without making sure that we
  // still have adequate coverage for the Android conformance test.

  // Set the flag to false to make sure this test passes even when the
  // workaround is disabled.
  SetQuicReloadableFlag(quic_reply_to_old_android_conformance_test, false);
  SavingWriter* saving_writer = new SavingWriter();
  // dispatcher_ takes ownership of saving_writer.
  QuicDispatcherPeer::UseWriter(dispatcher_.get(), saving_writer);

  QuicTimeWaitListManager* time_wait_list_manager = new QuicTimeWaitListManager(
      saving_writer, dispatcher_.get(), mock_helper_.GetClock(),
      &mock_alarm_factory_);
  // dispatcher_ takes ownership of time_wait_list_manager.
  QuicDispatcherPeer::SetTimeWaitListManager(dispatcher_.get(),
                                             time_wait_list_manager);
  // clang-format off
  static const unsigned char packet[1200] = {
    // Android UDP network conformance test packet as it was after this change:
    // https://android-review.googlesource.com/c/platform/cts/+/1104285
    0x0d,  // public flags: version, 8-byte connection ID, 1-byte packet number
    0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,  // 8-byte connection ID
    0xaa, 0xda, 0xca, 0xaa,  // reserved-space version number
    0x01,  // 1-byte packet number
    0x00,  // private flags
    0x07,  // PING frame
  };
  // clang-format on

  QuicEncryptedPacket encrypted(reinterpret_cast<const char*>(packet),
                                sizeof(packet), false);
  std::unique_ptr<QuicReceivedPacket> received_packet(
      ConstructReceivedPacket(encrypted, mock_helper_.GetClock()->Now()));
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, _, _)).Times(0);

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  dispatcher_->ProcessPacket(server_address_, client_address, *received_packet);
  ASSERT_EQ(1u, saving_writer->packets()->size());

  // The Android UDP network conformance test directly checks that bytes 1-9
  // of the response match the connection ID that was sent.
  static const char connection_id_bytes[] = {0x71, 0x72, 0x73, 0x74,
                                             0x75, 0x76, 0x77, 0x78};
  ASSERT_GE((*(saving_writer->packets()))[0]->length(),
            1u + sizeof(connection_id_bytes));
  test::CompareCharArraysWithHexError(
      "response connection ID", &(*(saving_writer->packets()))[0]->data()[1],
      sizeof(connection_id_bytes), connection_id_bytes,
      sizeof(connection_id_bytes));
}

// Verify the stopgap test: Packets with truncated connection IDs should be
// dropped.
class QuicDispatcherTestStrayPacketConnectionId : public QuicDispatcherTest {};

// Packets with truncated connection IDs should be dropped.
TEST_F(QuicDispatcherTestStrayPacketConnectionId,
       StrayPacketTruncatedConnectionId) {
  SetQuicReloadableFlag(quic_drop_invalid_small_initial_connection_id, true);
  CreateTimeWaitListManager();

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId connection_id = TestConnectionId(1);
  EXPECT_CALL(*dispatcher_, CreateQuicSession(_, _, QuicStringPiece("hq"), _))
      .Times(0);
  if (VersionHasIetfInvariantHeader(
          CurrentSupportedVersions()[0].transport_version)) {
    // This IETF packet has invalid connection ID length.
    EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(0);
  } else {
    // This is a GQUIC packet considered as IETF QUIC packet with short header
    // with unacceptable packet number.
    EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*time_wait_list_manager_,
                AddConnectionIdToTimeWait(_, _, _, _, _))
        .Times(1);
  }
  ProcessPacket(client_address, connection_id, true, "data",
                CONNECTION_ID_ABSENT, PACKET_4BYTE_PACKET_NUMBER);
}

class BlockingWriter : public QuicPacketWriterWrapper {
 public:
  BlockingWriter() : write_blocked_(false) {}

  bool IsWriteBlocked() const override { return write_blocked_; }
  void SetWritable() override { write_blocked_ = false; }

  WriteResult WritePacket(const char* /*buffer*/,
                          size_t /*buf_len*/,
                          const QuicIpAddress& /*self_client_address*/,
                          const QuicSocketAddress& /*peer_client_address*/,
                          PerPacketOptions* /*options*/) override {
    // It would be quite possible to actually implement this method here with
    // the fake blocked status, but it would be significantly more work in
    // Chromium, and since it's not called anyway, don't bother.
    QUIC_LOG(DFATAL) << "Not supported";
    return WriteResult();
  }

  bool write_blocked_;
};

class QuicDispatcherWriteBlockedListTest : public QuicDispatcherTest {
 public:
  void SetUp() override {
    QuicDispatcherTest::SetUp();
    writer_ = new BlockingWriter;
    QuicDispatcherPeer::UseWriter(dispatcher_.get(), writer_);

    QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);

    EXPECT_CALL(*dispatcher_,
                CreateQuicSession(_, client_address, QuicStringPiece("hq"), _))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, TestConnectionId(1), client_address,
            &helper_, &alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
                ProcessUdpPacket(_, _, _))
        .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
          ValidatePacket(TestConnectionId(1), packet);
        })));
    EXPECT_CALL(*dispatcher_,
                ShouldCreateOrBufferPacketForConnection(
                    ReceivedPacketInfoConnectionIdEquals(TestConnectionId(1))));
    ProcessPacket(client_address, TestConnectionId(1), true, SerializeCHLO());

    EXPECT_CALL(*dispatcher_,
                CreateQuicSession(_, client_address, QuicStringPiece("hq"), _))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, TestConnectionId(2), client_address,
            &helper_, &alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session2_)));
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session2_->connection()),
                ProcessUdpPacket(_, _, _))
        .WillOnce(WithArg<2>(Invoke([this](const QuicEncryptedPacket& packet) {
          ValidatePacket(TestConnectionId(2), packet);
        })));
    EXPECT_CALL(*dispatcher_,
                ShouldCreateOrBufferPacketForConnection(
                    ReceivedPacketInfoConnectionIdEquals(TestConnectionId(2))));
    ProcessPacket(client_address, TestConnectionId(2), true, SerializeCHLO());

    blocked_list_ = QuicDispatcherPeer::GetWriteBlockedList(dispatcher_.get());
  }

  void TearDown() override {
    if (connection1() != nullptr) {
      EXPECT_CALL(*connection1(), CloseConnection(QUIC_PEER_GOING_AWAY, _, _));
    }

    if (connection2() != nullptr) {
      EXPECT_CALL(*connection2(), CloseConnection(QUIC_PEER_GOING_AWAY, _, _));
    }
    dispatcher_->Shutdown();
  }

  // Set the dispatcher's writer to be blocked. By default, all connections use
  // the same writer as the dispatcher in this test.
  void SetBlocked() {
    QUIC_LOG(INFO) << "set writer " << writer_ << " to blocked";
    writer_->write_blocked_ = true;
  }

  // Simulate what happens when connection1 gets blocked when writing.
  void BlockConnection1() {
    Connection1Writer()->write_blocked_ = true;
    dispatcher_->OnWriteBlocked(connection1());
  }

  BlockingWriter* Connection1Writer() {
    return static_cast<BlockingWriter*>(connection1()->writer());
  }

  // Simulate what happens when connection2 gets blocked when writing.
  void BlockConnection2() {
    Connection2Writer()->write_blocked_ = true;
    dispatcher_->OnWriteBlocked(connection2());
  }

  BlockingWriter* Connection2Writer() {
    return static_cast<BlockingWriter*>(connection2()->writer());
  }

 protected:
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  BlockingWriter* writer_;
  QuicDispatcher::WriteBlockedList* blocked_list_;
};

TEST_F(QuicDispatcherWriteBlockedListTest, BasicOnCanWrite) {
  // No OnCanWrite calls because no connections are blocked.
  dispatcher_->OnCanWrite();

  // Register connection 1 for events, and make sure it's notified.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  EXPECT_CALL(*connection1(), OnCanWrite());
  dispatcher_->OnCanWrite();

  // It should get only one notification.
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(0);
  dispatcher_->OnCanWrite();
  EXPECT_FALSE(dispatcher_->HasPendingWrites());
}

TEST_F(QuicDispatcherWriteBlockedListTest, OnCanWriteOrder) {
  // Make sure we handle events in order.
  InSequence s;
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection2());
  EXPECT_CALL(*connection1(), OnCanWrite());
  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();

  // Check the other ordering.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection2());
  dispatcher_->OnWriteBlocked(connection1());
  EXPECT_CALL(*connection2(), OnCanWrite());
  EXPECT_CALL(*connection1(), OnCanWrite());
  dispatcher_->OnCanWrite();
}

TEST_F(QuicDispatcherWriteBlockedListTest, OnCanWriteRemove) {
  // Add and remove one connction.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  blocked_list_->erase(connection1());
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(0);
  dispatcher_->OnCanWrite();

  // Add and remove one connction and make sure it doesn't affect others.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection2());
  blocked_list_->erase(connection1());
  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();

  // Add it, remove it, and add it back and make sure things are OK.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  blocked_list_->erase(connection1());
  dispatcher_->OnWriteBlocked(connection1());
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(1);
  dispatcher_->OnCanWrite();
}

TEST_F(QuicDispatcherWriteBlockedListTest, DoubleAdd) {
  // Make sure a double add does not necessitate a double remove.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection1());
  blocked_list_->erase(connection1());
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(0);
  dispatcher_->OnCanWrite();

  // Make sure a double add does not result in two OnCanWrite calls.
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection1());
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(1);
  dispatcher_->OnCanWrite();
}

TEST_F(QuicDispatcherWriteBlockedListTest, OnCanWriteHandleBlockConnection1) {
  // If the 1st blocked writer gets blocked in OnCanWrite, it will be added back
  // into the write blocked list.
  InSequence s;
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection2());
  EXPECT_CALL(*connection1(), OnCanWrite())
      .WillOnce(
          Invoke(this, &QuicDispatcherWriteBlockedListTest::BlockConnection1));
  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();

  // connection1 should be still in the write blocked list.
  EXPECT_TRUE(dispatcher_->HasPendingWrites());

  // Now call OnCanWrite again, connection1 should get its second chance.
  EXPECT_CALL(*connection1(), OnCanWrite());
  EXPECT_CALL(*connection2(), OnCanWrite()).Times(0);
  dispatcher_->OnCanWrite();
  EXPECT_FALSE(dispatcher_->HasPendingWrites());
}

TEST_F(QuicDispatcherWriteBlockedListTest, OnCanWriteHandleBlockConnection2) {
  // If the 2nd blocked writer gets blocked in OnCanWrite, it will be added back
  // into the write blocked list.
  InSequence s;
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection2());
  EXPECT_CALL(*connection1(), OnCanWrite());
  EXPECT_CALL(*connection2(), OnCanWrite())
      .WillOnce(
          Invoke(this, &QuicDispatcherWriteBlockedListTest::BlockConnection2));
  dispatcher_->OnCanWrite();

  // connection2 should be still in the write blocked list.
  EXPECT_TRUE(dispatcher_->HasPendingWrites());

  // Now call OnCanWrite again, connection2 should get its second chance.
  EXPECT_CALL(*connection1(), OnCanWrite()).Times(0);
  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();
  EXPECT_FALSE(dispatcher_->HasPendingWrites());
}

TEST_F(QuicDispatcherWriteBlockedListTest,
       OnCanWriteHandleBlockBothConnections) {
  // Both connections get blocked in OnCanWrite, and added back into the write
  // blocked list.
  InSequence s;
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  dispatcher_->OnWriteBlocked(connection2());
  EXPECT_CALL(*connection1(), OnCanWrite())
      .WillOnce(
          Invoke(this, &QuicDispatcherWriteBlockedListTest::BlockConnection1));
  EXPECT_CALL(*connection2(), OnCanWrite())
      .WillOnce(
          Invoke(this, &QuicDispatcherWriteBlockedListTest::BlockConnection2));
  dispatcher_->OnCanWrite();

  // Both connections should be still in the write blocked list.
  EXPECT_TRUE(dispatcher_->HasPendingWrites());

  // Now call OnCanWrite again, both connections should get its second chance.
  EXPECT_CALL(*connection1(), OnCanWrite());
  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();
  EXPECT_FALSE(dispatcher_->HasPendingWrites());
}

TEST_F(QuicDispatcherWriteBlockedListTest, PerConnectionWriterBlocked) {
  // By default, all connections share the same packet writer with the
  // dispatcher.
  EXPECT_EQ(dispatcher_->writer(), connection1()->writer());
  EXPECT_EQ(dispatcher_->writer(), connection2()->writer());

  // Test the case where connection1 shares the same packet writer as the
  // dispatcher, whereas connection2 owns it's packet writer.
  // Change connection2's writer.
  connection2()->SetQuicPacketWriter(new BlockingWriter, /*owns_writer=*/true);
  EXPECT_NE(dispatcher_->writer(), connection2()->writer());

  BlockConnection2();
  EXPECT_TRUE(dispatcher_->HasPendingWrites());

  EXPECT_CALL(*connection2(), OnCanWrite());
  dispatcher_->OnCanWrite();
  EXPECT_FALSE(dispatcher_->HasPendingWrites());
}

TEST_F(QuicDispatcherWriteBlockedListTest,
       RemoveConnectionFromWriteBlockedListWhenDeletingSessions) {
  dispatcher_->OnConnectionClosed(connection1()->connection_id(),
                                  QUIC_PACKET_WRITE_ERROR, "Closed by test.",
                                  ConnectionCloseSource::FROM_SELF);

  SetBlocked();

  ASSERT_FALSE(dispatcher_->HasPendingWrites());
  SetBlocked();
  dispatcher_->OnWriteBlocked(connection1());
  ASSERT_TRUE(dispatcher_->HasPendingWrites());

  EXPECT_QUIC_BUG(dispatcher_->DeleteSessions(),
                  "QuicConnection was in WriteBlockedList before destruction");
  MarkSession1Deleted();
}

class BufferedPacketStoreTest : public QuicDispatcherTest {
 public:
  BufferedPacketStoreTest()
      : QuicDispatcherTest(),
        server_addr_(QuicSocketAddress(QuicIpAddress::Any4(), 5)),
        client_addr_(QuicIpAddress::Loopback4(), 1234),
        signed_config_(new QuicSignedServerConfig) {}

  void SetUp() override {
    QuicDispatcherTest::SetUp();
    clock_ = QuicDispatcherPeer::GetHelper(dispatcher_.get())->GetClock();

    QuicTransportVersion version = AllSupportedTransportVersions().front();
    CryptoHandshakeMessage chlo =
        crypto_test_utils::GenerateDefaultInchoateCHLO(clock_, version,
                                                       &crypto_config_);
    // Pass an inchoate CHLO.
    crypto_test_utils::GenerateFullCHLO(
        chlo, &crypto_config_, server_addr_, client_addr_, version, clock_,
        signed_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
        &full_chlo_);
  }

  std::string SerializeFullCHLO() {
    return std::string(full_chlo_.GetSerialized().AsStringPiece());
  }

 protected:
  QuicSocketAddress server_addr_;
  QuicSocketAddress client_addr_;
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config_;
  const QuicClock* clock_;
  CryptoHandshakeMessage full_chlo_;
};

TEST_F(BufferedPacketStoreTest, ProcessNonChloPacketsUptoLimitAndProcessChlo) {
  InSequence s;
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId conn_id = TestConnectionId(1);
  // A bunch of non-CHLO should be buffered upon arrival, and the first one
  // should trigger ShouldCreateOrBufferPacketForConnection().
  EXPECT_CALL(*dispatcher_, ShouldCreateOrBufferPacketForConnection(
                                ReceivedPacketInfoConnectionIdEquals(conn_id)));
  for (size_t i = 1; i <= kDefaultMaxUndecryptablePackets + 1; ++i) {
    ProcessPacket(client_address, conn_id, true,
                  QuicStrCat("data packet ", i + 1), CONNECTION_ID_PRESENT,
                  PACKET_4BYTE_PACKET_NUMBER, /*packet_number=*/i + 1);
  }
  EXPECT_EQ(0u, dispatcher_->session_map().size())
      << "No session should be created before CHLO arrives.";

  // Pop out the last packet as it is also be dropped by the store.
  data_connection_map_[conn_id].pop_back();
  // When CHLO arrives, a new session should be created, and all packets
  // buffered should be delivered to the session.
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(conn_id, client_address, QuicStringPiece(), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, conn_id, client_address, &mock_helper_,
          &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));

  // Only |kDefaultMaxUndecryptablePackets| packets were buffered, and they
  // should be delivered in arrival order.
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(kDefaultMaxUndecryptablePackets + 1)  // + 1 for CHLO.
      .WillRepeatedly(
          WithArg<2>(Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(conn_id, packet);
          })));
  ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());
}

TEST_F(BufferedPacketStoreTest,
       ProcessNonChloPacketsForDifferentConnectionsUptoLimit) {
  InSequence s;
  // A bunch of non-CHLO should be buffered upon arrival.
  size_t kNumConnections = kMaxConnectionsWithoutCHLO + 1;
  for (size_t i = 1; i <= kNumConnections; ++i) {
    QuicSocketAddress client_address(QuicIpAddress::Loopback4(), i);
    QuicConnectionId conn_id = TestConnectionId(i);
    EXPECT_CALL(*dispatcher_,
                ShouldCreateOrBufferPacketForConnection(
                    ReceivedPacketInfoConnectionIdEquals(conn_id)));
    ProcessPacket(client_address, conn_id, true,
                  QuicStrCat("data packet on connection ", i),
                  CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER,
                  /*packet_number=*/2);
  }

  // Pop out the packet on last connection as it shouldn't be enqueued in store
  // as well.
  data_connection_map_[TestConnectionId(kNumConnections)].pop_front();

  // Reset session creation counter to ensure processing CHLO can always
  // create session.
  QuicDispatcherPeer::set_new_sessions_allowed_per_event_loop(dispatcher_.get(),
                                                              kNumConnections);
  // Process CHLOs to create session for these connections.
  for (size_t i = 1; i <= kNumConnections; ++i) {
    QuicSocketAddress client_address(QuicIpAddress::Loopback4(), i);
    QuicConnectionId conn_id = TestConnectionId(i);
    if (i == kNumConnections) {
      EXPECT_CALL(*dispatcher_,
                  ShouldCreateOrBufferPacketForConnection(
                      ReceivedPacketInfoConnectionIdEquals(conn_id)));
    }
    EXPECT_CALL(*dispatcher_, CreateQuicSession(conn_id, client_address,
                                                QuicStringPiece(), _))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, conn_id, client_address, &mock_helper_,
            &mock_alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
    // First |kNumConnections| - 1 connections should have buffered
    // a packet in store. The rest should have been dropped.
    size_t num_packet_to_process = i <= kMaxConnectionsWithoutCHLO ? 2u : 1u;
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
                ProcessUdpPacket(_, client_address, _))
        .Times(num_packet_to_process)
        .WillRepeatedly(WithArg<2>(
            Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
              ValidatePacket(conn_id, packet);
            })));

    ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());
  }
}

// Tests that store delivers empty packet list if CHLO arrives firstly.
TEST_F(BufferedPacketStoreTest, DeliverEmptyPackets) {
  QuicConnectionId conn_id = TestConnectionId(1);
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  EXPECT_CALL(*dispatcher_, ShouldCreateOrBufferPacketForConnection(
                                ReceivedPacketInfoConnectionIdEquals(conn_id)));
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(conn_id, client_address, QuicStringPiece(), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, conn_id, client_address, &mock_helper_,
          &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, client_address, _));
  ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());
}

// Tests that a retransmitted CHLO arrives after a connection for the
// CHLO has been created.
TEST_F(BufferedPacketStoreTest, ReceiveRetransmittedCHLO) {
  InSequence s;
  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId conn_id = TestConnectionId(1);
  ProcessPacket(client_address, conn_id, true, QuicStrCat("data packet ", 2),
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER,
                /*packet_number=*/2);

  // When CHLO arrives, a new session should be created, and all packets
  // buffered should be delivered to the session.
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(conn_id, client_address, QuicStringPiece(), _))
      .Times(1)  // Only triggered by 1st CHLO.
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, conn_id, client_address, &mock_helper_,
          &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(3)  // Triggered by 1 data packet and 2 CHLOs.
      .WillRepeatedly(
          WithArg<2>(Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(conn_id, packet);
          })));
  ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());

  ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());
}

// Tests that expiration of a connection add connection id to time wait list.
TEST_F(BufferedPacketStoreTest, ReceiveCHLOAfterExpiration) {
  InSequence s;
  CreateTimeWaitListManager();
  QuicBufferedPacketStore* store =
      QuicDispatcherPeer::GetBufferedPackets(dispatcher_.get());
  QuicBufferedPacketStorePeer::set_clock(store, mock_helper_.GetClock());

  QuicSocketAddress client_address(QuicIpAddress::Loopback4(), 1);
  QuicConnectionId conn_id = TestConnectionId(1);
  ProcessPacket(client_address, conn_id, true, QuicStrCat("data packet ", 2),
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER,
                /*packet_number=*/2);

  mock_helper_.AdvanceTime(
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs));
  QuicAlarm* alarm = QuicBufferedPacketStorePeer::expiration_alarm(store);
  // Cancel alarm as if it had been fired.
  alarm->Cancel();
  store->OnExpirationTimeout();
  // New arrived CHLO will be dropped because this connection is in time wait
  // list.
  ASSERT_TRUE(time_wait_list_manager_->IsConnectionIdInTimeWait(conn_id));
  EXPECT_CALL(*time_wait_list_manager_, ProcessPacket(_, _, conn_id, _, _));
  ProcessPacket(client_address, conn_id, true, SerializeFullCHLO());
}

TEST_F(BufferedPacketStoreTest, ProcessCHLOsUptoLimitAndBufferTheRest) {
  // Process more than (|kMaxNumSessionsToCreate| +
  // |kDefaultMaxConnectionsInStore|) CHLOs,
  // the first |kMaxNumSessionsToCreate| should create connections immediately,
  // the next |kDefaultMaxConnectionsInStore| should be buffered,
  // the rest should be dropped.
  QuicBufferedPacketStore* store =
      QuicDispatcherPeer::GetBufferedPackets(dispatcher_.get());
  const size_t kNumCHLOs =
      kMaxNumSessionsToCreate + kDefaultMaxConnectionsInStore + 1;
  for (uint64_t conn_id = 1; conn_id <= kNumCHLOs; ++conn_id) {
    EXPECT_CALL(
        *dispatcher_,
        ShouldCreateOrBufferPacketForConnection(
            ReceivedPacketInfoConnectionIdEquals(TestConnectionId(conn_id))));
    if (conn_id <= kMaxNumSessionsToCreate) {
      EXPECT_CALL(*dispatcher_,
                  CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                    QuicStringPiece(), _))
          .WillOnce(testing::Return(CreateSession(
              dispatcher_.get(), config_, TestConnectionId(conn_id),
              client_addr_, &mock_helper_, &mock_alarm_factory_,
              &crypto_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
              &session1_)));
      EXPECT_CALL(
          *reinterpret_cast<MockQuicConnection*>(session1_->connection()),
          ProcessUdpPacket(_, _, _))
          .WillOnce(WithArg<2>(
              Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
                ValidatePacket(TestConnectionId(conn_id), packet);
              })));
    }
    ProcessPacket(client_addr_, TestConnectionId(conn_id), true,
                  SerializeFullCHLO());
    if (conn_id <= kMaxNumSessionsToCreate + kDefaultMaxConnectionsInStore &&
        conn_id > kMaxNumSessionsToCreate) {
      EXPECT_TRUE(store->HasChloForConnection(TestConnectionId(conn_id)));
    } else {
      // First |kMaxNumSessionsToCreate| CHLOs should be passed to new
      // connections immediately, and the last CHLO should be dropped as the
      // store is full.
      EXPECT_FALSE(store->HasChloForConnection(TestConnectionId(conn_id)));
    }
  }

  // Graduately consume buffered CHLOs. The buffered connections should be
  // created but the dropped one shouldn't.
  for (uint64_t conn_id = kMaxNumSessionsToCreate + 1;
       conn_id <= kMaxNumSessionsToCreate + kDefaultMaxConnectionsInStore;
       ++conn_id) {
    EXPECT_CALL(*dispatcher_,
                CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                  QuicStringPiece(), _))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, TestConnectionId(conn_id), client_addr_,
            &mock_helper_, &mock_alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
                ProcessUdpPacket(_, _, _))
        .WillOnce(WithArg<2>(
            Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
              ValidatePacket(TestConnectionId(conn_id), packet);
            })));
  }
  EXPECT_CALL(*dispatcher_,
              CreateQuicSession(TestConnectionId(kNumCHLOs), client_addr_,
                                QuicStringPiece(), _))
      .Times(0);

  while (store->HasChlosBuffered()) {
    dispatcher_->ProcessBufferedChlos(kMaxNumSessionsToCreate);
  }

  EXPECT_EQ(TestConnectionId(static_cast<size_t>(kMaxNumSessionsToCreate) +
                             kDefaultMaxConnectionsInStore),
            session1_->connection_id());
}

// Duplicated CHLO shouldn't be buffered.
TEST_F(BufferedPacketStoreTest, BufferDuplicatedCHLO) {
  for (uint64_t conn_id = 1; conn_id <= kMaxNumSessionsToCreate + 1;
       ++conn_id) {
    // Last CHLO will be buffered. Others will create connection right away.
    if (conn_id <= kMaxNumSessionsToCreate) {
      EXPECT_CALL(*dispatcher_,
                  CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                    QuicStringPiece(), _))
          .WillOnce(testing::Return(CreateSession(
              dispatcher_.get(), config_, TestConnectionId(conn_id),
              client_addr_, &mock_helper_, &mock_alarm_factory_,
              &crypto_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
              &session1_)));
      EXPECT_CALL(
          *reinterpret_cast<MockQuicConnection*>(session1_->connection()),
          ProcessUdpPacket(_, _, _))
          .WillOnce(WithArg<2>(
              Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
                ValidatePacket(TestConnectionId(conn_id), packet);
              })));
    }
    ProcessPacket(client_addr_, TestConnectionId(conn_id), true,
                  SerializeFullCHLO());
  }
  // Retransmit CHLO on last connection should be dropped.
  QuicConnectionId last_connection =
      TestConnectionId(kMaxNumSessionsToCreate + 1);
  ProcessPacket(client_addr_, last_connection, true, SerializeFullCHLO());

  size_t packets_buffered = 2;

  // Reset counter and process buffered CHLO.
  EXPECT_CALL(*dispatcher_, CreateQuicSession(last_connection, client_addr_,
                                              QuicStringPiece(), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, last_connection, client_addr_,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  // Only one packet(CHLO) should be process.
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(packets_buffered)
      .WillRepeatedly(WithArg<2>(
          Invoke([this, last_connection](const QuicEncryptedPacket& packet) {
            ValidatePacket(last_connection, packet);
          })));
  dispatcher_->ProcessBufferedChlos(kMaxNumSessionsToCreate);
}

TEST_F(BufferedPacketStoreTest, BufferNonChloPacketsUptoLimitWithChloBuffered) {
  uint64_t last_conn_id = kMaxNumSessionsToCreate + 1;
  QuicConnectionId last_connection_id = TestConnectionId(last_conn_id);
  for (uint64_t conn_id = 1; conn_id <= last_conn_id; ++conn_id) {
    // Last CHLO will be buffered. Others will create connection right away.
    if (conn_id <= kMaxNumSessionsToCreate) {
      EXPECT_CALL(*dispatcher_,
                  CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                    QuicStringPiece(), _))
          .WillOnce(testing::Return(CreateSession(
              dispatcher_.get(), config_, TestConnectionId(conn_id),
              client_addr_, &mock_helper_, &mock_alarm_factory_,
              &crypto_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
              &session1_)));
      EXPECT_CALL(
          *reinterpret_cast<MockQuicConnection*>(session1_->connection()),
          ProcessUdpPacket(_, _, _))
          .WillRepeatedly(WithArg<2>(
              Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
                ValidatePacket(TestConnectionId(conn_id), packet);
              })));
    }
    ProcessPacket(client_addr_, TestConnectionId(conn_id), true,
                  SerializeFullCHLO());
  }

  // Process another |kDefaultMaxUndecryptablePackets| + 1 data packets. The
  // last one should be dropped.
  for (uint64_t packet_number = 2;
       packet_number <= kDefaultMaxUndecryptablePackets + 2; ++packet_number) {
    ProcessPacket(client_addr_, last_connection_id, true, "data packet");
  }

  // Reset counter and process buffered CHLO.
  EXPECT_CALL(*dispatcher_, CreateQuicSession(last_connection_id, client_addr_,
                                              QuicStringPiece(), _))
      .WillOnce(testing::Return(CreateSession(
          dispatcher_.get(), config_, last_connection_id, client_addr_,
          &mock_helper_, &mock_alarm_factory_, &crypto_config_,
          QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
  // Only CHLO and following |kDefaultMaxUndecryptablePackets| data packets
  // should be process.
  EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
              ProcessUdpPacket(_, _, _))
      .Times(kDefaultMaxUndecryptablePackets + 1)
      .WillRepeatedly(WithArg<2>(
          Invoke([this, last_connection_id](const QuicEncryptedPacket& packet) {
            ValidatePacket(last_connection_id, packet);
          })));
  dispatcher_->ProcessBufferedChlos(kMaxNumSessionsToCreate);
}

// Tests that when dispatcher's packet buffer is full, a CHLO on connection
// which doesn't have buffered CHLO should be buffered.
TEST_F(BufferedPacketStoreTest, ReceiveCHLOForBufferedConnection) {
  QuicBufferedPacketStore* store =
      QuicDispatcherPeer::GetBufferedPackets(dispatcher_.get());

  uint64_t conn_id = 1;
  ProcessPacket(client_addr_, TestConnectionId(conn_id), true, "data packet",
                CONNECTION_ID_PRESENT, PACKET_4BYTE_PACKET_NUMBER,
                /*packet_number=*/1);
  // Fill packet buffer to full with CHLOs on other connections. Need to feed
  // extra CHLOs because the first |kMaxNumSessionsToCreate| are going to create
  // session directly.
  for (conn_id = 2;
       conn_id <= kDefaultMaxConnectionsInStore + kMaxNumSessionsToCreate;
       ++conn_id) {
    if (conn_id <= kMaxNumSessionsToCreate + 1) {
      EXPECT_CALL(*dispatcher_,
                  CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                    QuicStringPiece(), _))
          .WillOnce(testing::Return(CreateSession(
              dispatcher_.get(), config_, TestConnectionId(conn_id),
              client_addr_, &mock_helper_, &mock_alarm_factory_,
              &crypto_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
              &session1_)));
      EXPECT_CALL(
          *reinterpret_cast<MockQuicConnection*>(session1_->connection()),
          ProcessUdpPacket(_, _, _))
          .WillOnce(WithArg<2>(
              Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
                ValidatePacket(TestConnectionId(conn_id), packet);
              })));
    }
    ProcessPacket(client_addr_, TestConnectionId(conn_id), true,
                  SerializeFullCHLO());
  }
  EXPECT_FALSE(store->HasChloForConnection(
      /*connection_id=*/TestConnectionId(1)));

  // CHLO on connection 1 should still be buffered.
  ProcessPacket(client_addr_, /*connection_id=*/TestConnectionId(1), true,
                SerializeFullCHLO());
  EXPECT_TRUE(store->HasChloForConnection(
      /*connection_id=*/TestConnectionId(1)));
}

// Regression test for b/117874922.
TEST_F(BufferedPacketStoreTest, ProcessBufferedChloWithDifferentVersion) {
  // Turn off version 99, such that the preferred version is not supported by
  // the server.
  SetQuicReloadableFlag(quic_enable_version_99, false);
  uint64_t last_connection_id = kMaxNumSessionsToCreate + 5;
  ParsedQuicVersionVector supported_versions = CurrentSupportedVersions();
  for (uint64_t conn_id = 1; conn_id <= last_connection_id; ++conn_id) {
    // Last 5 CHLOs will be buffered. Others will create connection right away.
    ParsedQuicVersion version =
        supported_versions[(conn_id - 1) % supported_versions.size()];
    if (conn_id <= kMaxNumSessionsToCreate) {
      EXPECT_CALL(*dispatcher_,
                  CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                    QuicStringPiece(), version))
          .WillOnce(testing::Return(CreateSession(
              dispatcher_.get(), config_, TestConnectionId(conn_id),
              client_addr_, &mock_helper_, &mock_alarm_factory_,
              &crypto_config_, QuicDispatcherPeer::GetCache(dispatcher_.get()),
              &session1_)));
      EXPECT_CALL(
          *reinterpret_cast<MockQuicConnection*>(session1_->connection()),
          ProcessUdpPacket(_, _, _))
          .WillRepeatedly(WithArg<2>(
              Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
                ValidatePacket(TestConnectionId(conn_id), packet);
              })));
    }
    ProcessPacket(client_addr_, TestConnectionId(conn_id), true, version,
                  SerializeFullCHLO(), CONNECTION_ID_PRESENT,
                  PACKET_4BYTE_PACKET_NUMBER, 1);
  }

  // Process buffered CHLOs. Verify the version is correct.
  for (uint64_t conn_id = kMaxNumSessionsToCreate + 1;
       conn_id <= last_connection_id; ++conn_id) {
    ParsedQuicVersion version =
        supported_versions[(conn_id - 1) % supported_versions.size()];
    EXPECT_CALL(*dispatcher_,
                CreateQuicSession(TestConnectionId(conn_id), client_addr_,
                                  QuicStringPiece(), version))
        .WillOnce(testing::Return(CreateSession(
            dispatcher_.get(), config_, TestConnectionId(conn_id), client_addr_,
            &mock_helper_, &mock_alarm_factory_, &crypto_config_,
            QuicDispatcherPeer::GetCache(dispatcher_.get()), &session1_)));
    EXPECT_CALL(*reinterpret_cast<MockQuicConnection*>(session1_->connection()),
                ProcessUdpPacket(_, _, _))
        .WillRepeatedly(WithArg<2>(
            Invoke([this, conn_id](const QuicEncryptedPacket& packet) {
              ValidatePacket(TestConnectionId(conn_id), packet);
            })));
  }
  dispatcher_->ProcessBufferedChlos(kMaxNumSessionsToCreate);
}

}  // namespace
}  // namespace test
}  // namespace quic
