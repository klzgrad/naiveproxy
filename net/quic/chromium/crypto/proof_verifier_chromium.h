// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_CRYPTO_PROOF_VERIFIER_CHROMIUM_H_
#define NET_QUIC_CHROMIUM_CRYPTO_PROOF_VERIFIER_CHROMIUM_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/ct_verify_result.h"
#include "net/cert/x509_certificate.h"
#include "net/log/net_log_with_source.h"
#include "net/quic/core/crypto/proof_verifier.h"

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class CTVerifier;
class TransportSecurityState;

// ProofVerifyDetailsChromium is the implementation-specific information that a
// ProofVerifierChromium returns about a certificate verification.
class NET_EXPORT_PRIVATE ProofVerifyDetailsChromium
    : public ProofVerifyDetails {
 public:
  ProofVerifyDetailsChromium();
  ProofVerifyDetailsChromium(const ProofVerifyDetailsChromium&);
  ~ProofVerifyDetailsChromium() override;

  // ProofVerifyDetails implementation
  ProofVerifyDetails* Clone() const override;

  CertVerifyResult cert_verify_result;
  ct::CTVerifyResult ct_verify_result;

  // pinning_failure_log contains a message produced by
  // TransportSecurityState::PKPState::CheckPublicKeyPins in the event of a
  // pinning failure. It is a (somewhat) human-readable string.
  std::string pinning_failure_log;

  // True if PKP was bypassed due to a local trust anchor.
  bool pkp_bypassed;
};

// ProofVerifyContextChromium is the implementation-specific information that a
// ProofVerifierChromium needs in order to log correctly.
struct ProofVerifyContextChromium : public ProofVerifyContext {
 public:
  ProofVerifyContextChromium(int cert_verify_flags,
                             const NetLogWithSource& net_log)
      : cert_verify_flags(cert_verify_flags), net_log(net_log) {}

  int cert_verify_flags;
  NetLogWithSource net_log;
};

// ProofVerifierChromium implements the QUIC ProofVerifier interface.  It is
// capable of handling multiple simultaneous requests.
class NET_EXPORT_PRIVATE ProofVerifierChromium : public ProofVerifier {
 public:
  ProofVerifierChromium(CertVerifier* cert_verifier,
                        CTPolicyEnforcer* ct_policy_enforcer,
                        TransportSecurityState* transport_security_state,
                        CTVerifier* cert_transparency_verifier);
  ~ProofVerifierChromium() override;

  // ProofVerifier interface
  QuicAsyncStatus VerifyProof(
      const std::string& hostname,
      const uint16_t port,
      const std::string& server_config,
      QuicTransportVersion quic_version,
      QuicStringPiece chlo_hash,
      const std::vector<std::string>& certs,
      const std::string& cert_sct,
      const std::string& signature,
      const ProofVerifyContext* verify_context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* verify_details,
      std::unique_ptr<ProofVerifierCallback> callback) override;
  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname,
      const std::vector<std::string>& certs,
      const ProofVerifyContext* verify_context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* verify_details,
      std::unique_ptr<ProofVerifierCallback> callback) override;

 private:
  class Job;

  void OnJobComplete(Job* job);

  // Set owning pointers to active jobs.
  std::map<Job*, std::unique_ptr<Job>> active_jobs_;

  // Underlying verifier used to verify certificates.
  CertVerifier* const cert_verifier_;
  CTPolicyEnforcer* const ct_policy_enforcer_;

  TransportSecurityState* const transport_security_state_;
  CTVerifier* const cert_transparency_verifier_;

  DISALLOW_COPY_AND_ASSIGN(ProofVerifierChromium);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_CRYPTO_PROOF_VERIFIER_CHROMIUM_H_
