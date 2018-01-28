// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/test_root_certs.h"

#include <Security/Security.h>

#include "base/logging.h"
#include "net/cert/x509_certificate.h"

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
  return true;
}

void TestRootCerts::Clear() {
  CFArrayRemoveAllValues(temporary_roots_);
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
  return SecTrustSetAnchorCertificatesOnly(trust_ref, !allow_system_trust_);
}

void TestRootCerts::SetAllowSystemTrust(bool allow_system_trust) {
  allow_system_trust_ = allow_system_trust;
}

TestRootCerts::~TestRootCerts() {}

void TestRootCerts::Init() {
  temporary_roots_.reset(
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
  allow_system_trust_ = true;
}

}  // namespace net
