// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verify_proc_ios.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include "base/mac/scoped_cftyperef.h"
#include "base/memory/ref_counted.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_ios_and_mac.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace {

// Creates new SecTrustRef object backed up by cert from |cert_file|.
base::ScopedCFTypeRef<SecTrustRef> CreateSecTrust(
    const std::string& cert_file) {
  base::ScopedCFTypeRef<SecTrustRef> scoped_result;

  scoped_refptr<net::X509Certificate> cert =
      net::ImportCertFromFile(net::GetTestCertsDirectory(), cert_file);
  if (!cert) {
    ADD_FAILURE();
    return scoped_result;
  }
  base::ScopedCFTypeRef<CFMutableArrayRef> certs(
      net::x509_util::CreateSecCertificateArrayForX509Certificate(cert.get()));
  if (!certs) {
    ADD_FAILURE();
    return scoped_result;
  }

  base::ScopedCFTypeRef<SecPolicyRef> policy(
      SecPolicyCreateSSL(TRUE, CFSTR("chromium.org")));
  SecTrustRef result = nullptr;
  if (SecTrustCreateWithCertificates(certs.get(), policy, &result) ==
      errSecSuccess) {
    scoped_result.reset(result);
  }
  return scoped_result;
}

}  // namespace

namespace net {

using CertVerifyProcIOSTest = PlatformTest;

// Tests |GetCertFailureStatusFromTrust| with null trust object.
TEST_F(CertVerifyProcIOSTest, StatusForNullTrust) {
  EXPECT_EQ(CERT_STATUS_INVALID,
            CertVerifyProcIOS::GetCertFailureStatusFromTrust(nullptr));
}

// Tests |GetCertFailureStatusFromTrust| with trust object that has not been
// evaluated backed by ok_cert.pem cert.
TEST_F(CertVerifyProcIOSTest, StatusForNotEvaluatedTrust) {
  CertStatus status = CertVerifyProcIOS::GetCertFailureStatusFromTrust(
      CreateSecTrust("ok_cert.pem"));
  EXPECT_TRUE(status & CERT_STATUS_COMMON_NAME_INVALID);
  EXPECT_TRUE(status & CERT_STATUS_AUTHORITY_INVALID);
  EXPECT_FALSE(status & CERT_STATUS_DATE_INVALID);
}

// Tests |GetCertFailureStatusFromTrust| with evaluated trust object backed by
// expired_cert.pem cert.
TEST_F(CertVerifyProcIOSTest, StatusForEvaluatedTrust) {
  base::ScopedCFTypeRef<SecTrustRef> trust(CreateSecTrust("expired_cert.pem"));
  ASSERT_TRUE(trust);
  SecTrustResultType result = kSecTrustResultInvalid;
  SecTrustEvaluate(trust, &result);

  CertStatus status = CertVerifyProcIOS::GetCertFailureStatusFromTrust(trust);
  EXPECT_TRUE(status & CERT_STATUS_COMMON_NAME_INVALID);
  EXPECT_TRUE(status & CERT_STATUS_AUTHORITY_INVALID);
  EXPECT_TRUE(status & CERT_STATUS_DATE_INVALID);
}

}  // namespace net
