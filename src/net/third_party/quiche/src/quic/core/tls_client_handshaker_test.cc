// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "absl/base/macros.h"
#include "quic/core/crypto/quic_decrypter.h"
#include "quic/core/crypto/quic_encrypter.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_server_id.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_expect_bug.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/crypto_test_utils.h"
#include "quic/test_tools/quic_connection_peer.h"
#include "quic/test_tools/quic_framer_peer.h"
#include "quic/test_tools/quic_session_peer.h"
#include "quic/test_tools/quic_test_utils.h"
#include "quic/test_tools/simple_session_cache.h"
#include "quic/tools/fake_proof_verifier.h"
#include "common/test_tools/quiche_test_utils.h"

using testing::_;

namespace quic {
namespace test {
namespace {

constexpr char kServerHostname[] = "test.example.com";
constexpr uint16_t kServerPort = 443;

// TestProofVerifier wraps ProofVerifierForTesting, except for VerifyCertChain
// which, if TestProofVerifier is active, always returns QUIC_PENDING. (If this
// test proof verifier is not active, it delegates VerifyCertChain to the
// ProofVerifierForTesting.) The pending VerifyCertChain operation can be
// completed by calling InvokePendingCallback. This allows for testing
// asynchronous VerifyCertChain operations.
class TestProofVerifier : public ProofVerifier {
 public:
  TestProofVerifier()
      : verifier_(crypto_test_utils::ProofVerifierForTesting()) {}

  QuicAsyncStatus VerifyProof(
      const std::string& hostname,
      const uint16_t port,
      const std::string& server_config,
      QuicTransportVersion quic_version,
      absl::string_view chlo_hash,
      const std::vector<std::string>& certs,
      const std::string& cert_sct,
      const std::string& signature,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return verifier_->VerifyProof(
        hostname, port, server_config, quic_version, chlo_hash, certs, cert_sct,
        signature, context, error_details, details, std::move(callback));
  }

  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname,
      const uint16_t port,
      const std::vector<std::string>& certs,
      const std::string& ocsp_response,
      const std::string& cert_sct,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      uint8_t* out_alert,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    if (!active_) {
      return verifier_->VerifyCertChain(
          hostname, port, certs, ocsp_response, cert_sct, context,
          error_details, details, out_alert, std::move(callback));
    }
    pending_ops_.push_back(std::make_unique<VerifyChainPendingOp>(
        hostname, port, certs, ocsp_response, cert_sct, context, error_details,
        details, out_alert, std::move(callback), verifier_.get()));
    return QUIC_PENDING;
  }

  std::unique_ptr<ProofVerifyContext> CreateDefaultContext() override {
    return nullptr;
  }

  void Activate() { active_ = true; }

  size_t NumPendingCallbacks() const { return pending_ops_.size(); }

  void InvokePendingCallback(size_t n) {
    ASSERT_GT(NumPendingCallbacks(), n);
    pending_ops_[n]->Run();
    auto it = pending_ops_.begin() + n;
    pending_ops_.erase(it);
  }

 private:
  // Implementation of ProofVerifierCallback that fails if the callback is ever
  // run.
  class FailingProofVerifierCallback : public ProofVerifierCallback {
   public:
    void Run(bool /*ok*/,
             const std::string& /*error_details*/,
             std::unique_ptr<ProofVerifyDetails>* /*details*/) override {
      FAIL();
    }
  };

  class VerifyChainPendingOp {
   public:
    VerifyChainPendingOp(const std::string& hostname,
                         const uint16_t port,
                         const std::vector<std::string>& certs,
                         const std::string& ocsp_response,
                         const std::string& cert_sct,
                         const ProofVerifyContext* context,
                         std::string* error_details,
                         std::unique_ptr<ProofVerifyDetails>* details,
                         uint8_t* out_alert,
                         std::unique_ptr<ProofVerifierCallback> callback,
                         ProofVerifier* delegate)
        : hostname_(hostname),
          port_(port),
          certs_(certs),
          ocsp_response_(ocsp_response),
          cert_sct_(cert_sct),
          context_(context),
          error_details_(error_details),
          details_(details),
          out_alert_(out_alert),
          callback_(std::move(callback)),
          delegate_(delegate) {}

