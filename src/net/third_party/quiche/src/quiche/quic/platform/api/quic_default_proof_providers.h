// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_DEFAULT_PROOF_PROVIDERS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_DEFAULT_PROOF_PROVIDERS_H_

#include <memory>
#include <string>

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/common/platform/api/quiche_default_proof_providers.h"

namespace quic {

// Provides a default proof verifier that can verify a cert chain for |host|.
// The verifier has to do a good faith attempt at verifying the certificate
// against a reasonable root store, and not just always return success.
inline std::unique_ptr<ProofVerifier> CreateDefaultProofVerifier(
    const std::string& host) {
  return quiche::CreateDefaultProofVerifier(host);
}

// Provides a default proof source for CLI-based tools.  The actual certificates
// used in the proof source should be confifgurable via command-line flags.
inline std::unique_ptr<ProofSource> CreateDefaultProofSource() {
  return quiche::CreateDefaultProofSource();
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_DEFAULT_PROOF_PROVIDERS_H_
