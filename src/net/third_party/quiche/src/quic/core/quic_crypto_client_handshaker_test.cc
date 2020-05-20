// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_crypto_client_handshaker.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/proto/crypto_server_config_proto.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace {

class TestProofHandler : public QuicCryptoClientStream::ProofHandler {
 public:
  ~TestProofHandler() override {}
  void OnProofValid(
      const QuicCryptoClientConfig::CachedState& /*cached*/) override {}
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& /*verify_details*/) override {}
};

class InsecureProofVerifier : public ProofVerifier {
 public:
  InsecureProofVerifier() {}
  ~InsecureProofVerifier() override {}

  // ProofVerifier override.
  QuicAsyncStatus VerifyProof(
      const std::string& /*hostname*/,
      const uint16_t /*port*/,
      const std::string& /*server_config*/,
      QuicTransportVersion /*transport_version*/,
      quiche::QuicheStringPiece /*chlo_hash*/,
      const std::vector<std::string>& /*certs*/,
      const std::string& /*cert_sct*/,
      const std::string& /*signature*/,
      const ProofVerifyContext* /*context*/,
      std::string* /*error_details*/,
      std::unique_ptr<ProofVerifyDetails>* /*verify_details*/,
      std::unique_ptr<ProofVerifierCallback> /*callback*/) override {
    return QUIC_SUCCESS;
  }

  QuicAsyncStatus VerifyCertChain(
      const std::string& /*hostname*/,
      const std::vector<std::string>& /*certs*/,
      const std::string& /*ocsp_response*/,
      const std::string& /*cert_sct*/,
      const ProofVerifyContext* /*context*/,
      std::string* /*error_details*/,
      std::unique_ptr<ProofVerifyDetails>* /*details*/,
      std::unique_ptr<ProofVerifierCallback> /*callback*/) override {
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
                const std::string& hostname,
                const std::string& /*server_config*/,
                QuicTransportVersion /*transport_version*/,
                quiche::QuicheStringPiece /*chlo_hash*/,
                std::unique_ptr<Callback> callback) override {
    QuicReferenceCountedPointer<ProofSource::Chain> chain =
        GetCertChain(server_address, hostname);
    QuicCryptoProof proof;
    proof.signature = "Dummy signature";
    proof.leaf_cert_scts = "Dummy timestamp";
    callback->Run(true, chain, proof, /*details=*/nullptr);
  }

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& /*server_address*/,
      const std::string& /*hostname*/) override {
    std::vector<std::string> certs;
    certs.push_back("Dummy cert");
    return QuicReferenceCountedPointer<ProofSource::Chain>(
        new ProofSource::Chain(certs));
  }

  void ComputeTlsSignature(
      const QuicSocketAddress& /*server_address*/,
      const std::string& /*hostname*/,
      uint16_t /*signature_algorit*/,
      quiche::QuicheStringPiece /*in*/,
      std::unique_ptr<SignatureCallback> callback) override {
    callback->Run(true, "Dummy signature", /*details=*/nullptr);
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

class QuicCryptoClientHandshakerTest
    : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  QuicCryptoClientHandshakerTest()
      : version_(GetParam()),
        proof_handler_(),
        helper_(),
        alarm_factory_(),
        server_id_("host", 123),
        connection_(new test::MockQuicConnection(&helper_,
                                                 &alarm_factory_,
                                                 Perspective::IS_CLIENT,
                                                 {version_})),
        session_(connection_, false),
        crypto_client_config_(std::make_unique<InsecureProofVerifier>()),
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
    QuicServerConfigProtobuf config = QuicCryptoServerConfig::GenerateConfig(
        helper_.GetRandomGenerator(), helper_.GetClock(), options);
    state_.Initialize(
        config.config(), "sourcetoken", std::vector<std::string>{"Dummy cert"},
        "", "chlo_hash", "signature", helper_.GetClock()->WallNow(),
        helper_.GetClock()->WallNow().Add(QuicTime::Delta::FromSeconds(30)));

    state_.SetProofValid();
  }

  ParsedQuicVersion version_;
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

INSTANTIATE_TEST_SUITE_P(
    QuicCryptoClientHandshakerTests,
    QuicCryptoClientHandshakerTest,
    ::testing::ValuesIn(AllSupportedVersionsWithQuicCrypto()),
    ::testing::PrintToStringParamName());

TEST_P(QuicCryptoClientHandshakerTest, TestSendFullPaddingInInchoateHello) {
  handshaker_.DoSendCHLOTest(&state_);

  EXPECT_TRUE(connection_->fully_pad_during_crypto_handshake());
}

TEST_P(QuicCryptoClientHandshakerTest, TestDisabledPaddingInInchoateHello) {
  crypto_client_config_.set_pad_inchoate_hello(false);
  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_FALSE(connection_->fully_pad_during_crypto_handshake());
}

TEST_P(QuicCryptoClientHandshakerTest,
       TestPaddingInFullHelloEvenIfInchoateDisabled) {
  // Disable inchoate, but full hello should still be padded.
  crypto_client_config_.set_pad_inchoate_hello(false);

  InitializeServerParametersToEnableFullHello();

  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_TRUE(connection_->fully_pad_during_crypto_handshake());
}

TEST_P(QuicCryptoClientHandshakerTest, TestNoPaddingInFullHelloWhenDisabled) {
  crypto_client_config_.set_pad_full_hello(false);

  InitializeServerParametersToEnableFullHello();

  handshaker_.DoSendCHLOTest(&state_);
  EXPECT_FALSE(connection_->fully_pad_during_crypto_handshake());
}

}  // namespace
}  // namespace quic
