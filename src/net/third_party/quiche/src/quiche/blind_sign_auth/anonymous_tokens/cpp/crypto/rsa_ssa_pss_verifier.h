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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_SSA_PSS_VERIFIER_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_SSA_PSS_VERIFIER_H_

#include <stdint.h>

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/verifier.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_export.h"
// copybara:strip_begin(internal comment)
// The QUICHE_EXPORT annotation is necessary for some classes and functions
// to link correctly on Windows. Please do not remove them!
// copybara:strip_end

namespace private_membership {
namespace anonymous_tokens {

// RsaSsaPssVerifier is able to verify an unblinded token (signature) against an
// inputted message using a public key and other input parameters.
class QUICHE_EXPORT RsaSsaPssVerifier : public Verifier {
 public:
  // Passing of public_metadata is optional. If it is set to any value including
  // an empty string, RsaSsaPssVerifier will assume that partially blind RSA
  // signature protocol is being executed.
  //
  // If public metadata is passed and the boolean "use_rsa_public_exponent" is
  // set to false, the public exponent in the public_key is not used in any
  // computations in the protocol.
  //
  // Setting "use_rsa_public_exponent" to true is deprecated. All new users
  // should set it to false.
  static absl::StatusOr<std::unique_ptr<RsaSsaPssVerifier>> New(
      int salt_length, const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
      const RSAPublicKey& public_key, bool use_rsa_public_exponent,
      std::optional<absl::string_view> public_metadata = std::nullopt);

  // Verifies the signature.
  //
  // Returns OkStatus() on successful verification. Otherwise returns an error.
  absl::Status Verify(absl::string_view unblind_token,
                      absl::string_view message) override;

 private:
  // Use `New` to construct
  RsaSsaPssVerifier(int salt_length,
                    std::optional<absl::string_view> public_metadata,
                    const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
                    bssl::UniquePtr<RSA> rsa_public_key);

  const int salt_length_;
  std::optional<std::string> public_metadata_;
  const EVP_MD* const sig_hash_;   // Owned by BoringSSL.
  const EVP_MD* const mgf1_hash_;  // Owned by BoringSSL.

  // If public metadata is passed to RsaSsaPssVerifier::New, rsa_public_key_
  // will be initialized using RSA_new_public_key_large_e method.
  const bssl::UniquePtr<RSA> rsa_public_key_;
};

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_RSA_SSA_PSS_VERIFIER_H_
