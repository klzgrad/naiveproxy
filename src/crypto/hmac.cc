// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/hmac.h"

#include <stddef.h>

#include <algorithm>
#include <string>

#include "base/check.h"
#include "base/notreached.h"
#include "base/stl_util.h"
#include "crypto/openssl_util.h"
#include "crypto/secure_util.h"
#include "crypto/symmetric_key.h"
#include "third_party/boringssl/src/include/openssl/hmac.h"

namespace crypto {

HMAC::HMAC(HashAlgorithm hash_alg) : hash_alg_(hash_alg), initialized_(false) {
  // Only SHA-1 and SHA-256 hash algorithms are supported now.
  DCHECK(hash_alg_ == SHA1 || hash_alg_ == SHA256);
}

HMAC::~HMAC() {
  // Zero out key copy.
  key_.assign(key_.size(), 0);
  base::STLClearObject(&key_);
}

size_t HMAC::DigestLength() const {
  switch (hash_alg_) {
    case SHA1:
      return 20;
    case SHA256:
      return 32;
    default:
      NOTREACHED();
      return 0;
  }
}

bool HMAC::Init(const unsigned char* key, size_t key_length) {
  // Init must not be called more than once on the same HMAC object.
  DCHECK(!initialized_);
  initialized_ = true;
  key_.assign(key, key + key_length);
  return true;
}

bool HMAC::Init(const SymmetricKey* key) {
  return Init(key->key());
}

bool HMAC::Sign(base::StringPiece data,
                unsigned char* digest,
                size_t digest_length) const {
  DCHECK(initialized_);

  ScopedOpenSSLSafeSizeBuffer<EVP_MAX_MD_SIZE> result(digest, digest_length);
  return !!::HMAC(hash_alg_ == SHA1 ? EVP_sha1() : EVP_sha256(), key_.data(),
                  key_.size(),
                  reinterpret_cast<const unsigned char*>(data.data()),
                  data.size(), result.safe_buffer(), nullptr);
}

bool HMAC::Verify(base::StringPiece data, base::StringPiece digest) const {
  if (digest.size() != DigestLength())
    return false;
  return VerifyTruncated(data, digest);
}

bool HMAC::VerifyTruncated(base::StringPiece data,
                           base::StringPiece digest) const {
  if (digest.empty())
    return false;
  size_t digest_length = DigestLength();
  std::unique_ptr<unsigned char[]> computed_digest(
      new unsigned char[digest_length]);
  if (!Sign(data, computed_digest.get(), digest_length))
    return false;

  return SecureMemEqual(digest.data(), computed_digest.get(),
                        std::min(digest.size(), digest_length));
}

}  // namespace crypto
