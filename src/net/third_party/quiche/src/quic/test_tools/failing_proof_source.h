// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_
#define QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_

#include "absl/strings/string_view.h"
#include "quic/core/crypto/proof_source.h"

namespace quic {
namespace test {

class FailingProofSource : public ProofSource {
 public:
  void GetProof(const QuicSocketAddress& server_address,
                const QuicSocketAddress& client_address,
                const std::string& hostname,
                const std::string& server_config,
                QuicTransportVersion transport_version,
                absl::string_view chlo_hash,
                std::unique_ptr<Callback> callback) override;

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname) override;

  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname,
      uint16_t signature_algorithm,
      absl::string_view in,
      std::unique_ptr<SignatureCallback> callback) override;

  TicketCrypter* GetTicketCrypter() override { return nullptr; }
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FAILING_PROOF_SOURCE_H_
