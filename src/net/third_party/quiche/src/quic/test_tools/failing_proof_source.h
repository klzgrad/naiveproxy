// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_
#define QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_

#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {

class FailingProofSource : public ProofSource {
 public:
  void GetProof(const QuicSocketAddress& server_address,
                const std::string& hostname,
                const std::string& server_config,
                QuicTransportVersion transport_version,
                quiche::QuicheStringPiece chlo_hash,
                std::unique_ptr<Callback> callback) override;

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const std::string& hostname) override;

  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const std::string& hostname,
      uint16_t signature_algorithm,
      quiche::QuicheStringPiece in,
      std::unique_ptr<SignatureCallback> callback) override;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_
