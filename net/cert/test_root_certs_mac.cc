// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/test_root_certs.h"

#include <Security/Security.h>

#include "base/logging.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"

#if defined(OS_IOS)
#include "net/cert/x509_util_ios.h"
#else
#include "net/cert/x509_util_mac.h"
#endif

namespace net {

bool TestRootCerts::Add(X509Certificate* certificate) {
  base::ScopedCFTypeRef<SecCertificateRef> os_cert(
      x509_util::CreateSecCertificateFromX509Certificate(certificate));
  if (!os_cert)
    return false;

  if (CFArrayContainsValue(temporary_roots_,
                           CFRangeMake(0, CFArrayGetCount(temporary_roots_)),
                           os_cert.get()))
    return true;
  CFArrayAppendValue(temporary_roots_, os_cert.get());

  // Add the certificate to the parallel |test_trust_store_|.
  CertErrors errors;
  std::string cert_bytes;
  if (!X509Certificate::GetDEREncoded(certificate->os_cert_handle(),
                                      &cert_bytes))
    return false;
  scoped_refptr<ParsedCertificate> parsed = ParsedCertificate::Create(
      x509_util::CreateCryptoBuffer(cert_bytes),
      x509_util::DefaultParseCertificateOptions(), &errors);
  if (!parsed)
    return false;
  test_trust_store_.AddTrustAnchor(parsed);

  return true;
}

void TestRootCerts::Clear() {
  CFArrayRemoveAllValues(temporary_roots_);
  test_trust_store_.Clear();
}

bool TestRootCerts::IsEmpty() const {
  return CFArrayGetCount(temporary_roots_) == 0;
}

OSStatus TestRootCerts::FixupSecTrustRef(SecTrustRef trust_ref) const {
  if (IsEmpty())
    return noErr;

  OSStatus status = SecTrustSetAnchorCertificates(trust_ref, temporary_roots_);
  if (status)
    return status;
  // Trust system store in addition to trusting |temporary_roots_|.
  return SecTrustSetAnchorCertificatesOnly(trust_ref, false);
}

TestRootCerts::~TestRootCerts() {}

void TestRootCerts::Init() {
  temporary_roots_.reset(
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
}

}  // namespace net
