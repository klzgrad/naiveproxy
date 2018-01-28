// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_SIMPLE_PATH_BUILDER_DELEGATE_H_
#define NET_CERT_INTERNAL_SIMPLE_PATH_BUILDER_DELEGATE_H_

#include <stddef.h>

#include "base/compiler_specific.h"
#include "net/base/net_export.h"
#include "net/cert/internal/path_builder.h"

namespace net {

class CertErrors;
class SignatureAlgorithm;

// SimplePathBuilderDelegate is an implementation of CertPathBuilderDelegate
// that uses some default policies:
//
//   * RSA public keys must be >= |min_rsa_modulus_length_bits|.
//   * Signature algorithm can be RSA PKCS#1, RSASSA-PSS or ECDSA
//   * Hash algorithm can be SHA1, SHA256, SHA348 or SHA512
//   * EC named curve can be P-256, P-384, P-521.
class NET_EXPORT SimplePathBuilderDelegate : public CertPathBuilderDelegate {
 public:
  // Error emitted when a public key is rejected because it is an RSA key with a
  // modulus size that is too small.
  static const CertErrorId kRsaModulusTooSmall;

  explicit SimplePathBuilderDelegate(size_t min_rsa_modulus_length_bits);

  // Accepts RSA PKCS#1, RSASSA-PSS or ECDA using any of the SHA* digests
  // (including SHA1).
  bool IsSignatureAlgorithmAcceptable(
      const SignatureAlgorithm& signature_algorithm,
      CertErrors* errors) override;

  // Requires RSA keys be >= |min_rsa_modulus_length_bits_|.
  bool IsPublicKeyAcceptable(EVP_PKEY* public_key, CertErrors* errors) override;

  // No-op implementation.
  void CheckPathAfterVerification(CertPathBuilderResultPath* path) override;

 private:
  const size_t min_rsa_modulus_length_bits_;
};

}  // namespace net

#endif  // NET_CERT_INTERNAL_SIMPLE_PATH_BUILDER_DELEGATE_H_
