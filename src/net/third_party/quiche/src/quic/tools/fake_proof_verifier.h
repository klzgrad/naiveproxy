// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_FAKE_PROOF_VERIFIER_H_
#define QUICHE_QUIC_TOOLS_FAKE_PROOF_VERIFIER_H_

#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// ProofVerifier implementation which always returns success.
class FakeProofVerifier : public ProofVerifier {
 public:
  ~FakeProofVerifier() override {}
  QuicAsyncStatus VerifyProof(
      const std::string& /*hostname*/,
      const uint16_t /*port*/,
      const std::string& /*server_config*/,
      QuicTransportVersion /*quic_version*/,
      quiche::QuicheStringPiece /*chlo_hash*/,
      const std::vector<std::string>& /*certs*/,
      const std::string& /*cert_sct*/,
      const std::string& /*signature*/,
      const ProofVerifyContext* /*context*/,
      std::string* /*error_details*/,
      std::unique_ptr<ProofVerifyDetails>* /*details*/,
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

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_FAKE_PROOF_VERIFIER_H_
