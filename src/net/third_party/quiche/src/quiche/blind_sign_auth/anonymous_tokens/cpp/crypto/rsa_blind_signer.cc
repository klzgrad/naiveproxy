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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/rsa_blind_signer.h"

#include <cstddef>
#include <cstdint>
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
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {
namespace {

absl::StatusOr<bssl::UniquePtr<RSA>> CreatePrivateKeyWithPublicMetadata(
    const absl::string_view rsa_modulus_str,
    const absl::string_view rsa_public_exponent_str,
    const absl::string_view rsa_p_str, const absl::string_view rsa_q_str,
    const absl::string_view rsa_crt_str,
    const absl::string_view public_metadata,
    const bool use_rsa_public_exponent) {
  // Convert RSA modulus n (=p*q) to BIGNUM.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(rsa_modulus_str));
  // Convert public exponent e to BIGNUM.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> old_e,
                               StringToBignum(rsa_public_exponent_str));

  // Compute new public exponent based on public metadata.
  bssl::UniquePtr<BIGNUM> derived_rsa_e;
  if (use_rsa_public_exponent) {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        derived_rsa_e, ComputeExponentWithPublicMetadataAndPublicExponent(
                           *rsa_modulus, *old_e, public_metadata));
  } else {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        derived_rsa_e,
        ComputeExponentWithPublicMetadata(*rsa_modulus, public_metadata));
  }

  // Convert p & q to BIGNUM.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_p,
                               StringToBignum(rsa_p_str));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_q,
                               StringToBignum(rsa_q_str));

  // Compute phi(p) = p-1 and phi(q) = q-1.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_p, NewBigNum());
  if (BN_sub(phi_p.get(), rsa_p.get(), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(p): ", GetSslErrors()));
  }
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_q, NewBigNum());
  if (BN_sub(phi_q.get(), rsa_q.get(), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(q): ", GetSslErrors()));
  }

  bssl::UniquePtr<BN_CTX> bn_ctx(BN_CTX_new());
  if (!bn_ctx) {
    return absl::InternalError("BN_CTX_new failed.");
  }
  // Compute lcm(phi(p), phi(q)).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> lcm,
                               ComputeCarmichaelLcm(*phi_p, *phi_q, *bn_ctx));

  // Compute the new private exponent derived_rsa_d.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> derived_rsa_d,
                               NewBigNum());
  if (!BN_mod_inverse(derived_rsa_d.get(), derived_rsa_e.get(), lcm.get(),
                      bn_ctx.get())) {
    return absl::InternalError(
        absl::StrCat("Could not compute private exponent d: ", GetSslErrors()));
  }

  // Compute new_dpm1 = derived_rsa_d mod p-1.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_dpm1, NewBigNum());
  BN_mod(new_dpm1.get(), derived_rsa_d.get(), phi_p.get(), bn_ctx.get());
  // Compute new_dqm1 = derived_rsa_d mod q-1.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_dqm1, NewBigNum());
  BN_mod(new_dqm1.get(), derived_rsa_d.get(), phi_q.get(), bn_ctx.get());
  // Convert crt to BIGNUM.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_crt,
                               StringToBignum(rsa_crt_str));

  // Create private key derived from given key and public metadata.
  bssl::UniquePtr<RSA> derived_private_key(RSA_new_private_key_large_e(
      rsa_modulus.get(), derived_rsa_e.get(), derived_rsa_d.get(), rsa_p.get(),
      rsa_q.get(), new_dpm1.get(), new_dqm1.get(), rsa_crt.get()));
  if (!derived_private_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new_private_key_large_e failed: ", GetSslErrors()));
  }

  return derived_private_key;
}

}  // namespace

RsaBlindSigner::RsaBlindSigner(std::optional<absl::string_view> public_metadata,
                               bssl::UniquePtr<RSA> rsa_private_key)
    : public_metadata_(public_metadata),
      rsa_private_key_(std::move(rsa_private_key)) {}

absl::StatusOr<std::unique_ptr<RsaBlindSigner>> RsaBlindSigner::New(
    const RSAPrivateKey& signing_key, const bool use_rsa_public_exponent,
    std::optional<absl::string_view> public_metadata) {
  bssl::UniquePtr<RSA> rsa_private_key;
  if (!public_metadata.has_value()) {
    // The RSA modulus and exponent are checked as part of the conversion to
    // bssl::UniquePtr<RSA>.
    ANON_TOKENS_ASSIGN_OR_RETURN(
        rsa_private_key, AnonymousTokensRSAPrivateKeyToRSA(signing_key));
  } else {
    // If public metadata is passed, RsaBlindSigner will compute a new private
    // exponent using the public metadata.
    //
    // Empty string is a valid public metadata value.
    ANON_TOKENS_ASSIGN_OR_RETURN(
        rsa_private_key,
        CreatePrivateKeyWithPublicMetadata(
            signing_key.n(), signing_key.e(), signing_key.p(), signing_key.q(),
            signing_key.crt(), *public_metadata, use_rsa_public_exponent));
  }
  return absl::WrapUnique(
      new RsaBlindSigner(public_metadata, std::move(rsa_private_key)));
}

absl::StatusOr<std::string> RsaBlindSigner::Sign(
    const absl::string_view blinded_data) const {
  if (blinded_data.empty() || blinded_data.data() == nullptr) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  }

  int mod_size = RSA_size(rsa_private_key_.get());
  if (blinded_data.size() != mod_size) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", mod_size,
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }

  std::string signature(mod_size, 0);
  // Compute a raw RSA signature.
  size_t out_len;
  if (RSA_sign_raw(
          /*rsa=*/rsa_private_key_.get(), /*out_len=*/&out_len,
          /*out=*/reinterpret_cast<uint8_t*>(&signature[0]),
          /*max_out=*/mod_size,
          /*in=*/reinterpret_cast<const uint8_t*>(&blinded_data[0]),
          /*in_len=*/mod_size,
          /*padding=*/RSA_NO_PADDING) != kBsslSuccess) {
    return absl::InternalError(
        "RSA_sign_raw failed when called from RsaBlindSigner::Sign");
  }
  if (out_len != mod_size || out_len != signature.size()) {
    return absl::InternalError(absl::StrCat(
        "Expected value of out_len and signature.size() = ", mod_size,
        " bytes, actual value of out_len and signature.size() = ", out_len,
        " and ", signature.size(), " bytes."));
  }
  return signature;
}

}  // namespace anonymous_tokens
}  // namespace private_membership