    void Run() {
      // TestProofVerifier depends on crypto_test_utils::ProofVerifierForTesting
      // running synchronously. It passes a FailingProofVerifierCallback and
      // runs the original callback after asserting that the verification ran
      // synchronously.
      QuicAsyncStatus status = delegate_->VerifyCertChain(
          hostname_, port_, certs_, ocsp_response_, cert_sct_, context_,
          error_details_, details_, out_alert_,
          std::make_unique<FailingProofVerifierCallback>());
      ASSERT_NE(status, QUIC_PENDING);
      callback_->Run(status == QUIC_SUCCESS, *error_details_, details_);
    }

   private:
    std::string hostname_;
    const uint16_t port_;
    std::vector<std::string> certs_;
    std::string ocsp_response_;
    std::string cert_sct_;
    const ProofVerifyContext* context_;
    std::string* error_details_;
    std::unique_ptr<ProofVerifyDetails>* details_;
    uint8_t* out_alert_;
    std::unique_ptr<ProofVerifierCallback> callback_;
    ProofVerifier* delegate_;
  };

  std::unique_ptr<ProofVerifier> verifier_;
  bool active_ = false;
  std::vector<std::unique_ptr<VerifyChainPendingOp>> pending_ops_;
};

class TlsClientHandshakerTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  TlsClientHandshakerTest()
      : supported_versions_({GetParam()}),
        server_id_(kServerHostname, kServerPort, false),
        server_compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize) {
    SetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2, true);
    crypto_config_ = std::make_unique<QuicCryptoClientConfig>(
        std::make_unique<TestProofVerifier>(),
        std::make_unique<test::SimpleSessionCache>());
    server_crypto_config_ = crypto_test_utils::CryptoServerConfigForTesting();
    CreateConnection();
  }

  void CreateSession() {
    session_ = std::make_unique<TestQuicSpdyClientSession>(
        connection_, DefaultQuicConfig(), supported_versions_, server_id_,
        crypto_config_.get());
    EXPECT_CALL(*session_, GetAlpnsToOffer())
        .WillRepeatedly(testing::Return(std::vector<std::string>(
            {AlpnForVersion(connection_->version())})));
  }

  void CreateConnection() {
    connection_ =
        new PacketSavingConnection(&client_helper_, &alarm_factory_,
                                   Perspective::IS_CLIENT, supported_versions_);
    // Advance the time, because timers do not like uninitialized times.
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
    CreateSession();
  }

  void CompleteCryptoHandshake() {
    CompleteCryptoHandshakeWithServerALPN(
        AlpnForVersion(connection_->version()));
  }

  void CompleteCryptoHandshakeWithServerALPN(const std::string& alpn) {
    EXPECT_CALL(*connection_, SendCryptoData(_, _, _))
        .Times(testing::AnyNumber());
    stream()->CryptoConnect();
    QuicConfig config;
    crypto_test_utils::HandshakeWithFakeServer(
        &config, server_crypto_config_.get(), &server_helper_, &alarm_factory_,
        connection_, stream(), alpn);
  }

  QuicCryptoClientStream* stream() {
    return session_->GetMutableCryptoStream();
  }

  QuicCryptoServerStreamBase* server_stream() {
    return server_session_->GetMutableCryptoStream();
  }

  // Initializes a fake server, and all its associated state, for testing.
  void InitializeFakeServer() {
    TestQuicSpdyServerSession* server_session = nullptr;
    CreateServerSessionForTest(
        server_id_, QuicTime::Delta::FromSeconds(100000), supported_versions_,
        &server_helper_, &alarm_factory_, server_crypto_config_.get(),
        &server_compressed_certs_cache_, &server_connection_, &server_session);
    server_session_.reset(server_session);
    std::string alpn = AlpnForVersion(connection_->version());
    EXPECT_CALL(*server_session_, SelectAlpn(_))
        .WillRepeatedly([alpn](const std::vector<absl::string_view>& alpns) {
          return std::find(alpns.cbegin(), alpns.cend(), alpn);
        });
  }

  MockQuicConnectionHelper server_helper_;
  MockQuicConnectionHelper client_helper_;
  MockAlarmFactory alarm_factory_;
  PacketSavingConnection* connection_;
  ParsedQuicVersionVector supported_versions_;
  std::unique_ptr<TestQuicSpdyClientSession> session_;
  QuicServerId server_id_;
  CryptoHandshakeMessage message_;
  std::unique_ptr<QuicCryptoClientConfig> crypto_config_;

  // Server state.
  std::unique_ptr<QuicCryptoServerConfig> server_crypto_config_;
  PacketSavingConnection* server_connection_;
  std::unique_ptr<TestQuicSpdyServerSession> server_session_;
  QuicCompressedCertsCache server_compressed_certs_cache_;
};

