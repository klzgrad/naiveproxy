// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_TRUST_STORE_H_
#define NET_CERT_INTERNAL_TRUST_STORE_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/cert/internal/cert_issuer_source.h"
#include "net/cert/internal/parsed_certificate.h"

namespace net {

enum class CertificateTrustType {
  // This certificate is explicitly blacklisted (distrusted).
  DISTRUSTED,

  // The trustedness of this certificate is unknown (inherits trust from
  // its issuer).
  UNSPECIFIED,

  // This certificate is a trust anchor (as defined by RFC 5280). The only
  // fields in the certificate that are meaningful are its name and SPKI.
  TRUSTED_ANCHOR,

  // This certificate is a trust anchor, and additionally some of the fields in
  // the certificate (other than name and SPKI) should be used during the
  // verification process. See VerifyCertificateChain() for details on how
  // constraints are applied.
  TRUSTED_ANCHOR_WITH_CONSTRAINTS,
};

// Describes the level of trust in a certificate. See CertificateTrustType for
// details.
//
// TODO(eroman): Right now this is just a glorified wrapper around an enum...
struct NET_EXPORT CertificateTrust {
  static CertificateTrust ForTrustAnchor();
  static CertificateTrust ForTrustAnchorEnforcingConstraints();
  static CertificateTrust ForUnspecified();
  static CertificateTrust ForDistrusted();

  bool IsTrustAnchor() const;
  bool IsDistrusted() const;
  bool HasUnspecifiedTrust() const;

  CertificateTrustType type = CertificateTrustType::UNSPECIFIED;
};

// Interface for finding intermediates / trust anchors, and testing the
// trustedness of certificates.
class NET_EXPORT TrustStore : public CertIssuerSource {
 public:
  TrustStore();

  // Writes the trustedness of |cert| into |*trust|. Both |cert| and |trust|
  // must be non-null.
  virtual void GetTrust(const scoped_refptr<ParsedCertificate>& cert,
                        CertificateTrust* trust) const = 0;

  // Disable async issuers for TrustStore, as it isn't needed.
  void AsyncGetIssuersOf(const ParsedCertificate* cert,
                         std::unique_ptr<Request>* out_req) final;

 private:
  DISALLOW_COPY_AND_ASSIGN(TrustStore);
};

}  // namespace net

#endif  // NET_CERT_INTERNAL_TRUST_STORE_H_
