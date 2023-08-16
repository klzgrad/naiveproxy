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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/rsa_blinder.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/digest.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

absl::StatusOr<std::unique_ptr<RsaBlinder>> RsaBlinder::New(
    const RSABlindSignaturePublicKey& public_key,
    std::optional<absl::string_view> public_metadata) {
  RSAPublicKey rsa_public_key_proto;
  if (!rsa_public_key_proto.ParseFromString(
          public_key.serialized_public_key())) {
    return absl::InvalidArgumentError("Public key is malformed.");
  }

  // Convert to OpenSSL RSA which will be used in the code paths for the
  // standard RSA blind signature scheme.
  //
  // Moreover, it will also be passed as an argument to PSS related padding and
  // padding verification methods irrespective of whether RsaBlinder is being
  // used as a part of the standard RSA blind signature scheme or the scheme
  // with public metadata support.
  ANON_TOKENS_ASSIGN_OR_RETURN(
      bssl::UniquePtr<RSA> rsa_public_key,
      AnonymousTokensRSAPublicKeyToRSA(rsa_public_key_proto));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(rsa_public_key_proto.n()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_e,
                               StringToBignum(rsa_public_key_proto.e()));

  bssl::UniquePtr<BIGNUM> augmented_rsa_e = nullptr;
  // If public metadata is supported, RsaBlinder will compute a new public
  // exponent using the public metadata.
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

  // Owned by BoringSSL.
  ANON_TOKENS_ASSIGN_OR_RETURN(
      const EVP_MD* sig_hash,
      ProtoHashTypeToEVPDigest(public_key.sig_hash_type()));

  // Owned by BoringSSL.
  ANON_TOKENS_ASSIGN_OR_RETURN(
      const EVP_MD* mgf1_hash,
      ProtoMaskGenFunctionToEVPDigest(public_key.mask_gen_function()));

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> r, NewBigNum());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> r_inv_mont, NewBigNum());

  // Limit r between [2, n) so that an r of 1 never happens. An r of 1 doesn't
  // blind.
  if (BN_rand_range_ex(r.get(), 2, rsa_modulus.get()) != kBsslSuccess) {
    return absl::InternalError(
        "BN_rand_range_ex failed when called from RsaBlinder::New.");
  }

  bssl::UniquePtr<BN_CTX> bn_ctx(BN_CTX_new());
  if (!bn_ctx) {
    return absl::InternalError("BN_CTX_new failed.");
  }

  bssl::UniquePtr<BN_MONT_CTX> bn_mont_ctx(
      BN_MONT_CTX_new_for_modulus(rsa_modulus.get(), bn_ctx.get()));
  if (!bn_mont_ctx) {
    return absl::InternalError("BN_MONT_CTX_new_for_modulus failed.");
  }

  // We wish to compute r^-1 in the Montgomery domain, or r^-1 R mod n. This is
  // can be done with BN_mod_inverse_blinded followed by BN_to_montgomery, but
  // it is equivalent and slightly more efficient to first compute r R^-1 mod n
  // with BN_from_montgomery, and then inverting that to give r^-1 R mod n.
  int is_r_not_invertible = 0;
  if (BN_from_montgomery(r_inv_mont.get(), r.get(), bn_mont_ctx.get(),
                         bn_ctx.get()) != kBsslSuccess ||
      BN_mod_inverse_blinded(r_inv_mont.get(), &is_r_not_invertible,
                             r_inv_mont.get(), bn_mont_ctx.get(),
                             bn_ctx.get()) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("BN_mod_inverse failed when called from RsaBlinder::New, "
                     "is_r_not_invertible = ",
                     is_r_not_invertible));
  }

  return absl::WrapUnique(new RsaBlinder(
      public_key.salt_length(), public_metadata, sig_hash, mgf1_hash,
      std::move(rsa_public_key), std::move(rsa_modulus),
      std::move(augmented_rsa_e), std::move(r), std::move(r_inv_mont),
      std::move(bn_mont_ctx)));
}

