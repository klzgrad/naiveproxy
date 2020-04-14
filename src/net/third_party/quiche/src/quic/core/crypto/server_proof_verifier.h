// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_SERVER_PROOF_VERIFIER_H_
#define QUICHE_QUIC_CORE_CRYPTO_SERVER_PROOF_VERIFIER_H_

#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

// A ServerProofVerifier checks the certificate chain presented by a client.
class QUIC_EXPORT_PRIVATE ServerProofVerifier {
 public:
  virtual ~ServerProofVerifier() {}

  // VerifyCertChain checks that |certs| is a valid chain. On success, it
  // returns QUIC_SUCCESS. On failure, it returns QUIC_FAILURE and sets
  // |*error_details| to a description of the problem. In either case it may set
  // |*details|, which the caller takes ownership of.
  //
  // |context| specifies an implementation specific struct (which may be nullptr
  // for some implementations) that provides useful information for the
  // verifier, e.g. logging handles.
  //
  // This function may also return QUIC_PENDING, in which case the
  // ServerProofVerifier will call back, on the original thread, via |callback|
  // when complete. In this case, the ServerProofVerifier will take ownership of
  // |callback|.
  virtual QuicAsyncStatus VerifyCertChain(
      const std::vector<std::string>& certs,
      std::string* error_details,
      std::unique_ptr<ProofVerifierCallback> callback) = 0;
};

}  // namespace quic
#endif  // QUICHE_QUIC_CORE_CRYPTO_SERVER_PROOF_VERIFIER_H_
