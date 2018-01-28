// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/signature_verifier.h"

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "crypto/openssl_util.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"

namespace crypto {

namespace {

const EVP_MD* ToOpenSSLDigest(SignatureVerifier::HashAlgorithm hash_alg) {
  switch (hash_alg) {
    case SignatureVerifier::SHA1:
      return EVP_sha1();
    case SignatureVerifier::SHA256:
      return EVP_sha256();
  }
  return nullptr;
}

}  // namespace

struct SignatureVerifier::VerifyContext {
  bssl::ScopedEVP_MD_CTX ctx;
};

SignatureVerifier::SignatureVerifier() {}

SignatureVerifier::~SignatureVerifier() {}

bool SignatureVerifier::VerifyInit(SignatureAlgorithm signature_algorithm,
                                   const uint8_t* signature,
                                   size_t signature_len,
                                   const uint8_t* public_key_info,
                                   size_t public_key_info_len) {
  int pkey_type = EVP_PKEY_NONE;
  const EVP_MD* digest = nullptr;
  switch (signature_algorithm) {
    case RSA_PKCS1_SHA1:
      pkey_type = EVP_PKEY_RSA;
      digest = EVP_sha1();
      break;
    case RSA_PKCS1_SHA256:
      pkey_type = EVP_PKEY_RSA;
      digest = EVP_sha256();
      break;
    case ECDSA_SHA256:
      pkey_type = EVP_PKEY_EC;
      digest = EVP_sha256();
      break;
  }
  DCHECK_NE(EVP_PKEY_NONE, pkey_type);
  DCHECK(digest);

  return CommonInit(pkey_type, digest, signature, signature_len,
                    public_key_info, public_key_info_len, nullptr);
}

bool SignatureVerifier::VerifyInitRSAPSS(HashAlgorithm hash_alg,
                                         HashAlgorithm mask_hash_alg,
                                         size_t salt_len,
                                         const uint8_t* signature,
                                         size_t signature_len,
                                         const uint8_t* public_key_info,
                                         size_t public_key_info_len) {
  OpenSSLErrStackTracer err_tracer(FROM_HERE);
  const EVP_MD* const digest = ToOpenSSLDigest(hash_alg);
  DCHECK(digest);
  if (!digest) {
    return false;
  }

  EVP_PKEY_CTX* pkey_ctx;
  if (!CommonInit(EVP_PKEY_RSA, digest, signature, signature_len,
                  public_key_info, public_key_info_len, &pkey_ctx)) {
    return false;
  }

  int rv = EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING);
  if (rv != 1)
    return false;
  const EVP_MD* const mgf_digest = ToOpenSSLDigest(mask_hash_alg);
  DCHECK(mgf_digest);
  if (!mgf_digest) {
    return false;
  }
  return EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, mgf_digest) &&
         EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx,
                                          base::checked_cast<int>(salt_len));
}

void SignatureVerifier::VerifyUpdate(const uint8_t* data_part,
                                     size_t data_part_len) {
  DCHECK(verify_context_);
  OpenSSLErrStackTracer err_tracer(FROM_HERE);
  int rv = EVP_DigestVerifyUpdate(verify_context_->ctx.get(), data_part,
                                  data_part_len);
  DCHECK_EQ(rv, 1);
}

bool SignatureVerifier::VerifyFinal() {
  DCHECK(verify_context_);
  OpenSSLErrStackTracer err_tracer(FROM_HERE);
  int rv = EVP_DigestVerifyFinal(verify_context_->ctx.get(), signature_.data(),
                                 signature_.size());
  DCHECK_EQ(static_cast<int>(!!rv), rv);
  Reset();
  return rv == 1;
}

bool SignatureVerifier::CommonInit(int pkey_type,
                                   const EVP_MD* digest,
                                   const uint8_t* signature,
                                   size_t signature_len,
                                   const uint8_t* public_key_info,
                                   size_t public_key_info_len,
                                   EVP_PKEY_CTX** pkey_ctx) {
  if (verify_context_)
    return false;

  verify_context_.reset(new VerifyContext);

  signature_.assign(signature, signature + signature_len);

  CBS cbs;
  CBS_init(&cbs, public_key_info, public_key_info_len);
  bssl::UniquePtr<EVP_PKEY> public_key(EVP_parse_public_key(&cbs));
  if (!public_key || CBS_len(&cbs) != 0 ||
      EVP_PKEY_id(public_key.get()) != pkey_type) {
    return false;
  }

  int rv = EVP_DigestVerifyInit(verify_context_->ctx.get(), pkey_ctx,
                                digest, nullptr, public_key.get());
  return rv == 1;
}

void SignatureVerifier::Reset() {
  verify_context_.reset();
  signature_.clear();
}

}  // namespace crypto