RsaBlinder::RsaBlinder(
    int salt_length, std::optional<absl::string_view> public_metadata,
    const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    bssl::UniquePtr<RSA> rsa_public_key, bssl::UniquePtr<BIGNUM> rsa_modulus,
    bssl::UniquePtr<BIGNUM> augmented_rsa_e, bssl::UniquePtr<BIGNUM> r,
    bssl::UniquePtr<BIGNUM> r_inv_mont, bssl::UniquePtr<BN_MONT_CTX> mont_n)
    : salt_length_(salt_length),
      public_metadata_(public_metadata),
      sig_hash_(sig_hash),
      mgf1_hash_(mgf1_hash),
      rsa_public_key_(std::move(rsa_public_key)),
      rsa_modulus_(std::move(rsa_modulus)),
      augmented_rsa_e_(std::move(augmented_rsa_e)),
      r_(std::move(r)),
      r_inv_mont_(std::move(r_inv_mont)),
      mont_n_(std::move(mont_n)),
      blinder_state_(RsaBlinder::BlinderState::kCreated) {}

absl::StatusOr<std::string> RsaBlinder::Blind(const absl::string_view message) {
  // Check that the blinder state was kCreated
  if (blinder_state_ != RsaBlinder::BlinderState::kCreated) {
    return absl::FailedPreconditionError(
        "RsaBlinder is in wrong state to blind message.");
  }
  std::string augmented_message(message);
  if (public_metadata_.has_value()) {
    augmented_message = EncodeMessagePublicMetadata(message, *public_metadata_);
  }
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string digest_str,
                               ComputeHash(augmented_message, *sig_hash_));
  std::vector<uint8_t> digest(digest_str.begin(), digest_str.end());

  // Construct the PSS padded message, using the same workflow as BoringSSL's
  // RSA_sign_pss_mgf1 for processing the message (but not signing the message):
  // google3/third_party/openssl/boringssl/src/crypto/fipsmodule/rsa/rsa.c?l=557
  if (digest.size() != EVP_MD_size(sig_hash_)) {
    return absl::InternalError("Invalid input message length.");
  }

  // Allocate for padded length
  const int padded_len = BN_num_bytes(rsa_modulus_.get());
  std::vector<uint8_t> padded(padded_len);

  // The |md| and |mgf1_md| arguments identify the hash used to calculate
  // |digest| and the MGF1 hash, respectively. If |mgf1_md| is NULL, |md| is
  // used. |salt_len| specifies the expected salt length in bytes. If |salt_len|
  // is -1, then the salt length is the same as the hash length. If -2, then the
  // salt length is maximal given the size of |rsa|. If unsure, use -1.
  if (RSA_padding_add_PKCS1_PSS_mgf1(
          /*rsa=*/rsa_public_key_.get(), /*EM=*/padded.data(),
          /*mHash=*/digest.data(), /*Hash=*/sig_hash_, /*mgf1Hash=*/mgf1_hash_,
          /*sLen=*/salt_length_) != kBsslSuccess) {
    return absl::InternalError(
        "RSA_padding_add_PKCS1_PSS_mgf1 failed when called from "
        "RsaBlinder::Blind");
  }

  bssl::UniquePtr<BN_CTX> bn_ctx(BN_CTX_new());
  if (!bn_ctx) {
    return absl::InternalError("BN_CTX_new failed.");
  }

  std::string encoded_message(padded.begin(), padded.end());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> encoded_message_bn,
                               StringToBignum(encoded_message));

  // Take `r^e mod n`. This is an equivalent operation to RSA_encrypt, without
  // extra encode/decode trips.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rE, NewBigNum());
  if (BN_mod_exp_mont(rE.get(), r_.get(), augmented_rsa_e_.get(),
                      rsa_modulus_.get(), bn_ctx.get(),
                      mont_n_.get()) != kBsslSuccess) {
    return absl::InternalError(
        "BN_mod_exp_mont failed when called from RsaBlinder::Blind.");
  }

  // Do `encoded_message*r^e mod n`.
  //
  // To avoid leaking side channels, we use Montgomery reduction. This would be
  // FromMontgomery(ModMulMontgomery(ToMontgomery(m), ToMontgomery(r^e))).
  // However, this is equivalent to ModMulMontgomery(m, ToMontgomery(r^e)).
  // Each BN_mod_mul_montgomery removes a factor of R, so by having only one
  // input in the Montgomery domain, we save a To/FromMontgomery pair.
  //
  // Internally, BN_mod_exp_mont actually computes r^e in the Montgomery domain
  // and converts it out, but there is no public API for this, so we perform an
  // extra conversion.
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> multiplication_res,
                               NewBigNum());
  if (BN_to_montgomery(multiplication_res.get(), rE.get(), mont_n_.get(),
                       bn_ctx.get()) != kBsslSuccess ||
      BN_mod_mul_montgomery(multiplication_res.get(), encoded_message_bn.get(),
                            multiplication_res.get(), mont_n_.get(),
                            bn_ctx.get()) != kBsslSuccess) {
    return absl::InternalError(
        "BN_mod_mul failed when called from RsaBlinder::Blind.");
  }

  absl::StatusOr<std::string> blinded_msg =
      BignumToString(*multiplication_res, BN_num_bytes(rsa_modulus_.get()));

  // Update RsaBlinder state to kBlinded
  blinder_state_ = RsaBlinder::BlinderState::kBlinded;

  return blinded_msg;
}

