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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_CRYPTO_UTILS_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_CRYPTO_UTILS_H_

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/base.h"
#include "openssl/bn.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "quiche/common/platform/api/quiche_export.h"
// copybara:strip_begin(internal comment)
// The QUICHE_EXPORT annotation is necessary for some classes and functions
// to link correctly on Windows. Please do not remove them!
// copybara:strip_end

namespace private_membership {
namespace anonymous_tokens {

// Internal functions only exposed for testing.
namespace internal {

// Outputs a public metadata `hash` using HKDF with the public metadata as
// input and the rsa modulus as salt. The expected output hash size is passed as
// out_len_bytes.
//
// Implementation follows the steps listed in
// https://datatracker.ietf.org/doc/draft-amjad-cfrg-partially-blind-rsa/
//
// This method internally calls HKDF with output size of more than
// out_len_bytes and later truncates the output to out_len_bytes. This is done
// so that the output is indifferentiable from truly random bytes.
// https://cfrg.github.io/draft-irtf-cfrg-hash-to-curve/draft-irtf-cfrg-hash-to-curve.html#name-hashing-to-a-finite-field
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT
PublicMetadataHashWithHKDF(absl::string_view public_metadata,
                           absl::string_view rsa_modulus_str,
                           size_t out_len_bytes);

}  // namespace internal

// Deletes a BN_CTX.
class BnCtxDeleter {
 public:
  void operator()(BN_CTX* ctx) { BN_CTX_free(ctx); }
};
typedef std::unique_ptr<BN_CTX, BnCtxDeleter> BnCtxPtr;

// Deletes a BN_MONT_CTX.
class BnMontCtxDeleter {
 public:
  void operator()(BN_MONT_CTX* mont_ctx) { BN_MONT_CTX_free(mont_ctx); }
};
typedef std::unique_ptr<BN_MONT_CTX, BnMontCtxDeleter> BnMontCtxPtr;

// Deletes an EVP_MD_CTX.
class EvpMdCtxDeleter {
 public:
  void operator()(EVP_MD_CTX* ctx) { EVP_MD_CTX_destroy(ctx); }
};
typedef std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter> EvpMdCtxPtr;

// Creates and starts a BIGNUM context.
absl::StatusOr<BnCtxPtr> QUICHE_EXPORT GetAndStartBigNumCtx();

// Creates a new BIGNUM.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT NewBigNum();

// Converts a BIGNUM to string.
absl::StatusOr<std::string> QUICHE_EXPORT BignumToString(
    const BIGNUM& big_num, size_t output_len);

// Converts a string to BIGNUM.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT StringToBignum(
    absl::string_view input_str);

// Retrieve error messages from OpenSSL.
std::string QUICHE_EXPORT GetSslErrors();

// Generate a message mask. For more details, see
// https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/
absl::StatusOr<std::string> QUICHE_EXPORT GenerateMask(
    const RSABlindSignaturePublicKey& public_key);

// Mask message using protocol at
// https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/
std::string QUICHE_EXPORT MaskMessageConcat(absl::string_view mask,
                                                  absl::string_view message);

// Encode Message and Public Metadata using steps in
// https://datatracker.ietf.org/doc/draft-amjad-cfrg-partially-blind-rsa/
//
// The length of public metadata must fit in 4 bytes.
std::string QUICHE_EXPORT EncodeMessagePublicMetadata(
    absl::string_view message, absl::string_view public_metadata);

// Compute 2^(x - 1/2).
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT GetRsaSqrtTwo(
    int x);

// Compute compute 2^x.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT ComputePowerOfTwo(
    int x);

// Converts the AnonymousTokens proto hash type to the equivalent EVP digest.
absl::StatusOr<const EVP_MD*> QUICHE_EXPORT
ProtoHashTypeToEVPDigest(HashType hash_type);

// Converts the AnonymousTokens proto hash type for mask generation function to
// the equivalent EVP digest.
absl::StatusOr<const EVP_MD*> QUICHE_EXPORT
ProtoMaskGenFunctionToEVPDigest(MaskGenFunction mgf);

// ComputeHash sub-routine used during blindness and verification of RSA blind
// signatures protocol with or without public metadata.
absl::StatusOr<std::string> QUICHE_EXPORT ComputeHash(
    absl::string_view input, const EVP_MD& hasher);

// Computes the Carmichael LCM given phi(p) and phi(q) where N = p*q is a safe
// RSA modulus.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT
ComputeCarmichaelLcm(const BIGNUM& phi_p, const BIGNUM& phi_q, BN_CTX& bn_ctx);

// Converts AnonymousTokens::RSAPrivateKey to bssl::UniquePtr<RSA> without
// public metadata augmentation.
absl::StatusOr<bssl::UniquePtr<RSA>> QUICHE_EXPORT
AnonymousTokensRSAPrivateKeyToRSA(const RSAPrivateKey& private_key);

// Converts AnonymousTokens::RSAPublicKey to bssl::UniquePtr<RSA> without
// public metadata augmentation.
absl::StatusOr<bssl::UniquePtr<RSA>> QUICHE_EXPORT
AnonymousTokensRSAPublicKeyToRSA(const RSAPublicKey& public_key);

// Compute exponent based only on the public metadata. Assumes that n is a safe
// modulus i.e. it produces a strong RSA key pair. If not, the exponent may be
// invalid.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT
PublicMetadataExponent(const BIGNUM& n, absl::string_view public_metadata);

// Computes final exponent by multiplying the public exponent e with the
// exponent derived from public metadata. Assumes that n is a safe modulus i.e.
// it produces a strong RSA key pair. If not, the exponent may be invalid.
//
// Empty public metadata is considered to be a valid value for public_metadata
// and will output an exponent different than `e` as well.
absl::StatusOr<bssl::UniquePtr<BIGNUM>> QUICHE_EXPORT
ComputeFinalExponentUnderPublicMetadata(const BIGNUM& n, const BIGNUM& e,
                                        absl::string_view public_metadata);

// Helper method that implements RSA PSS Blind Signatures verification protocol
// for both the standard scheme as well as the public metadata version.
//
// The standard public exponent e in rsa_public_key should always have a
// standard value even if the public_metada is not std::nullopt.
//
// If the public_metadata is set to std::nullopt, augmented_rsa_e should be
// equal to a standard public exponent same as the value of e in rsa_public_key.
// Otherwise, it will be equal to a new public exponent value derived using the
// public metadata.
absl::Status QUICHE_EXPORT RsaBlindSignatureVerify(
    int salt_length, const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    RSA* rsa_public_key, const BIGNUM& rsa_modulus,
    const BIGNUM& augmented_rsa_e, absl::string_view signature,
    absl::string_view message,
    std::optional<absl::string_view> public_metadata = std::nullopt);

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_CRYPTO_UTILS_H_
