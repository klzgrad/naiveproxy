// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_CRYPTO_HELPERS_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_CRYPTO_HELPERS_H_

#include "net/third_party/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quic/core/crypto/proof_source.h"
#include "net/third_party/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quic/core/crypto/quic_crypto_client_config.h"
#include "net/third_party/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quic/core/quic_connection_id.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quic/core/quic_versions.h"
#include "net/third_party/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// Never, ever, change this certificate name. You will break 0-rtt handshake if
// you do.
static constexpr char kDummyCertName[] = "Dummy cert";

struct CryptoServerConfig {
  std::unique_ptr<QuicCryptoServerConfig> config;
  QuicString serialized_crypto_config;
};

// Length of HKDF input keying material, equal to its number of bytes.
// https://tools.ietf.org/html/rfc5869#section-2.2.
// TODO(zhihuang): Verify that input keying material length is correct.
constexpr size_t kInputKeyingMaterialLength = 32;

// Used by QuicCryptoServerConfig to provide dummy proof credentials.
// TODO(zhihuang): Remove when secure P2P QUIC handshake is possible.
class DummyProofSource : public ProofSource {
 public:
  DummyProofSource() {}
  ~DummyProofSource() override {}

  // ProofSource overrides.
  void GetProof(const QuicSocketAddress& server_address,
                const QuicString& hostname,
                const QuicString& server_config,
                QuicTransportVersion transport_version,
                QuicStringPiece chlo_hash,
                std::unique_ptr<Callback> callback) override;

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicString& hostname) override;

  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicString& hostname,
      uint16_t signature_algorithm,
      QuicStringPiece in,
      std::unique_ptr<SignatureCallback> callback) override;
};

// Used by QuicCryptoClientConfig to ignore the peer's credentials
// and establish an insecure QUIC connection.
// TODO(zhihuang): Remove when secure P2P QUIC handshake is possible.
class InsecureProofVerifier : public ProofVerifier {
 public:
  InsecureProofVerifier() {}
  ~InsecureProofVerifier() override {}

  // ProofVerifier overrides.
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
      std::unique_ptr<ProofVerifierCallback> callback) override;

  QuicAsyncStatus VerifyCertChain(
      const QuicString& hostname,
      const std::vector<QuicString>& certs,
      const ProofVerifyContext* context,
      QuicString* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override;

  std::unique_ptr<ProofVerifyContext> CreateDefaultContext() override;
};

// Implementation of the server-side crypto stream helper.
class QuartcCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  QuicConnectionId GenerateConnectionIdForReject(
      QuicTransportVersion version,
      QuicConnectionId connection_id) const override;

  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            QuicString* error_details) const override;
};

std::unique_ptr<QuicCryptoClientConfig> CreateCryptoClientConfig(
    QuicStringPiece pre_shared_key);

CryptoServerConfig CreateCryptoServerConfig(QuicRandom* random,
                                            const QuicClock* clock,
                                            QuicStringPiece pre_shared_key);

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_CRYPTO_HELPERS_H_
