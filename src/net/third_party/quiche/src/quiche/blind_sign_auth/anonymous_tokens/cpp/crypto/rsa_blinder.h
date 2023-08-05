// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLINDER_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLINDER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/blinder.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_export.h"
// copybara:strip_begin(internal comment)
// The QUICHE_EXPORT annotation is necessary for some classes and functions
// to link correctly on Windows. Please do not remove them!
// copybara:strip_end

namespace private_membership {
namespace anonymous_tokens {

// RsaBlinder `blinds` input messages, and then unblinds them after they are
// signed.
class QUICHE_EXPORT RsaBlinder : public Blinder {
 public:
  // Passing of public_metadata is optional. If it is set to any value including
  // an empty string, RsaBlinder will assume that partially blind RSA signature
  // protocol is being executed.
  static absl::StatusOr<std::unique_ptr<RsaBlinder>> New(
      const RSABlindSignaturePublicKey& public_key,
      std::optional<absl::string_view> public_metadata = std::nullopt);

  // Blind `message` using n and e derived from an RSA public key and the public
  // metadata if applicable.
  //
  // Before blinding, the `message` will first be hashed and then encoded with
  // the EMSA-PSS operation.
  absl::StatusOr<std::string> Blind(absl::string_view message) override;

  // Unblinds `blind_signature`.
  absl::StatusOr<std::string> Unblind(
      absl::string_view blind_signature) override;

  // Verifies an `unblinded` signature against the input message.
  absl::Status Verify(absl::string_view signature, absl::string_view message);

 private:
  // Use `New` to construct
  RsaBlinder(int salt_length, std::optional<absl::string_view> public_metadata,
             const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
             bssl::UniquePtr<RSA> rsa_public_key,
             bssl::UniquePtr<BIGNUM> rsa_modulus,
             bssl::UniquePtr<BIGNUM> augmented_rsa_e, bssl::UniquePtr<BIGNUM> r,
             bssl::UniquePtr<BIGNUM> r_inv_mont,
             bssl::UniquePtr<BN_MONT_CTX> mont_n);

  const int salt_length_;
  std::optional<std::string> public_metadata_;
  const EVP_MD* sig_hash_;   // Owned by BoringSSL.
  const EVP_MD* mgf1_hash_;  // Owned by BoringSSL.

  const bssl::UniquePtr<RSA> rsa_public_key_;
  // Storing RSA modulus separately for helping with BN computations.
  const bssl::UniquePtr<BIGNUM> rsa_modulus_;
  // If public metadata is not supported, augmented_rsa_e_ will be equal to
  // public exponent e in rsa_public_key_.
  const bssl::UniquePtr<BIGNUM> augmented_rsa_e_;

  const bssl::UniquePtr<BIGNUM> r_;
  // r^-1 mod n in the Montgomery domain
  const bssl::UniquePtr<BIGNUM> r_inv_mont_;
  const bssl::UniquePtr<BN_MONT_CTX> mont_n_;

  BlinderState blinder_state_;
};

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLINDER_H_
