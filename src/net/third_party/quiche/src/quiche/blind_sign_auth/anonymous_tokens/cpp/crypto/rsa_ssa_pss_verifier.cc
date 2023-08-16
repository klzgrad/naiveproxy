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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/rsa_ssa_pss_verifier.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/bn.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

absl::StatusOr<std::unique_ptr<RsaSsaPssVerifier>> RsaSsaPssVerifier::New(
    const int salt_length, const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    const RSAPublicKey& public_key,
    std::optional<absl::string_view> public_metadata) {
  // Convert to OpenSSL RSA which will be used in the code paths for the
  // standard RSA blind signature scheme.
  //
  // Moreover, it will also be passed as an argument to PSS related padding
  // verification methods irrespective of whether RsaBlinder is being used as a
  // part of the standard RSA blind signature scheme or the scheme with public
  // metadata support.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<RSA> rsa_public_key,
                               AnonymousTokensRSAPublicKeyToRSA(public_key));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(public_key.n()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_e,
                               StringToBignum(public_key.e()));

  bssl::UniquePtr<BIGNUM> augmented_rsa_e = nullptr;
  // If public metadata is supported, RsaSsaPssVerifier will compute a new
  // public exponent using the public metadata.
  //
  // Empty string is a valid public metadata value.
  if (public_metadata.has_value()) {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        augmented_rsa_e,
        ComputeFinalExponentUnderPublicMetadata(
            *rsa_modulus.get(), *rsa_e.get(), *public_metadata));
  } else {
    augmented_rsa_e = std::move(rsa_e);
  }
  return absl::WrapUnique(
      new RsaSsaPssVerifier(salt_length, public_metadata, sig_hash, mgf1_hash,
                            std::move(rsa_public_key), std::move(rsa_modulus),
                            std::move(augmented_rsa_e)));
}

RsaSsaPssVerifier::RsaSsaPssVerifier(
    int salt_length, std::optional<absl::string_view> public_metadata,
    const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    bssl::UniquePtr<RSA> rsa_public_key, bssl::UniquePtr<BIGNUM> rsa_modulus,
    bssl::UniquePtr<BIGNUM> augmented_rsa_e)
    : salt_length_(salt_length),
      public_metadata_(public_metadata),
      sig_hash_(sig_hash),
      mgf1_hash_(mgf1_hash),
      rsa_public_key_(std::move(rsa_public_key)),
      rsa_modulus_(std::move(rsa_modulus)),
      augmented_rsa_e_(std::move(augmented_rsa_e)) {}

absl::Status RsaSsaPssVerifier::Verify(absl::string_view unblind_token,
                                       absl::string_view message) {
  return RsaBlindSignatureVerify(salt_length_, sig_hash_, mgf1_hash_,
                                 rsa_public_key_.get(), *rsa_modulus_.get(),
                                 *augmented_rsa_e_.get(), unblind_token,
                                 message, public_metadata_);
}

}  // namespace anonymous_tokens
}  // namespace private_membership
