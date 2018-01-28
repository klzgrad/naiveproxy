// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_TRUST_STORE_MAC_H_
#define NET_CERT_INTERNAL_TRUST_STORE_MAC_H_

#include <CoreFoundation/CoreFoundation.h>

#include "base/gtest_prod_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/cert/internal/trust_store.h"

namespace net {

// TrustStoreMac is an implementation of TrustStore which uses macOS keychain
// to find trust anchors for path building.
// TODO(mattm): handle distrust of intermediates.
class NET_EXPORT TrustStoreMac : public TrustStore {
 public:
  // Creates a TrustStoreMac which will find anchors that are trusted for
  // |policy_oid|. For list of possible policy values, see:
  // https://developer.apple.com/reference/security/1667150-certificate_key_and_trust_servic/1670151-standard_policies_for_specific_c?language=objc
  // TODO(mattm): policy oids are actually CFStrings, but the constants are
  // defined as CFTypeRef in older SDK versions. Change |policy_oid| type to
  // const CFStringRef when Chromium switches to building against the 10.11 SDK
  // (or newer).
  explicit TrustStoreMac(CFTypeRef policy_oid);
  ~TrustStoreMac() override;

  // TrustStore implementation:
  void SyncGetIssuersOf(const ParsedCertificate* cert,
                        ParsedCertificateList* issuers) override;
  void GetTrust(const scoped_refptr<ParsedCertificate>& cert,
                CertificateTrust* trust) const override;

 private:
  FRIEND_TEST_ALL_PREFIXES(TrustStoreMacTest, MultiRootNotTrusted);
  FRIEND_TEST_ALL_PREFIXES(TrustStoreMacTest, SystemCerts);

  // Finds certificates in the OS keychains whose Subject matches |name_data|.
  // The result is an array of SecCertificateRef.
  static base::ScopedCFTypeRef<CFArrayRef>
  FindMatchingCertificatesForMacNormalizedSubject(CFDataRef name_data);

  // Returns the OS-normalized issuer of |cert|.
  // macOS internally uses a normalized form of subject/issuer names for
  // comparing, roughly similar to RFC3280's normalization scheme. The
  // normalized form is used for any database lookups and comparisons.
  static base::ScopedCFTypeRef<CFDataRef> GetMacNormalizedIssuer(
      const ParsedCertificate* cert);

  const CFStringRef policy_oid_;

  DISALLOW_COPY_AND_ASSIGN(TrustStoreMac);
};

}  // namespace net

#endif  // NET_CERT_INTERNAL_TRUST_STORE_MAC_H_
