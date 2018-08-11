// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/test_root_certs.h"

#include "base/location.h"
#include "base/logging.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "third_party/boringssl/src/include/openssl/pool.h"

namespace net {

bool TestRootCerts::Add(X509Certificate* certificate) {
  CertErrors errors;
  auto parsed = ParsedCertificate::Create(
      x509_util::DupCryptoBuffer(certificate->cert_buffer()),
      ParseCertificateOptions(), &errors);
  if (!parsed) {
    LOG(ERROR) << "Failed to parse DER certificate: " << errors.ToDebugString();
    return false;
  }
  test_trust_store_.AddTrustAnchor(parsed);
  empty_ = false;
  return true;
}

void TestRootCerts::Clear() {
  test_trust_store_.Clear();
  empty_ = true;
}

bool TestRootCerts::IsEmpty() const {
  return empty_;
}

TestRootCerts::~TestRootCerts() {}

void TestRootCerts::Init() {
  empty_ = true;
}

}  // namespace net
