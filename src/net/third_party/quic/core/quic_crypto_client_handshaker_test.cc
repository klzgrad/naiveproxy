// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_crypto_client_handshaker.h"
#include "net/third_party/quic/core/proto/crypto_server_config.pb.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {
namespace {

using ::testing::Test;

class TestProofHandler : public QuicCryptoClientStream::ProofHandler {
 public:
  ~TestProofHandler() override {}
  void OnProofValid(
      const QuicCryptoClientConfig::CachedState& cached) override {}
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override {}
};

class InsecureProofVerifier : public ProofVerifier {
 public:
  InsecureProofVerifier() {}
  ~InsecureProofVerifier() override {}

  // ProofVerifier override.
  QuicAsyncStatus VerifyProof(
      const QuicString& hostname,
      const uint16_t port,
      const QuicString& server_config,
      QuicTransportVersion transport_version,
      QuicStringPiece chlo_hash,
      const std::vector<QuicString>& certs,
      const QuicString& cert_sct,
      const QuicString& signature,
      const ProofVerifyContext* context,
      QuicString* error_details,
      std::unique_ptr<ProofVerifyDetails>* verify_details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }

  QuicAsyncStatus VerifyCertChain(
      const QuicString& hostname,
      const std::vector<QuicString>& certs,
      const ProofVerifyContext* context,
      QuicString* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }

  std::unique_ptr<ProofVerifyContext> CreateDefaultContext() override {
    return nullptr;
  }
};

class DummyProofSource : public ProofSource {
 public:
  DummyProofSource() {}
  ~DummyProofSource() override {}

  // ProofSource override.
  void GetProof(const QuicSocketAddress& server_address,
                const QuicString& hostname,
                const QuicString& server_config,
                QuicTransportVersion transport_version,
                QuicStringPiece chlo_hash,
                std::unique_ptr<Callback> callback) override {
    QuicReferenceCountedPointer<ProofSource::Chain> chain =
        GetCertChain(server_address, hostname);
    QuicCryptoProof proof;
    proof.signature = "Dummy signature";
    proof.leaf_cert_scts = "Dummy timestamp";
    callback->Run(true, chain, proof, nullptr /* details */);
  }

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicString& hostname) override {
    std::vector<QuicString> certs;
    certs.push_back("Dummy cert");
    return QuicReferenceCountedPointer<ProofSource::Chain>(
        new ProofSource::Chain(certs));
  }

  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicString& hostname,
      uint16_t signature_algorithm,
      QuicStringPiece in,
      std::unique_ptr<SignatureCallback> callback) override {
    callback->Run(true, "Dummy signature");
  }
};

class Handshaker : public QuicCryptoClientHandshaker {
 public:
  Handshaker(const QuicServerId& server_id,
             QuicCryptoClientStream* stream,
             QuicSession* session,
             std::unique_ptr<ProofVerifyContext> verify_context,
             QuicCryptoClientConfig* crypto_config,
             QuicCryptoClientStream::ProofHandler* proof_handler)
      : QuicCryptoClientHandshaker(server_id,
                                   stream,
                                   session,
                                   std::move(verify_context),
                                   crypto_config,
                                   proof_handler) {}

  void DoSendCHLOTest(QuicCryptoClientConfig::CachedState* cached) {
    QuicCryptoClientHandshaker::DoSendCHLO(cached);
  }
};

class QuicCryptoClientHandshakerTest : public Test {
 protected:
  QuicCryptoClientHandshakerTest()
      : proof_handler_(),
        helper_(),
        alarm_factory_(),
        server_id_("host", 123),
        connection_(new test::MockQuicConnection(&helper_,
                                                 &alarm_factory_,
                                                 Perspective::IS_CLIENT)),
        session_(connection_, false),
        crypto_client_config_(QuicMakeUnique<InsecureProofVerifier>(),
                              quic::TlsClientHandshaker::CreateSslCtx()),
        client_stream_(new QuicCryptoClientStream(server_id_,
                                                  &session_,
                                                  nullptr,
                                                  &crypto_client_config_,
                                                  &proof_handler_)),
        handshaker_(server_id_,
                    client_stream_,
                    &session_,
                    nullptr,
                    &crypto_client_config_,
                    &proof_handler_),
        state_() {
    // Session takes the ownership of the client stream! (but handshaker also
    // takes a reference to it, but doesn't take the ownership).
    session_.SetCryptoStream(client_stream_);
    session_.Initialize();
  }

  void InitializeServerParametersToEnableFullHello() {
    QuicCryptoServerConfig::ConfigOptions options;
    std::unique_ptr<QuicServerConfigProtobuf> config =
        QuicCryptoServerConfig::GenerateConfig(helper_.GetRandomGenerator(),
                                               helper_.GetClock(), options);
    state_.Initialize(
        config->config(), "sourcetoken", std::vector<QuicString>{"Dummy cert"},
        "", "chlo_hash", "signature", helper_.GetClock()->WallNow(),
        helper_.GetClock()->WallNow().Add(QuicTime::Delta::FromSeconds(30)));

    state_.SetProofValid();
  }

  TestProofHandler proof_handler_;
  test::MockQuicConnectionHelper helper_;
  test::MockAlarmFactory alarm_factory_;
  QuicServerId server_id_;
  // Session takes the ownership of the connection.
  test::MockQuicConnection* connection_;
  test::MockQuicSession session_;
  QuicCryptoClientConfig crypto_client_config_;
  QuicCryptoClientStream* client_stream_;
  Handshaker handshaker_;
  QuicCryptoClientConfig::CachedState state_;
};

TEST_F(QuicCryptoClientHandshakerTest, TestSendFullPaddingInInchoateHello) {
  handshaker_.DoSendCHLOTest(&state_);

  EXPECT_TRUE(connection_->fully_pad_during_crypto_handshake());
}

TEST_F(QuicCryptoClientHandshakerTest, TestDisabledPaddingInInchoateHello) {
  crypto_client_config_.set_pad_inchoate_hello(false);
  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_FALSE(connection_->fully_pad_during_crypto_handshake());
}

TEST_F(QuicCryptoClientHandshakerTest,
       TestPaddingInFullHelloEvenIfInchoateDisabled) {
  // Disable inchoate, but full hello should still be padded.
  crypto_client_config_.set_pad_inchoate_hello(false);

  InitializeServerParametersToEnableFullHello();

  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_TRUE(connection_->fully_pad_during_crypto_handshake());
}

TEST_F(QuicCryptoClientHandshakerTest, TestNoPaddingInFullHelloWhenDisabled) {
  crypto_client_config_.set_pad_full_hello(false);

  InitializeServerParametersToEnableFullHello();

  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_FALSE(connection_->fully_pad_during_crypto_handshake());
}

}  // namespace
}  // namespace quic
