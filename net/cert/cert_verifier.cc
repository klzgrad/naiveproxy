// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verifier.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "net/cert/cert_verify_proc.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/sha.h"

#if defined(OS_NACL)
#include "base/logging.h"
#else
#include "net/cert/caching_cert_verifier.h"
#include "net/cert/multi_threaded_cert_verifier.h"
#endif

namespace net {

CertVerifier::RequestParams::RequestParams(
    scoped_refptr<X509Certificate> certificate,
    const std::string& hostname,
    int flags,
    const std::string& ocsp_response,
    CertificateList additional_trust_anchors)
    : certificate_(std::move(certificate)),
      hostname_(hostname),
      flags_(flags),
      ocsp_response_(ocsp_response),
      additional_trust_anchors_(std::move(additional_trust_anchors)) {
  // For efficiency sake, rather than compare all of the fields for each
  // comparison, compute a hash of their values. This is done directly in
  // this class, rather than as an overloaded hash operator, for efficiency's
  // sake.
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, CRYPTO_BUFFER_data(certificate_->cert_buffer()),
                CRYPTO_BUFFER_len(certificate_->cert_buffer()));
  for (const auto& cert_handle : certificate_->intermediate_buffers()) {
    SHA256_Update(&ctx, CRYPTO_BUFFER_data(cert_handle.get()),
                  CRYPTO_BUFFER_len(cert_handle.get()));
  }
  SHA256_Update(&ctx, hostname_.data(), hostname.size());
  SHA256_Update(&ctx, &flags, sizeof(flags));
  SHA256_Update(&ctx, ocsp_response.data(), ocsp_response.size());
  for (const auto& trust_anchor : additional_trust_anchors_) {
    SHA256_Update(&ctx, CRYPTO_BUFFER_data(trust_anchor->cert_buffer()),
                  CRYPTO_BUFFER_len(trust_anchor->cert_buffer()));
  }
  SHA256_Final(reinterpret_cast<uint8_t*>(
                   base::WriteInto(&key_, SHA256_DIGEST_LENGTH + 1)),
               &ctx);
}

CertVerifier::RequestParams::RequestParams(const RequestParams& other) =
    default;
CertVerifier::RequestParams::~RequestParams() = default;

bool CertVerifier::RequestParams::operator==(
    const CertVerifier::RequestParams& other) const {
  return key_ == other.key_;
}

bool CertVerifier::RequestParams::operator<(
    const CertVerifier::RequestParams& other) const {
  return key_ < other.key_;
}

bool CertVerifier::SupportsOCSPStapling() {
  return false;
}

std::unique_ptr<CertVerifier> CertVerifier::CreateDefault() {
#if defined(OS_NACL)
  NOTIMPLEMENTED();
  return std::unique_ptr<CertVerifier>();
#else
  return std::make_unique<CachingCertVerifier>(
      std::make_unique<MultiThreadedCertVerifier>(
          CertVerifyProc::CreateDefault()));
#endif
}

}  // namespace net
