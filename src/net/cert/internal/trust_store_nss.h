// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_TRUST_STORE_NSS_H_
#define NET_CERT_INTERNAL_TRUST_STORE_NSS_H_

#include <certt.h>

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/cert/internal/trust_store.h"

namespace net {

// TrustStoreNSS is an implementation of TrustStore which uses NSS to find trust
// anchors for path building.
class NET_EXPORT TrustStoreNSS : public TrustStore {
 public:
  // Creates a TrustStoreNSS which will find anchors that are trusted for
  // |trust_type|.
  explicit TrustStoreNSS(SECTrustType trust_type);
  ~TrustStoreNSS() override;

  // CertIssuerSource implementation:
  void SyncGetIssuersOf(const ParsedCertificate* cert,
                        ParsedCertificateList* issuers) override;

  // TrustStore implementation:
  void GetTrust(const scoped_refptr<ParsedCertificate>& cert,
                CertificateTrust* trust) const override;

 private:
  SECTrustType trust_type_;

  DISALLOW_COPY_AND_ASSIGN(TrustStoreNSS);
};

}  // namespace net

#endif  // NET_CERT_INTERNAL_TRUST_STORE_NSS_H_
