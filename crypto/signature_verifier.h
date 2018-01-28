// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_SIGNATURE_VERIFIER_H_
#define CRYPTO_SIGNATURE_VERIFIER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "build/build_config.h"
#include "crypto/crypto_export.h"

typedef struct env_md_st EVP_MD;
typedef struct evp_pkey_ctx_st EVP_PKEY_CTX;

namespace crypto {

// The SignatureVerifier class verifies a signature using a bare public key
// (as opposed to a certificate).
class CRYPTO_EXPORT SignatureVerifier {
 public:
  // The set of supported hash functions. Extend as required.
  enum HashAlgorithm {
    SHA1,
    SHA256,
  };

  // The set of supported signature algorithms. Extend as required.
  enum SignatureAlgorithm {
    RSA_PKCS1_SHA1,
    RSA_PKCS1_SHA256,
    ECDSA_SHA256,
  };

  SignatureVerifier();
  ~SignatureVerifier();

  // Streaming interface:

  // Initiates a signature verification operation.  This should be followed
  // by one or more VerifyUpdate calls and a VerifyFinal call.
  // NOTE: for RSA-PSS signatures, use VerifyInitRSAPSS instead.
  //
  // The signature is encoded according to the signature algorithm.
  //
  // The public key is specified as a DER encoded ASN.1 SubjectPublicKeyInfo
  // structure, which contains not only the public key but also its type
  // (algorithm):
  //   SubjectPublicKeyInfo  ::=  SEQUENCE  {
  //       algorithm            AlgorithmIdentifier,
  //       subjectPublicKey     BIT STRING  }
  bool VerifyInit(SignatureAlgorithm signature_algorithm,
                  const uint8_t* signature,
                  size_t signature_len,
                  const uint8_t* public_key_info,
                  size_t public_key_info_len);

  // Initiates a RSA-PSS signature verification operation.  This should be
  // followed by one or more VerifyUpdate calls and a VerifyFinal call.
  //
  // The RSA-PSS signature algorithm parameters are specified with the
  // |hash_alg|, |mask_hash_alg|, and |salt_len| arguments.
  //
  // An RSA-PSS signature is a nonnegative integer encoded as a byte string
  // (of the same length as the RSA modulus) in big-endian byte order. It
  // must not be further encoded in an ASN.1 BIT STRING.
  //
  // The public key is specified as a DER encoded ASN.1 SubjectPublicKeyInfo
  // structure, which contains not only the public key but also its type
  // (algorithm):
  //   SubjectPublicKeyInfo  ::=  SEQUENCE  {
  //       algorithm            AlgorithmIdentifier,
  //       subjectPublicKey     BIT STRING  }
  bool VerifyInitRSAPSS(HashAlgorithm hash_alg,
                        HashAlgorithm mask_hash_alg,
                        size_t salt_len,
                        const uint8_t* signature,
                        size_t signature_len,
                        const uint8_t* public_key_info,
                        size_t public_key_info_len);

  // Feeds a piece of the data to the signature verifier.
  void VerifyUpdate(const uint8_t* data_part, size_t data_part_len);

  // Concludes a signature verification operation.  Returns true if the
  // signature is valid.  Returns false if the signature is invalid or an
  // error occurred.
  bool VerifyFinal();

 private:
  bool CommonInit(int pkey_type,
                  const EVP_MD* digest,
                  const uint8_t* signature,
                  size_t signature_len,
                  const uint8_t* public_key_info,
                  size_t public_key_info_len,
                  EVP_PKEY_CTX** pkey_ctx);

  void Reset();

  std::vector<uint8_t> signature_;

  struct VerifyContext;
  std::unique_ptr<VerifyContext> verify_context_;
};

}  // namespace crypto

#endif  // CRYPTO_SIGNATURE_VERIFIER_H_
