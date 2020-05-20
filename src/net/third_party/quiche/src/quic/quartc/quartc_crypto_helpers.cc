// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_crypto_helpers.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {

void DummyProofSource::GetProof(const QuicSocketAddress& server_address,
                                const std::string& hostname,
                                const std::string& /*server_config*/,
                                QuicTransportVersion /*transport_version*/,
                                quiche::QuicheStringPiece /*chlo_hash*/,
                                std::unique_ptr<Callback> callback) {
  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      GetCertChain(server_address, hostname);
  QuicCryptoProof proof;
  proof.signature = "Dummy signature";
  proof.leaf_cert_scts = "Dummy timestamp";
  callback->Run(true, chain, proof, nullptr /* details */);
}

QuicReferenceCountedPointer<DummyProofSource::Chain>
DummyProofSource::GetCertChain(const QuicSocketAddress& /*server_address*/,
                               const std::string& /*hostname*/) {
  std::vector<std::string> certs;
  certs.push_back(kDummyCertName);
  return QuicReferenceCountedPointer<ProofSource::Chain>(
      new ProofSource::Chain(certs));
}

void DummyProofSource::ComputeTlsSignature(
    const QuicSocketAddress& /*server_address*/,
    const std::string& /*hostname*/,
    uint16_t /*signature_algorithm*/,
    quiche::QuicheStringPiece /*in*/,
    std::unique_ptr<SignatureCallback> callback) {
  callback->Run(true, "Dummy signature", /*details=*/nullptr);
}

QuicAsyncStatus InsecureProofVerifier::VerifyProof(
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
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  return QUIC_SUCCESS;
}

QuicAsyncStatus InsecureProofVerifier::VerifyCertChain(
    const std::string& /*hostname*/,
    const std::vector<std::string>& /*certs*/,
    const std::string& /*ocsp_response*/,
    const std::string& /*cert_sct*/,
    const ProofVerifyContext* /*context*/,
    std::string* /*error_details*/,
    std::unique_ptr<ProofVerifyDetails>* /*details*/,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  return QUIC_SUCCESS;
}

std::unique_ptr<ProofVerifyContext>
InsecureProofVerifier::CreateDefaultContext() {
  return nullptr;
}

bool QuartcCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& /*message*/,
    const QuicSocketAddress& /*client_address*/,
    const QuicSocketAddress& /*peer_address*/,
    const QuicSocketAddress& /*self_address*/,
    std::string* /*error_details*/) const {
  return true;
}

std::unique_ptr<QuicCryptoClientConfig> CreateCryptoClientConfig(
    quiche::QuicheStringPiece pre_shared_key) {
  auto config = std::make_unique<QuicCryptoClientConfig>(
      std::make_unique<InsecureProofVerifier>());
  config->set_pad_inchoate_hello(false);
  config->set_pad_full_hello(false);
  if (!pre_shared_key.empty()) {
    config->set_pre_shared_key(pre_shared_key);
  }
  return config;
}

CryptoServerConfig CreateCryptoServerConfig(
    QuicRandom* random,
    const QuicClock* clock,
    quiche::QuicheStringPiece pre_shared_key) {
  CryptoServerConfig crypto_server_config;

  // Generate a random source address token secret. For long-running servers
  // it's better to not regenerate it for each connection to enable zero-RTT
  // handshakes, but for transient clients it does not matter.
  char source_address_token_secret[kInputKeyingMaterialLength];
  random->RandBytes(source_address_token_secret, kInputKeyingMaterialLength);
  auto config = std::make_unique<QuicCryptoServerConfig>(
      std::string(source_address_token_secret, kInputKeyingMaterialLength),
      random, std::make_unique<DummyProofSource>(),
      KeyExchangeSource::Default());

  // We run QUIC over ICE, and ICE is verifying remote side with STUN pings.
  // We disable source address token validation in order to allow for 0-rtt
  // setup (plus source ip addresses are changing even during the connection
  // when ICE is used).
  config->set_validate_source_address_token(false);

  // Effectively disables the anti-amplification measures (we don't need
  // them because we use ICE, and we need to disable them because we disable
  // padding of crypto packets).
  // This multiplier must be large enough so that the crypto handshake packet
  // (approx. 300 bytes) multiplied by this multiplier is larger than a fully
  // sized packet (currently 1200 bytes).
  // 1500 is a bit extreme: if you can imagine sending a 1 byte packet, and
  // your largest MTU would be below 1500 bytes, 1500*1 >=
  // any_packet_that_you_can_imagine_sending.
  // (again, we hardcode packet size to 1200, so we are not dealing with jumbo
  // frames).
  config->set_chlo_multiplier(1500);

  // We are sending small client hello, we must not validate its size.
  config->set_validate_chlo_size(false);

  // Provide server with serialized config string to prove ownership.
  QuicCryptoServerConfig::ConfigOptions options;
  // The |message| is used to handle the return value of AddDefaultConfig
  // which is raw pointer of the CryptoHandshakeMessage.
  std::unique_ptr<CryptoHandshakeMessage> message(
      config->AddDefaultConfig(random, clock, options));
  config->set_pad_rej(false);
  config->set_pad_shlo(false);
  if (!pre_shared_key.empty()) {
    config->set_pre_shared_key(pre_shared_key);
  }
  crypto_server_config.config = std::move(config);
  const QuicData& data = message->GetSerialized();

  crypto_server_config.serialized_crypto_config =
      std::string(data.data(), data.length());
  return crypto_server_config;
}

}  // namespace quic
