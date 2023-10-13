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
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/anonymous_tokens_pb_openssl_converters.h"
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
    const RSAPublicKey& public_key, const bool use_rsa_public_exponent,
    std::optional<absl::string_view> public_metadata) {
  bssl::UniquePtr<RSA> rsa_public_key;

  if (!public_metadata.has_value()) {
    ANON_TOKENS_ASSIGN_OR_RETURN(rsa_public_key,
                                 AnonymousTokensRSAPublicKeyToRSA(public_key));
  } else {
    // If public metadata is passed, RsaSsaPssVerifier will compute a new public
    // exponent using the public metadata.
    //
    // Empty string is a valid public metadata value.
    ANON_TOKENS_ASSIGN_OR_RETURN(
        rsa_public_key, CreatePublicKeyRSAWithPublicMetadata(
                            public_key.n(), public_key.e(), *public_metadata,
                            use_rsa_public_exponent));
  }

  return absl::WrapUnique(new RsaSsaPssVerifier(salt_length, public_metadata,
                                                sig_hash, mgf1_hash,
                                                std::move(rsa_public_key)));
}

RsaSsaPssVerifier::RsaSsaPssVerifier(
    int salt_length, std::optional<absl::string_view> public_metadata,
    const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    bssl::UniquePtr<RSA> rsa_public_key)
    : salt_length_(salt_length),
      public_metadata_(public_metadata),
      sig_hash_(sig_hash),
      mgf1_hash_(mgf1_hash),
      rsa_public_key_(std::move(rsa_public_key)) {}

absl::Status RsaSsaPssVerifier::Verify(absl::string_view unblind_token,
                                       absl::string_view message) {
  std::string augmented_message(message);
  if (public_metadata_.has_value()) {
    augmented_message = EncodeMessagePublicMetadata(message, *public_metadata_);
  }
  return RsaBlindSignatureVerify(salt_length_, sig_hash_, mgf1_hash_,
                                 unblind_token, augmented_message,
                                 rsa_public_key_.get());
}

}  // namespace anonymous_tokens
}  // namespace private_membership
