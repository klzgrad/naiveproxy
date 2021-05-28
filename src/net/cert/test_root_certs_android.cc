// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/test_root_certs.h"

#include "base/location.h"
#include "net/android/network_library.h"
#include "net/cert/x509_certificate.h"
#include "third_party/boringssl/src/include/openssl/pool.h"

namespace net {

bool TestRootCerts::Add(X509Certificate* certificate) {
  android::AddTestRootCertificate(
      CRYPTO_BUFFER_data(certificate->cert_buffer()),
      CRYPTO_BUFFER_len(certificate->cert_buffer()));
  empty_ = false;
  return true;
}

void TestRootCerts::Clear() {
  if (empty_)
    return;

  android::ClearTestRootCertificates();
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