INSTANTIATE_TEST_SUITE_P(TlsHandshakerTests,
                         TlsClientHandshakerTest,
                         ::testing::ValuesIn(AllSupportedVersionsWithTls()),
                         ::testing::PrintToStringParamName());

TEST_P(TlsClientHandshakerTest, NotInitiallyConnected) {
  EXPECT_FALSE(stream()->encryption_established());
  EXPECT_FALSE(stream()->one_rtt_keys_available());
}

TEST_P(TlsClientHandshakerTest, ConnectedAfterHandshake) {
  CompleteCryptoHandshake();
  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());
}

TEST_P(TlsClientHandshakerTest, ConnectionClosedOnTlsError) {
  // Have client send ClientHello.
  stream()->CryptoConnect();
  if (GetQuicReloadableFlag(quic_send_tls_crypto_error_code)) {
    EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _, _));
  } else {
    EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));
  }

  // Send a zero-length ServerHello from server to client.
  char bogus_handshake_message[] = {
      // Handshake struct (RFC 8446 appendix B.3)
      2,        // HandshakeType server_hello
      0, 0, 0,  // uint24 length
  };
  stream()->crypto_message_parser()->ProcessInput(
      absl::string_view(bogus_handshake_message,
                        ABSL_ARRAYSIZE(bogus_handshake_message)),
      ENCRYPTION_INITIAL);

  EXPECT_FALSE(stream()->one_rtt_keys_available());
}

TEST_P(TlsClientHandshakerTest, ProofVerifyDetailsAvailableAfterHandshake) {
  EXPECT_CALL(*session_, OnProofVerifyDetailsAvailable(testing::_));
  stream()->CryptoConnect();
  QuicConfig config;
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &server_helper_, &alarm_factory_,
      connection_, stream(), AlpnForVersion(connection_->version()));
  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
}

TEST_P(TlsClientHandshakerTest, HandshakeWithAsyncProofVerifier) {
  InitializeFakeServer();

  // Enable TestProofVerifier to capture call to VerifyCertChain and run it
  // asynchronously.
  TestProofVerifier* proof_verifier =
      static_cast<TestProofVerifier*>(crypto_config_->proof_verifier());
  proof_verifier->Activate();

  stream()->CryptoConnect();
  // Exchange handshake messages.
  std::pair<size_t, size_t> moved_message_counts =
      crypto_test_utils::AdvanceHandshake(
          connection_, stream(), 0, server_connection_, server_stream(), 0);

  ASSERT_EQ(proof_verifier->NumPendingCallbacks(), 1u);
  proof_verifier->InvokePendingCallback(0);

  // Exchange more handshake messages.
  crypto_test_utils::AdvanceHandshake(
      connection_, stream(), moved_message_counts.first, server_connection_,
      server_stream(), moved_message_counts.second);

  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
}

