// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_REVOCATION_CHECKER_H_
#define NET_CERT_INTERNAL_REVOCATION_CHECKER_H_

#include "base/strings/string_piece_forward.h"
#include "net/base/net_export.h"
#include "net/cert/crl_set.h"
#include "net/cert/internal/parsed_certificate.h"

namespace net {

class CertPathErrors;
class CertNetFetcher;
struct CertificateTrust;

// RevocationPolicy describes how revocation should be carried out for a
// particular chain.
struct NET_EXPORT_PRIVATE RevocationPolicy {
  // Callers should not rely on the default-initialized value, but should fully
  // specify all the parameters.
  RevocationPolicy();

  // If |check_revocation| is true, then revocation checking is mandatory. This
  // means that every certificate in the chain (excluding trust anchors) must
  // have valid (unexpired) revocation information proving it to be unrevoked.
  //
  // The mechanisms used for checking revocation may include stapled OCSP,
  // cached OCSP, online OCSP, cached CRL, online CRL.
  //
  // The other properties of RevocationPolicy place further constraints on how
  // revocation checking may proceed.
  bool check_revocation : 1;

  // If |networking_allowed| is true then revocation checking is allowed to
  // issue network requests in order to fetch fresh OCSP/CRL. Otherwise
  // networking is not permitted in the course of revocation checking.
  bool networking_allowed : 1;

  // If set to true, considers certificates lacking URLs for OCSP/CRL to be
  // unrevoked. Otherwise will fail for certificates lacking revocation
  // mechanisms.
  bool allow_missing_info : 1;

  // If set to true, failure to perform online revocation checks (due to a
  // network level failure) is considered equivalent to a successful revocation
  // check.
  //
  // TODO(649017): The "soft fail" expectations of consumers are more broad than
  // this, and may also entail parsing failures and parsed non-success OCSP
  // responses.
  bool allow_network_failure : 1;
};

// Checks the revocation status of |certs| according to |policy|, and adds
// any failures to |errors|. On failure errors are added to |errors|. On success
// no errors are added.
//
// |net_fetcher| may be null, however this may lead to failed revocation checks
// depending on |policy|.
NET_EXPORT_PRIVATE void CheckCertChainRevocation(
    const ParsedCertificateList& certs,
    const CertificateTrust& last_cert_trust,
    const RevocationPolicy& policy,
    base::StringPiece stapled_leaf_ocsp_response,
    CertNetFetcher* net_fetcher,
    CertPathErrors* errors);

// Checks the revocation status of a certificate chain using the CRLSet and adds
// revocation errors to |errors|.
//
// Returns the revocation status of the leaf certificate:
//
// * CRLSet::REVOKED if any certificate in the chain is revoked. Also adds a
//   corresponding error for the certificate in |errors|.
//
// * CRLSet::GOOD if the leaf certificate is covered as GOOD by the CRLSet, and
//   none of the intermediates were revoked according to the CRLSet.
//
// * CRLSet::UNKNOWN if none of the certificates are known to be revoked, and
//   the revocation status of leaf certificate was UNKNOWN by the CRLSet.
NET_EXPORT_PRIVATE CRLSet::Result CheckChainRevocationUsingCRLSet(
    const CRLSet* crl_set,
    const ParsedCertificateList& certs,
    CertPathErrors* errors);

}  // namespace net

#endif  // NET_CERT_INTERNAL_REVOCATION_CHECKER_H_