// Unblinds `blind_signature`.
absl::StatusOr<std::string> RsaBlinder::Unblind(
    const absl::string_view blind_signature) {
  if (blinder_state_ != RsaBlinder::BlinderState::kBlinded) {
    return absl::FailedPreconditionError(
        "RsaBlinder is in wrong state to unblind signature.");
  }
  const int mod_size = BN_num_bytes(rsa_modulus_.get());
  // Parse the signed_blinded_data as BIGNUM.
  if (blind_signature.size() != mod_size) {
    return absl::InternalError(absl::StrCat(
        "Expected blind signature size = ", mod_size,
        " actual blind signature size = ", blind_signature.size(), " bytes."));
  }

  bssl::UniquePtr<BN_CTX> bn_ctx(BN_CTX_new());
  if (!bn_ctx) {
    return absl::InternalError("BN_CTX_new failed.");
  }

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> signed_big_num,
                               StringToBignum(blind_signature));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> unblinded_sig_big,
                               NewBigNum());
  // Do `signed_message*r^-1 mod n`.
  //
  // To avoid leaking side channels, we use Montgomery reduction. This would be
  // FromMontgomery(ModMulMontgomery(ToMontgomery(m), ToMontgomery(r^-1))).
  // However, this is equivalent to ModMulMontgomery(m, ToMontgomery(r^-1)).
  // Each BN_mod_mul_montgomery removes a factor of R, so by having only one
  // input in the Montgomery domain, we save a To/FromMontgomery pair.
  if (BN_mod_mul_montgomery(unblinded_sig_big.get(), signed_big_num.get(),
                            r_inv_mont_.get(), mont_n_.get(),
                            bn_ctx.get()) != kBsslSuccess) {
    return absl::InternalError(
        "BN_mod_mul failed when called from RsaBlinder::Unblind.");
  }
  absl::StatusOr<std::string> unblinded_signed_message =
      BignumToString(*unblinded_sig_big,
                     /*output_len=*/BN_num_bytes(rsa_modulus_.get()));
  blinder_state_ = RsaBlinder::BlinderState::kUnblinded;
  return unblinded_signed_message;
}

absl::Status RsaBlinder::Verify(absl::string_view signature,
                                absl::string_view message) {
  return RsaBlindSignatureVerify(salt_length_, sig_hash_, mgf1_hash_,
                                 rsa_public_key_.get(), *rsa_modulus_.get(),
                                 *augmented_rsa_e_.get(), signature, message,
                                 public_metadata_);
}

}  // namespace anonymous_tokens
}  // namespace private_membership
