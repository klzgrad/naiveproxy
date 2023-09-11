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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLIND_SIGNER_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLIND_SIGNER_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/blind_signer.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_export.h"
// copybara:strip_begin(internal comment)
// The QUICHE_EXPORT annotation is necessary for some classes and functions
// to link correctly on Windows. Please do not remove them!
// copybara:strip_end

namespace private_membership {
namespace anonymous_tokens {

// The RSA SSA (Signature Schemes with Appendix) using PSS (Probabilistic
// Signature Scheme) encoding is defined at
// https://tools.ietf.org/html/rfc8017#section-8.1). This implementation uses
// Boring SSL for the underlying cryptographic operations.
class QUICHE_EXPORT RsaBlindSigner : public BlindSigner {
 public:
  ~RsaBlindSigner() override = default;
  RsaBlindSigner(const RsaBlindSigner&) = delete;
  RsaBlindSigner& operator=(const RsaBlindSigner&) = delete;

  // Passing of public_metadata is optional. If it is set to any value including
  // an empty string, RsaBlindSigner will assume that partially blind RSA
  // signature protocol is being executed.
  //
  // If public metadata is passed and the boolean "use_rsa_public_exponent" is
  // set to false, the public exponent in the signing_key is not used in any
  // computations in the protocol.
  //
  // Setting "use_rsa_public_exponent" to true is deprecated. All new users
  // should set it to false.
  static absl::StatusOr<std::unique_ptr<RsaBlindSigner>> New(
      const RSAPrivateKey& signing_key, bool use_rsa_public_exponent,
      std::optional<absl::string_view> public_metadata = std::nullopt);

  // Computes the signature for 'blinded_data'.
  absl::StatusOr<std::string> Sign(
      absl::string_view blinded_data) const override;

 private:
  // Use New to construct.
  RsaBlindSigner(std::optional<absl::string_view> public_metadata,
                 bssl::UniquePtr<RSA> rsa_private_key);

  const std::optional<std::string> public_metadata_;

  // In case public metadata is passed to RsaBlindSigner::New, rsa_private_key_
  // will be initialized using RSA_new_private_key_large_e method.
  const bssl::UniquePtr<RSA> rsa_private_key_;
};

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_BLIND_SIGNER_H_
