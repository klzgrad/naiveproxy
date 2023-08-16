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
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

RsaBlindSigner::RsaBlindSigner(std::optional<absl::string_view> public_metadata,
                               bssl::UniquePtr<BIGNUM> rsa_modulus,
                               bssl::UniquePtr<BIGNUM> rsa_p,
                               bssl::UniquePtr<BIGNUM> rsa_q,
                               bssl::UniquePtr<BIGNUM> augmented_rsa_e,
                               bssl::UniquePtr<BIGNUM> augmented_rsa_d,
                               bssl::UniquePtr<RSA> rsa_standard_key)
    : public_metadata_(public_metadata),
      rsa_modulus_(std::move(rsa_modulus)),
      rsa_p_(std::move(rsa_p)),
      rsa_q_(std::move(rsa_q)),
      augmented_rsa_e_(std::move(augmented_rsa_e)),
      augmented_rsa_d_(std::move(augmented_rsa_d)),
      rsa_standard_key_(std::move(rsa_standard_key)) {}

absl::StatusOr<std::unique_ptr<RsaBlindSigner>> RsaBlindSigner::New(
    const RSAPrivateKey& signing_key,
    std::optional<absl::string_view> public_metadata) {
  if (!public_metadata.has_value()) {
    // The RSA modulus and exponent are checked as part of the conversion to
    // bssl::UniquePtr<RSA>.
    ANON_TOKENS_ASSIGN_OR_RETURN(
        bssl::UniquePtr<RSA> rsa_standard_key,
        AnonymousTokensRSAPrivateKeyToRSA(signing_key));
    return absl::WrapUnique(
        new RsaBlindSigner(public_metadata, nullptr, nullptr, nullptr, nullptr,
                           nullptr, std::move(rsa_standard_key)));
  }

  // Convert RSA modulus n (=p*q) to BIGNUM
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(signing_key.n()));
  // Convert p & q to BIGNUM
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_p,
                               StringToBignum(signing_key.p()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_q,
                               StringToBignum(signing_key.q()));
  // Convert public exponent e to BIGNUM
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> old_e,
                               StringToBignum(signing_key.e()));
  // Convert public exponent e to BIGNUM
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> old_d,
                               StringToBignum(signing_key.d()));

  // Compute new exponents based on public metadata.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> augmented_rsa_e,
                               ComputeFinalExponentUnderPublicMetadata(
                                   *rsa_modulus, *old_e, *public_metadata));

  // Compute phi(p) = p-1 and phi(q) = q-1
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

  // Compute the new private exponent new_d
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> augmented_rsa_d,
                               NewBigNum());
  if (!BN_mod_inverse(augmented_rsa_d.get(), augmented_rsa_e.get(), lcm.get(),
                      bn_ctx.get())) {
    return absl::InternalError(
        absl::StrCat("Could not compute private exponent d: ", GetSslErrors()));
  }

  return absl::WrapUnique(new RsaBlindSigner(
      *public_metadata, std::move(rsa_modulus), std::move(rsa_p),
      std::move(rsa_q), std::move(augmented_rsa_e),
      std::move(augmented_rsa_d)));
}

// Helper Signature method that assumes RSA_NO_PADDING.
// TODO(b/271438729): Adding blinding of private operations in RSA Sign
// TODO(b/271438266): Implement RsaSign using the Chinese Remainder Theorem
absl::StatusOr<std::string> RsaBlindSigner::SignInternal(
    absl::string_view input) const {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> input_bn,
                               StringToBignum(input));
  if (BN_ucmp(input_bn.get(), rsa_modulus_.get()) >= 0) {
    return absl::InvalidArgumentError(
        "RsaSign input size too large for modulus size");
  }

  ANON_TOKENS_ASSIGN_OR_RETURN(BnCtxPtr ctx, GetAndStartBigNumCtx());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> result, NewBigNum());
  // TODO(b/271438266): Replace with constant-time implementation.
  if (!BN_mod_exp(result.get(), input_bn.get(), augmented_rsa_d_.get(),
                  rsa_modulus_.get(), ctx.get())) {
    return absl::InternalError("BN_mod_exp_mont_consttime failed in RsaSign");
  }

  // Verify the result to protect against fault attacks as described in
  // boringssl. Also serves as a check for correctness.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> vrfy, NewBigNum());
  if (vrfy == nullptr ||
      !BN_mod_exp(vrfy.get(), result.get(), augmented_rsa_e_.get(),
                  rsa_modulus_.get(), ctx.get()) ||
      BN_cmp(vrfy.get(), input_bn.get()) != 0) {
    return absl::InternalError("Signature verification failed in RsaSign");
  }

  return BignumToString(*result, BN_num_bytes(rsa_modulus_.get()));
}

absl::StatusOr<std::string> RsaBlindSigner::Sign(
    const absl::string_view blinded_data) const {
  if (blinded_data.empty() || blinded_data.data() == nullptr) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  }

  int mod_size;
  if (!public_metadata_.has_value()) {
    mod_size = RSA_size(rsa_standard_key_.get());
  } else {
    mod_size = BN_num_bytes(rsa_modulus_.get());
  }
  if (blinded_data.size() != mod_size) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", mod_size,
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }

  std::string signature(mod_size, 0);
  if (!public_metadata_.has_value()) {
    // Compute a raw RSA signature.
    size_t out_len;
    if (RSA_sign_raw(/*rsa=*/rsa_standard_key_.get(), /*out_len=*/&out_len,
                     /*out=*/reinterpret_cast<uint8_t*>(&signature[0]),
                     /*max_out=*/mod_size,
                     /*in=*/reinterpret_cast<const uint8_t*>(&blinded_data[0]),
                     /*in_len=*/mod_size,
                     /*padding=*/RSA_NO_PADDING) != kBsslSuccess) {
      return absl::InternalError(
          "RSA_sign_raw failed when called from RsaBlindSigner::Sign");
    }
    if (out_len != mod_size && out_len == signature.size()) {
      return absl::InternalError(absl::StrCat(
          "Expected value of out_len = ", mod_size,
          " bytes, actual value of out_len and signature.size() = ", out_len,
          " and ", signature.size(), " bytes."));
    }
  } else {
    // As public metadata is not empty, we cannot use RSA_sign_raw as it might
    // err on exponent size.
    ANON_TOKENS_ASSIGN_OR_RETURN(signature, SignInternal(blinded_data));
    if (signature.size() != mod_size) {
      return absl::InternalError(absl::StrCat(
          "Expected value of signature.size() = ", mod_size,
          " bytes, actual value of signature.size() = ", signature.size(),
          " bytes."));
    }
  }
  return signature;
}

}  // namespace anonymous_tokens
}  // namespace private_membership