TEST_P(TlsClientHandshakerTest, Resumption) {
  // Disable 0-RTT on the server so that we're only testing 1-RTT resumption:
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection
  CreateConnection();
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_TRUE(stream()->IsResumption());
}

TEST_P(TlsClientHandshakerTest, ResumptionRejection) {
  // Disable 0-RTT on the server before the first connection so the client
  // doesn't attempt a 0-RTT resumption, only a 1-RTT resumption.
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection, but disable resumption on the server.
  SSL_CTX_set_options(server_crypto_config_->ssl_ctx(), SSL_OP_NO_TICKET);
  CreateConnection();
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());
  EXPECT_FALSE(stream()->EarlyDataAccepted());
  EXPECT_EQ(stream()->EarlyDataReason(),
            ssl_early_data_unsupported_for_session);
}

TEST_P(TlsClientHandshakerTest, ZeroRttResumption) {
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection
  CreateConnection();
  // OnConfigNegotiated should be called twice - once when processing saved
  // 0-RTT transport parameters, and then again when receiving transport
  // parameters from the server.
  EXPECT_CALL(*session_, OnConfigNegotiated()).Times(2);
  EXPECT_CALL(*connection_, SendCryptoData(_, _, _))
      .Times(testing::AnyNumber());
  // Start the second handshake and confirm we have keys before receiving any
  // messages from the server.
  stream()->CryptoConnect();
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_NE(stream()->crypto_negotiated_params().cipher_suite, 0);
  EXPECT_NE(stream()->crypto_negotiated_params().key_exchange_group, 0);
  EXPECT_NE(stream()->crypto_negotiated_params().peer_signature_algorithm, 0);
  // Finish the handshake with the server.
  QuicConfig config;
  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &server_helper_, &alarm_factory_,
      connection_, stream(), AlpnForVersion(connection_->version()));

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_TRUE(stream()->IsResumption());
  EXPECT_TRUE(stream()->EarlyDataAccepted());
  EXPECT_EQ(stream()->EarlyDataReason(), ssl_early_data_accepted);
}

TEST_P(TlsClientHandshakerTest, ZeroRttRejection) {
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection, but disable 0-RTT on the server.
  SSL_CTX_set_early_data_enabled(server_crypto_config_->ssl_ctx(), false);
  CreateConnection();

  // OnConfigNegotiated should be called twice - once when processing saved
  // 0-RTT transport parameters, and then again when receiving transport
  // parameters from the server.
  EXPECT_CALL(*session_, OnConfigNegotiated()).Times(2);

  // 4 packets will be sent in this connection: initial handshake packet, 0-RTT
  // packet containing SETTINGS, handshake packet upon 0-RTT rejection, 0-RTT
  // packet retransmission.
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_INITIAL, NOT_RETRANSMISSION));
  if (VersionUsesHttp3(session_->transport_version())) {
    EXPECT_CALL(*connection_,
                OnPacketSent(ENCRYPTION_ZERO_RTT, NOT_RETRANSMISSION));
  }
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_HANDSHAKE, NOT_RETRANSMISSION));
  if (VersionUsesHttp3(session_->transport_version())) {
    // TODO(b/158027651): change transmission type to
    // ALL_ZERO_RTT_RETRANSMISSION.
    EXPECT_CALL(*connection_,
                OnPacketSent(ENCRYPTION_FORWARD_SECURE, LOSS_RETRANSMISSION));
  }

  CompleteCryptoHandshake();

  QuicFramer* framer = QuicConnectionPeer::GetFramer(connection_);
  EXPECT_EQ(nullptr, QuicFramerPeer::GetEncrypter(framer, ENCRYPTION_ZERO_RTT));

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_TRUE(stream()->IsResumption());
  EXPECT_FALSE(stream()->EarlyDataAccepted());
  EXPECT_EQ(stream()->EarlyDataReason(), ssl_early_data_peer_declined);
}

