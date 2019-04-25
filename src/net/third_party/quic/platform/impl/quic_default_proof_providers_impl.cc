// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_default_proof_providers_impl.h"

#include <utility>

#include "net/cert/cert_verifier.h"
#include "net/cert/ct_log_verifier.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/multi_log_ct_verifier.h"
#include "net/http/transport_security_state.h"
#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"

using net::CertVerifier;
using net::CTVerifier;
using net::MultiLogCTVerifier;
using net::ProofVerifierChromium;

namespace quic {

class ProofVerifierChromiumWithOwnership : public net::ProofVerifierChromium {
 public:
  ProofVerifierChromiumWithOwnership(
      std::unique_ptr<net::CertVerifier> cert_verifier)
      : net::ProofVerifierChromium(cert_verifier.get(),
                                   &ct_policy_enforcer_,
                                   &transport_security_state_,
                                   &ct_verifier_),
        cert_verifier_(std::move(cert_verifier)) {}

 private:
  std::unique_ptr<net::CertVerifier> cert_verifier_;
  net::DefaultCTPolicyEnforcer ct_policy_enforcer_;
  net::TransportSecurityState transport_security_state_;
  net::MultiLogCTVerifier ct_verifier_;
};

std::unique_ptr<ProofVerifier> CreateDefaultProofVerifierImpl() {
  std::unique_ptr<net::CertVerifier> cert_verifier =
      net::CertVerifier::CreateDefault();
  return QuicMakeUnique<ProofVerifierChromiumWithOwnership>(
      std::move(cert_verifier));
}

}  // namespace quic
