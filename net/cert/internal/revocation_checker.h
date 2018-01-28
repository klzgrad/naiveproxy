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