TEST_P(TlsClientHandshakerTest, ZeroRttAndResumptionRejection) {
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection, but disable resumption on the server.
  SSL_CTX_set_options(server_crypto_config_->ssl_ctx(), SSL_OP_NO_TICKET);
  CreateConnection();

  // OnConfigNegotiated should be called twice - once when processing saved
  // 0-RTT transport parameters, and then again when receiving transport
  // parameters from the server.
  EXPECT_CALL(*session_, OnConfigNegotiated()).Times(2);

  // 4 packets will be sent in this connection: initial handshake packet, 0-RTT
  // packet containing SETTINGS, handshake packet upon 0-RTT rejection, 0-RTT
  // packet retransmission.
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_INITIAL, NOT_RETRANSMISSION));
  if (VersionUsesHttp3(session_->transport_version())) {
    EXPECT_CALL(*connection_,
                OnPacketSent(ENCRYPTION_ZERO_RTT, NOT_RETRANSMISSION));
  }
  EXPECT_CALL(*connection_,
              OnPacketSent(ENCRYPTION_HANDSHAKE, NOT_RETRANSMISSION));
  if (VersionUsesHttp3(session_->transport_version())) {
    // TODO(b/158027651): change transmission type to
    // ALL_ZERO_RTT_RETRANSMISSION.
    EXPECT_CALL(*connection_,
                OnPacketSent(ENCRYPTION_FORWARD_SECURE, LOSS_RETRANSMISSION));
  }

  CompleteCryptoHandshake();

  QuicFramer* framer = QuicConnectionPeer::GetFramer(connection_);
  EXPECT_EQ(nullptr, QuicFramerPeer::GetEncrypter(framer, ENCRYPTION_ZERO_RTT));

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());
  EXPECT_FALSE(stream()->EarlyDataAccepted());
  EXPECT_EQ(stream()->EarlyDataReason(), ssl_early_data_session_not_resumed);
}

TEST_P(TlsClientHandshakerTest, ClientSendsNoSNI) {
  // Reconfigure client to sent an empty server hostname. The crypto config also
  // needs to be recreated to use a FakeProofVerifier since the server's cert
  // won't match the empty hostname.
  server_id_ = QuicServerId("", 443);
  crypto_config_.reset(new QuicCryptoClientConfig(
      std::make_unique<FakeProofVerifier>(), nullptr));
  CreateConnection();
  InitializeFakeServer();

  stream()->CryptoConnect();
  crypto_test_utils::CommunicateHandshakeMessages(
      connection_, stream(), server_connection_, server_stream());

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());

  EXPECT_EQ(server_stream()->crypto_negotiated_params().sni, "");
}

TEST_P(TlsClientHandshakerTest, ClientSendingTooManyALPNs) {
  std::string long_alpn(250, 'A');
  EXPECT_CALL(*session_, GetAlpnsToOffer())
      .WillOnce(testing::Return(std::vector<std::string>({
          long_alpn + "1",
          long_alpn + "2",
          long_alpn + "3",
          long_alpn + "4",
          long_alpn + "5",
          long_alpn + "6",
          long_alpn + "7",
          long_alpn + "8",
      })));
  EXPECT_QUIC_BUG(stream()->CryptoConnect(), "Failed to set ALPN");
}

