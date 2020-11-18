// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/failing_proof_source.h"
#include "net/third_party/quiche/src/quic/test_tools/fake_proof_source.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_session_cache.h"
#include "net/third_party/quiche/src/quic/test_tools/test_ticket_crypter.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
class QuicConnection;
class QuicStream;
}  // namespace quic

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace quic {
namespace test {

namespace {

const char kServerHostname[] = "test.example.com";
const uint16_t kServerPort = 443;

class TlsServerHandshakerTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  TlsServerHandshakerTest()
      : server_compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
        server_id_(kServerHostname, kServerPort, false),
        supported_versions_({GetParam()}) {
    SetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2, true);
    client_crypto_config_ = std::make_unique<QuicCryptoClientConfig>(
        crypto_test_utils::ProofVerifierForTesting(),
        std::make_unique<test::SimpleSessionCache>());
    InitializeServerConfig();
    InitializeServer();
    InitializeFakeClient();
  }

  ~TlsServerHandshakerTest() override {
    // Ensure that anything that might reference |helpers_| is destroyed before
    // |helpers_| is destroyed.
    server_session_.reset();
    client_session_.reset();
    helpers_.clear();
    alarm_factories_.clear();
  }

  void InitializeServerConfig() {
    auto ticket_crypter = std::make_unique<TestTicketCrypter>();
    ticket_crypter_ = ticket_crypter.get();
    auto proof_source = std::make_unique<FakeProofSource>();
    proof_source_ = proof_source.get();
    proof_source_->SetTicketCrypter(std::move(ticket_crypter));
    server_crypto_config_ = std::make_unique<QuicCryptoServerConfig>(
        QuicCryptoServerConfig::TESTING, QuicRandom::GetInstance(),
        std::move(proof_source), KeyExchangeSource::Default());
  }

  void InitializeServerConfigWithFailingProofSource() {
    server_crypto_config_ = std::make_unique<QuicCryptoServerConfig>(
        QuicCryptoServerConfig::TESTING, QuicRandom::GetInstance(),
        std::make_unique<FailingProofSource>(), KeyExchangeSource::Default());
  }

  // Initializes the crypto server stream state for testing.  May be
  // called multiple times.
  void InitializeServer() {
    TestQuicSpdyServerSession* server_session = nullptr;
    helpers_.push_back(std::make_unique<NiceMock<MockQuicConnectionHelper>>());
    alarm_factories_.push_back(std::make_unique<MockAlarmFactory>());
    CreateServerSessionForTest(
        server_id_, QuicTime::Delta::FromSeconds(100000), supported_versions_,
        helpers_.back().get(), alarm_factories_.back().get(),
        server_crypto_config_.get(), &server_compressed_certs_cache_,
        &server_connection_, &server_session);
    CHECK(server_session);
    server_session_.reset(server_session);
    EXPECT_CALL(*server_session_->helper(), CanAcceptClientHello(_, _, _, _, _))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*server_session_, SelectAlpn(_))
        .WillRepeatedly(
            [this](const std::vector<quiche::QuicheStringPiece>& alpns) {
              return std::find(
                  alpns.cbegin(), alpns.cend(),
                  AlpnForVersion(server_session_->connection()->version()));
            });
    crypto_test_utils::SetupCryptoServerConfigForTest(
        server_connection_->clock(), server_connection_->random_generator(),
        server_crypto_config_.get());
  }

  QuicCryptoServerStreamBase* server_stream() {
    return server_session_->GetMutableCryptoStream();
  }

  QuicCryptoClientStream* client_stream() {
    return client_session_->GetMutableCryptoStream();
  }

  // Initializes a fake client, and all its associated state, for
  // testing.  May be called multiple times.
  void InitializeFakeClient() {
    TestQuicSpdyClientSession* client_session = nullptr;
    helpers_.push_back(std::make_unique<NiceMock<MockQuicConnectionHelper>>());
    alarm_factories_.push_back(std::make_unique<MockAlarmFactory>());
    CreateClientSessionForTest(
        server_id_, QuicTime::Delta::FromSeconds(100000), supported_versions_,
        helpers_.back().get(), alarm_factories_.back().get(),
        client_crypto_config_.get(), &client_connection_, &client_session);
    const std::string default_alpn =
        AlpnForVersion(client_connection_->version());
    ON_CALL(*client_session, GetAlpnsToOffer())
        .WillByDefault(Return(std::vector<std::string>({default_alpn})));
    CHECK(client_session);
    client_session_.reset(client_session);
    moved_messages_counts_ = {0, 0};
  }

  void CompleteCryptoHandshake() {
    while (!client_stream()->one_rtt_keys_available() ||
           !server_stream()->one_rtt_keys_available()) {
      auto previous_moved_messages_counts = moved_messages_counts_;
      AdvanceHandshakeWithFakeClient();
      // Check that the handshake has made forward progress
      ASSERT_NE(previous_moved_messages_counts, moved_messages_counts_);
    }
  }

  // Performs a single round of handshake message-exchange between the
  // client and server.
  void AdvanceHandshakeWithFakeClient() {
    CHECK(server_connection_);
    CHECK(client_session_ != nullptr);

    EXPECT_CALL(*client_session_, OnProofValid(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*client_session_, OnProofVerifyDetailsAvailable(_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*client_connection_, OnCanWrite()).Times(testing::AnyNumber());
    EXPECT_CALL(*server_connection_, OnCanWrite()).Times(testing::AnyNumber());
    // Call CryptoConnect if we haven't moved any client messages yet.
    if (moved_messages_counts_.first == 0) {
      client_stream()->CryptoConnect();
    }
    moved_messages_counts_ = crypto_test_utils::AdvanceHandshake(
        client_connection_, client_stream(), moved_messages_counts_.first,
        server_connection_, server_stream(), moved_messages_counts_.second);
  }

  void ExpectHandshakeSuccessful() {
    EXPECT_TRUE(client_stream()->one_rtt_keys_available());
    EXPECT_TRUE(client_stream()->encryption_established());
    EXPECT_TRUE(server_stream()->one_rtt_keys_available());
    EXPECT_TRUE(server_stream()->encryption_established());
    EXPECT_EQ(HANDSHAKE_COMPLETE, client_stream()->GetHandshakeState());
    EXPECT_EQ(HANDSHAKE_CONFIRMED, server_stream()->GetHandshakeState());

    const auto& client_crypto_params =
        client_stream()->crypto_negotiated_params();
    const auto& server_crypto_params =
        server_stream()->crypto_negotiated_params();
    // The TLS params should be filled in on the client.
    EXPECT_NE(0, client_crypto_params.cipher_suite);
    EXPECT_NE(0, client_crypto_params.key_exchange_group);
    EXPECT_NE(0, client_crypto_params.peer_signature_algorithm);

    // The cipher suite and key exchange group should match on the client and
    // server.
    EXPECT_EQ(client_crypto_params.cipher_suite,
              server_crypto_params.cipher_suite);
    EXPECT_EQ(client_crypto_params.key_exchange_group,
              server_crypto_params.key_exchange_group);
    // We don't support client certs on the server (yet), so the server
    // shouldn't have a peer signature algorithm to report.
    EXPECT_EQ(0, server_crypto_params.peer_signature_algorithm);
  }

 protected:
  // Every connection gets its own MockQuicConnectionHelper and
  // MockAlarmFactory, tracked separately from the server and client state so
  // their lifetimes persist through the whole test.
  std::vector<std::unique_ptr<MockQuicConnectionHelper>> helpers_;
  std::vector<std::unique_ptr<MockAlarmFactory>> alarm_factories_;

  // Server state.
  PacketSavingConnection* server_connection_;
  std::unique_ptr<TestQuicSpdyServerSession> server_session_;
  TestTicketCrypter* ticket_crypter_;  // owned by proof_source_
  FakeProofSource* proof_source_;  // owned by server_crypto_config_
  std::unique_ptr<QuicCryptoServerConfig> server_crypto_config_;
  QuicCompressedCertsCache server_compressed_certs_cache_;
  QuicServerId server_id_;

  // Client state.
  PacketSavingConnection* client_connection_;
  std::unique_ptr<QuicCryptoClientConfig> client_crypto_config_;
  std::unique_ptr<TestQuicSpdyClientSession> client_session_;

  crypto_test_utils::FakeClientOptions client_options_;
  // How many handshake messages have been moved from client to server and
  // server to client.
  std::pair<size_t, size_t> moved_messages_counts_ = {0, 0};

  // Which QUIC versions the client and server support.
  ParsedQuicVersionVector supported_versions_;
};

INSTANTIATE_TEST_SUITE_P(TlsServerHandshakerTests,
                         TlsServerHandshakerTest,
                         ::testing::ValuesIn(AllSupportedVersionsWithTls()),
                         ::testing::PrintToStringParamName());

TEST_P(TlsServerHandshakerTest, NotInitiallyConected) {
  EXPECT_FALSE(server_stream()->encryption_established());
  EXPECT_FALSE(server_stream()->one_rtt_keys_available());
}

TEST_P(TlsServerHandshakerTest, ConnectedAfterTlsHandshake) {
  CompleteCryptoHandshake();
  EXPECT_EQ(PROTOCOL_TLS1_3, server_stream()->handshake_protocol());
  ExpectHandshakeSuccessful();
}

TEST_P(TlsServerHandshakerTest, HandshakeWithAsyncProofSource) {
  EXPECT_CALL(*client_connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_connection_, CloseConnection(_, _, _)).Times(0);
  // Enable FakeProofSource to capture call to ComputeTlsSignature and run it
  // asynchronously.
  proof_source_->Activate();

  // Start handshake.
  AdvanceHandshakeWithFakeClient();

  ASSERT_EQ(proof_source_->NumPendingCallbacks(), 1);
  proof_source_->InvokePendingCallback(0);

  CompleteCryptoHandshake();

  ExpectHandshakeSuccessful();
}

TEST_P(TlsServerHandshakerTest, CancelPendingProofSource) {
  EXPECT_CALL(*client_connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_connection_, CloseConnection(_, _, _)).Times(0);
  // Enable FakeProofSource to capture call to ComputeTlsSignature and run it
  // asynchronously.
  proof_source_->Activate();

  // Start handshake.
  AdvanceHandshakeWithFakeClient();

  ASSERT_EQ(proof_source_->NumPendingCallbacks(), 1);
  server_session_ = nullptr;

  proof_source_->InvokePendingCallback(0);
}

TEST_P(TlsServerHandshakerTest, ExtractSNI) {
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();

  EXPECT_EQ(server_stream()->crypto_negotiated_params().sni,
            "test.example.com");
}

TEST_P(TlsServerHandshakerTest, ConnectionClosedOnTlsError) {
  EXPECT_CALL(*server_connection_,
              CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  // Send a zero-length ClientHello from client to server.
  char bogus_handshake_message[] = {
      // Handshake struct (RFC 8446 appendix B.3)
      1,        // HandshakeType client_hello
      0, 0, 0,  // uint24 length
  };
  server_stream()->crypto_message_parser()->ProcessInput(
      quiche::QuicheStringPiece(bogus_handshake_message,
                                QUICHE_ARRAYSIZE(bogus_handshake_message)),
      ENCRYPTION_INITIAL);

  EXPECT_FALSE(server_stream()->one_rtt_keys_available());
}

TEST_P(TlsServerHandshakerTest, ClientSendingBadALPN) {
  const std::string kTestBadClientAlpn = "bad-client-alpn";
  EXPECT_CALL(*client_session_, GetAlpnsToOffer())
      .WillOnce(Return(std::vector<std::string>({kTestBadClientAlpn})));
  EXPECT_CALL(*server_connection_,
              CloseConnection(QUIC_HANDSHAKE_FAILED,
                              "TLS handshake failure (ENCRYPTION_INITIAL) 120: "
                              "no application protocol",
                              _));

  AdvanceHandshakeWithFakeClient();

  EXPECT_FALSE(client_stream()->one_rtt_keys_available());
  EXPECT_FALSE(client_stream()->encryption_established());
  EXPECT_FALSE(server_stream()->one_rtt_keys_available());
  EXPECT_FALSE(server_stream()->encryption_established());
}

TEST_P(TlsServerHandshakerTest, CustomALPNNegotiation) {
  EXPECT_CALL(*client_connection_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_connection_, CloseConnection(_, _, _)).Times(0);

  const std::string kTestAlpn = "A Custom ALPN Value";
  const std::vector<std::string> kTestAlpns(
      {"foo", "bar", kTestAlpn, "something else"});
  EXPECT_CALL(*client_session_, GetAlpnsToOffer())
      .WillRepeatedly(Return(kTestAlpns));
  EXPECT_CALL(*server_session_, SelectAlpn(_))
      .WillOnce([kTestAlpn, kTestAlpns](
                    const std::vector<quiche::QuicheStringPiece>& alpns) {
        EXPECT_THAT(alpns, testing::ElementsAreArray(kTestAlpns));
        return std::find(alpns.cbegin(), alpns.cend(), kTestAlpn);
      });
  EXPECT_CALL(*client_session_,
              OnAlpnSelected(quiche::QuicheStringPiece(kTestAlpn)));
  EXPECT_CALL(*server_session_,
              OnAlpnSelected(quiche::QuicheStringPiece(kTestAlpn)));

  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
}

TEST_P(TlsServerHandshakerTest, RejectInvalidSNI) {
  server_id_ = QuicServerId("invalid!.example.com", kServerPort, false);
  InitializeFakeClient();
  static_cast<TlsClientHandshaker*>(
      QuicCryptoClientStreamPeer::GetHandshaker(client_stream()))
      ->AllowInvalidSNIForTests();

  // Run the handshake and expect it to fail.
  AdvanceHandshakeWithFakeClient();
  EXPECT_FALSE(server_stream()->encryption_established());
  EXPECT_FALSE(server_stream()->one_rtt_keys_available());
}

TEST_P(TlsServerHandshakerTest, Resumption) {
  // Do the first handshake
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_FALSE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->ResumptionAttempted());

  // Now do another handshake
  InitializeServer();
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_TRUE(client_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->ResumptionAttempted());
}

TEST_P(TlsServerHandshakerTest, ResumptionWithAsyncDecryptCallback) {
  // Do the first handshake
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();

  ticket_crypter_->SetRunCallbacksAsync(true);
  // Now do another handshake
  InitializeServer();
  InitializeFakeClient();

  AdvanceHandshakeWithFakeClient();
  // Test that the DecryptCallback will be run asynchronously, and then run it.
  ASSERT_EQ(ticket_crypter_->NumPendingCallbacks(), 1u);
  ticket_crypter_->RunPendingCallback(0);

  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_TRUE(client_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->ResumptionAttempted());
}

TEST_P(TlsServerHandshakerTest, ResumptionWithFailingDecryptCallback) {
  // Do the first handshake
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();

  ticket_crypter_->set_fail_decrypt(true);
  // Now do another handshake
  InitializeServer();
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_FALSE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->ResumptionAttempted());
}

TEST_P(TlsServerHandshakerTest, ResumptionWithFailingAsyncDecryptCallback) {
  // Do the first handshake
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();

  ticket_crypter_->set_fail_decrypt(true);
  ticket_crypter_->SetRunCallbacksAsync(true);
  // Now do another handshake
  InitializeServer();
  InitializeFakeClient();

  AdvanceHandshakeWithFakeClient();
  // Test that the DecryptCallback will be run asynchronously, and then run it.
  ASSERT_EQ(ticket_crypter_->NumPendingCallbacks(), 1u);
  ticket_crypter_->RunPendingCallback(0);

  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_FALSE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->ResumptionAttempted());
}

