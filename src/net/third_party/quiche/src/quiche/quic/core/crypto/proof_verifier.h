// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_PROOF_VERIFIER_H_
#define QUICHE_QUIC_CORE_CRYPTO_PROOF_VERIFIER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// ProofVerifyDetails is an abstract class that acts as a container for any
// implementation specific details that a ProofVerifier wishes to return. These
// details are saved in the CachedState for the origin in question.
class QUICHE_EXPORT ProofVerifyDetails {
 public:
  virtual ~ProofVerifyDetails() {}

  // Returns an new ProofVerifyDetails object with the same contents
  // as this one.
  virtual ProofVerifyDetails* Clone() const = 0;
};

// ProofVerifyContext is an abstract class that acts as a container for any
// implementation specific context that a ProofVerifier needs.
class QUICHE_EXPORT ProofVerifyContext {
 public:
  virtual ~ProofVerifyContext() {}
};

// ProofVerifierCallback provides a generic mechanism for a ProofVerifier to
// call back after an asynchronous verification.
class QUICHE_EXPORT ProofVerifierCallback {
 public:
  virtual ~ProofVerifierCallback() {}

  // Run is called on the original thread to mark the completion of an
  // asynchonous verification. If |ok| is true then the certificate is valid
  // and |error_details| is unused. Otherwise, |error_details| contains a
  // description of the error. |details| contains implementation-specific
  // details of the verification. |Run| may take ownership of |details| by
  // calling |release| on it.
  virtual void Run(bool ok, const std::string& error_details,
                   std::unique_ptr<ProofVerifyDetails>* details) = 0;
};

// A ProofVerifier checks the signature on a server config, and the certificate
// chain that backs the public key.
class QUICHE_EXPORT ProofVerifier {
 public:
  virtual ~ProofVerifier() {}

  // VerifyProof checks that |signature| is a valid signature of
  // |server_config| by the public key in the leaf certificate of |certs|, and
  // that |certs| is a valid chain for |hostname|. On success, it returns
  // QUIC_SUCCESS. On failure, it returns QUIC_FAILURE and sets |*error_details|
  // to a description of the problem. In either case it may set |*details|,
  // which the caller takes ownership of.
  //
  // |context| specifies an implementation specific struct (which may be nullptr
  // for some implementations) that provides useful information for the
  // verifier, e.g. logging handles.
  //
  // This function may also return QUIC_PENDING, in which case the ProofVerifier
  // will call back, on the original thread, via |callback| when complete.
  //
  // The signature uses SHA-256 as the hash function and PSS padding in the
  // case of RSA.
  virtual QuicAsyncStatus VerifyProof(
      const std::string& hostname, uint16_t port,
      const std::string& server_config, QuicTransportVersion transport_version,
      absl::string_view chlo_hash, const std::vector<std::string>& certs,
      const std::string& cert_sct, const std::string& signature,
      const ProofVerifyContext* context, std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) = 0;

  // VerifyCertChain checks that |certs| is a valid chain for |hostname|. On
  // success, it returns QUIC_SUCCESS. On failure, it returns QUIC_FAILURE and
  // sets |*error_details| to a description of the problem. In either case it
  // may set |*details|, which the caller takes ownership of.
  //
  // |context| specifies an implementation specific struct (which may be nullptr
  // for some implementations) that provides useful information for the
  // verifier, e.g. logging handles.
  //
  // If certificate verification fails, a TLS alert will be sent when closing
  // the connection. This alert defaults to certificate_unknown. By setting
  // |*out_alert|, a different alert can be sent to provide a more specific
  // reason why verification failed.
  //
  // This function may also return QUIC_PENDING, in which case the ProofVerifier
  // will call back, on the original thread, via |callback| when complete.
  // In this case, the ProofVerifier will take ownership of |callback|.
  virtual QuicAsyncStatus VerifyCertChain(
      const std::string& hostname, uint16_t port,
      const std::vector<std::string>& certs, const std::string& ocsp_response,
      const std::string& cert_sct, const ProofVerifyContext* context,
      std::string* error_details, std::unique_ptr<ProofVerifyDetails>* details,
      uint8_t* out_alert, std::unique_ptr<ProofVerifierCallback> callback) = 0;

  // Returns a ProofVerifyContext instance which can be use for subsequent
  // verifications. Applications may chose create a different context and
  // supply it for verifications instead.
  virtual std::unique_ptr<ProofVerifyContext> CreateDefaultContext() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_PROOF_VERIFIER_H_