TEST_P(TlsClientHandshakerTest, ServerRequiresCustomALPN) {
  InitializeFakeServer();
  const std::string kTestAlpn = "An ALPN That Client Did Not Offer";
  EXPECT_CALL(*server_session_, SelectAlpn(_))
      .WillOnce([kTestAlpn](const std::vector<absl::string_view>& alpns) {
        return std::find(alpns.cbegin(), alpns.cend(), kTestAlpn);
      });
  if (GetQuicReloadableFlag(quic_send_tls_crypto_error_code)) {
    EXPECT_CALL(
        *server_connection_,
        CloseConnection(
            QUIC_HANDSHAKE_FAILED,
            static_cast<QuicIetfTransportErrorCodes>(CRYPTO_ERROR_FIRST + 120),
            "TLS handshake failure (ENCRYPTION_INITIAL) 120: "
            "no application protocol",
            _));
  } else {
    EXPECT_CALL(
        *server_connection_,
        CloseConnection(QUIC_HANDSHAKE_FAILED,
                        "TLS handshake failure (ENCRYPTION_INITIAL) 120: "
                        "no application protocol",
                        _));
  }

  stream()->CryptoConnect();
  crypto_test_utils::AdvanceHandshake(connection_, stream(), 0,
                                      server_connection_, server_stream(), 0);

  EXPECT_FALSE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(server_stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->encryption_established());
  EXPECT_FALSE(server_stream()->encryption_established());
}

TEST_P(TlsClientHandshakerTest, ZeroRTTNotAttemptedOnALPNChange) {
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->IsResumption());

  // Create a second connection
  CreateConnection();
  // Override the ALPN to send on the second connection.
  const std::string kTestAlpn = "Test ALPN";
  EXPECT_CALL(*session_, GetAlpnsToOffer())
      .WillRepeatedly(testing::Return(std::vector<std::string>({kTestAlpn})));
  // OnConfigNegotiated should only be called once: when transport parameters
  // are received from the server.
  EXPECT_CALL(*session_, OnConfigNegotiated()).Times(1);

  CompleteCryptoHandshakeWithServerALPN(kTestAlpn);
  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());
  EXPECT_FALSE(stream()->EarlyDataAccepted());
  EXPECT_EQ(stream()->EarlyDataReason(), ssl_early_data_alpn_mismatch);
}

TEST_P(TlsClientHandshakerTest, InvalidSNI) {
  // Test that a client will skip sending SNI if configured to send an invalid
  // hostname. In this case, the inclusion of '!' is invalid.
  server_id_ = QuicServerId("invalid!.example.com", 443);
  crypto_config_.reset(new QuicCryptoClientConfig(
      std::make_unique<FakeProofVerifier>(), nullptr));
  CreateConnection();
  InitializeFakeServer();

  stream()->CryptoConnect();
  crypto_test_utils::CommunicateHandshakeMessages(
      connection_, stream(), server_connection_, server_stream());

  EXPECT_EQ(PROTOCOL_TLS1_3, stream()->handshake_protocol());
  EXPECT_TRUE(stream()->encryption_established());
  EXPECT_TRUE(stream()->one_rtt_keys_available());

  EXPECT_EQ(server_stream()->crypto_negotiated_params().sni, "");
}

TEST_P(TlsClientHandshakerTest, BadTransportParams) {
  if (!connection_->version().UsesHttp3()) {
    return;
  }
  // Finish establishing the first connection:
  CompleteCryptoHandshake();

  // Create a second connection
  CreateConnection();

  stream()->CryptoConnect();
  auto* id_manager = QuicSessionPeer::ietf_streamid_manager(session_.get());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            id_manager->max_outgoing_bidirectional_streams());
  QuicConfig config;
  config.SetMaxBidirectionalStreamsToSend(
      config.GetMaxBidirectionalStreamsToSend() - 1);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_ZERO_RTT_REJECTION_LIMIT_REDUCED, _, _))
      .WillOnce(testing::Invoke(connection_,
                                &MockQuicConnection::ReallyCloseConnection));
  // Close connection will be called again in the handshaker, but this will be
  // no-op as the connection is already closed.
  EXPECT_CALL(*connection_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  crypto_test_utils::HandshakeWithFakeServer(
      &config, server_crypto_config_.get(), &server_helper_, &alarm_factory_,
      connection_, stream(), AlpnForVersion(connection_->version()));
}

}  // namespace
}  // namespace test
}  // namespace quic