TEST_P(TlsServerHandshakerTest, HandshakeFailsWithFailingProofSource) {
  InitializeServerConfigWithFailingProofSource();
  InitializeServer();
  InitializeFakeClient();

  // Attempt handshake.
  AdvanceHandshakeWithFakeClient();
  // Check that the server didn't send any handshake messages, because it failed
  // to handshake.
  EXPECT_EQ(moved_messages_counts_.second, 0u);
}

TEST_P(TlsServerHandshakerTest, ZeroRttResumption) {
  std::vector<uint8_t> application_state = {0, 1, 2, 3};

  // Do the first handshake
  server_stream()->SetServerApplicationStateForResumption(
      std::make_unique<ApplicationState>(application_state));
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_FALSE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsZeroRtt());

  // Now do another handshake
  InitializeServer();
  server_stream()->SetServerApplicationStateForResumption(
      std::make_unique<ApplicationState>(application_state));
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_TRUE(client_stream()->IsResumption());
  EXPECT_TRUE(server_stream()->IsZeroRtt());
}

TEST_P(TlsServerHandshakerTest, ZeroRttRejectOnApplicationStateChange) {
  std::vector<uint8_t> original_application_state = {1, 2};
  std::vector<uint8_t> new_application_state = {3, 4};

  // Do the first handshake
  server_stream()->SetServerApplicationStateForResumption(
      std::make_unique<ApplicationState>(original_application_state));
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_FALSE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsZeroRtt());

  // Do another handshake, but change the application state
  InitializeServer();
  server_stream()->SetServerApplicationStateForResumption(
      std::make_unique<ApplicationState>(new_application_state));
  InitializeFakeClient();
  CompleteCryptoHandshake();
  ExpectHandshakeSuccessful();
  EXPECT_TRUE(client_stream()->IsResumption());
  EXPECT_FALSE(server_stream()->IsZeroRtt());
}

}  // namespace
}  // namespace test
}  // namespace quic
