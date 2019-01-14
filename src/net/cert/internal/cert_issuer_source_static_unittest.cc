// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/cert_issuer_source_static.h"

#include "net/cert/internal/cert_issuer_source_sync_unittest.h"
#include "net/cert/internal/parsed_certificate.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class CertIssuerSourceStaticTestDelegate {
 public:
  void AddCert(scoped_refptr<ParsedCertificate> cert) {
    source_.AddCert(std::move(cert));
  }

  CertIssuerSource& source() { return source_; }

 protected:
  CertIssuerSourceStatic source_;
};

INSTANTIATE_TYPED_TEST_CASE_P(CertIssuerSourceStaticTest,
                              CertIssuerSourceSyncTest,
                              CertIssuerSourceStaticTestDelegate);

INSTANTIATE_TYPED_TEST_CASE_P(CertIssuerSourceStaticNormalizationTest,
                              CertIssuerSourceSyncNormalizationTest,
                              CertIssuerSourceStaticTestDelegate);

}  // namespace

}  // namespace net
