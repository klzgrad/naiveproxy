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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"

#include <stddef.h>
#include <stdint.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/err.h"
#include "openssl/hkdf.h"
#include "openssl/rand.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

namespace internal {

// Approximation of sqrt(2) taken from
// //depot/google3/third_party/openssl/boringssl/src/crypto/fipsmodule/rsa/rsa_impl.c;l=997
const std::vector<uint32_t> kBoringSSLRSASqrtTwo = {
    0x4d7c60a5, 0xe633e3e1, 0x5fcf8f7b, 0xca3ea33b, 0xc246785e, 0x92957023,
    0xf9acce41, 0x797f2805, 0xfdfe170f, 0xd3b1f780, 0xd24f4a76, 0x3facb882,
    0x18838a2e, 0xaff5f3b2, 0xc1fcbdde, 0xa2f7dc33, 0xdea06241, 0xf7aa81c2,
    0xf6a1be3f, 0xca221307, 0x332a5e9f, 0x7bda1ebf, 0x0104dc01, 0xfe32352f,
    0xb8cf341b, 0x6f8236c7, 0x4264dabc, 0xd528b651, 0xf4d3a02c, 0xebc93e0c,
    0x81394ab6, 0xd8fd0efd, 0xeaa4a089, 0x9040ca4a, 0xf52f120f, 0x836e582e,
    0xcb2a6343, 0x31f3c84d, 0xc6d5a8a3, 0x8bb7e9dc, 0x460abc72, 0x2f7c4e33,
    0xcab1bc91, 0x1688458a, 0x53059c60, 0x11bc337b, 0xd2202e87, 0x42af1f4e,
    0x78048736, 0x3dfa2768, 0x0f74a85e, 0x439c7b4a, 0xa8b1fe6f, 0xdc83db39,
    0x4afc8304, 0x3ab8a2c3, 0xed17ac85, 0x83339915, 0x1d6f60ba, 0x893ba84c,
    0x597d89b3, 0x754abe9f, 0xb504f333, 0xf9de6484,
};

absl::StatusOr<bssl::UniquePtr<BIGNUM>> PublicMetadataHashWithHKDF(
    absl::string_view public_metadata, absl::string_view rsa_modulus_str,
    size_t out_len_bytes) {
  const EVP_MD* evp_md_sha_384 = EVP_sha384();
  // Prepend "key" to input.
  std::string modified_input = absl::StrCat("key", public_metadata);
  std::vector<uint8_t> input_buffer(modified_input.begin(),
                                    modified_input.end());
  // Append 0x00 to input.
  input_buffer.push_back(0x00);
  std::string out_e;
  // We set the out_e size beyond out_len_bytes so that out_e bytes are
  // indifferentiable from truly random bytes even after truncations.
  //
  // Expanding to 16 more bytes is sufficient.
  // https://cfrg.github.io/draft-irtf-cfrg-hash-to-curve/draft-irtf-cfrg-hash-to-curve.html#name-hashing-to-a-finite-field
  const size_t hkdf_output_size = out_len_bytes + 16;
  out_e.resize(hkdf_output_size);
  // The modulus is used as salt to ensure different outputs for same metadata
  // and different modulus.
  if (HKDF(reinterpret_cast<uint8_t*>(out_e.data()), hkdf_output_size,
           evp_md_sha_384, input_buffer.data(), input_buffer.size(),
           reinterpret_cast<const uint8_t*>(rsa_modulus_str.data()),
           rsa_modulus_str.size(),
           reinterpret_cast<const uint8_t*>(kHkdfPublicMetadataInfo.data()),
           kHkdfPublicMetadataInfoSizeInBytes) != kBsslSuccess) {
    return absl::InternalError("HKDF failed in public_metadata_crypto_utils");
  }
  // Truncate out_e to out_len_bytes
  out_e.resize(out_len_bytes);
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> out,
                               StringToBignum(out_e));
  return out;
}

}  // namespace internal

absl::StatusOr<BnCtxPtr> GetAndStartBigNumCtx() {
  // Create context to be used in intermediate computation.
  BnCtxPtr bn_ctx = BnCtxPtr(BN_CTX_new());
  if (!bn_ctx.get()) {
    return absl::InternalError("Error generating bignum context.");
  }
  BN_CTX_start(bn_ctx.get());

  return bn_ctx;
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> NewBigNum() {
  bssl::UniquePtr<BIGNUM> bn(BN_new());
  if (!bn.get()) {
    return absl::InternalError("Error generating bignum.");
  }
  return bn;
}

absl::StatusOr<std::string> BignumToString(const BIGNUM& big_num,
                                           const size_t output_len) {
  std::vector<uint8_t> serialization(output_len);
  if (BN_bn2bin_padded(serialization.data(), serialization.size(), &big_num) !=
      kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("Function BN_bn2bin_padded failed: ", GetSslErrors()));
  }
  return std::string(std::make_move_iterator(serialization.begin()),
                     std::make_move_iterator(serialization.end()));
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> StringToBignum(
    const absl::string_view input_str) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> output, NewBigNum());
  if (!BN_bin2bn(reinterpret_cast<const uint8_t*>(input_str.data()),
                 input_str.size(), output.get())) {
    return absl::InternalError(
        absl::StrCat("Function BN_bin2bn failed: ", GetSslErrors()));
  }
  if (!output.get()) {
    return absl::InternalError("Function BN_bin2bn failed.");
  }
  return output;
}

std::string GetSslErrors() {
  std::string ret;
  ERR_print_errors_cb(
      [](const char* str, size_t len, void* ctx) -> int {
        static_cast<std::string*>(ctx)->append(str, len);
        return 1;
      },
      &ret);
  return ret;
}

absl::StatusOr<std::string> GenerateMask(
    const RSABlindSignaturePublicKey& public_key) {
  std::string mask;
  if (public_key.message_mask_type() == AT_MESSAGE_MASK_CONCAT &&
      public_key.message_mask_size() >= kRsaMessageMaskSizeInBytes32) {
    mask = std::string(public_key.message_mask_size(), '\0');
    RAND_bytes(reinterpret_cast<uint8_t*>(mask.data()), mask.size());
  } else {
    return absl::InvalidArgumentError(
        "Undefined or unsupported message mask type.");
  }
  return mask;
}

std::string MaskMessageConcat(absl::string_view mask,
                              absl::string_view message) {
  return absl::StrCat(mask, message);
}

std::string EncodeMessagePublicMetadata(absl::string_view message,
                                        absl::string_view public_metadata) {
  // Prepend encoding of "msg" followed by 4 bytes representing public metadata
  // length.
  std::string tag = "msg";
  std::vector<uint8_t> buffer(tag.begin(), tag.end());
  buffer.push_back((public_metadata.size() >> 24) & 0xFF);
  buffer.push_back((public_metadata.size() >> 16) & 0xFF);
  buffer.push_back((public_metadata.size() >> 8) & 0xFF);
  buffer.push_back((public_metadata.size() >> 0) & 0xFF);

  // Finally append public metadata and then the message to the output.
  std::string encoding(buffer.begin(), buffer.end());
  return absl::StrCat(encoding, public_metadata, message);
}

absl::StatusOr<const EVP_MD*> ProtoHashTypeToEVPDigest(
    const HashType hash_type) {
  switch (hash_type) {
    case AT_HASH_TYPE_SHA256:
      return EVP_sha256();
    case AT_HASH_TYPE_SHA384:
      return EVP_sha384();
    case AT_HASH_TYPE_UNDEFINED:
    default:
      return absl::InvalidArgumentError("Unknown hash type.");
  }
}

absl::StatusOr<const EVP_MD*> ProtoMaskGenFunctionToEVPDigest(
    const MaskGenFunction mgf) {
  switch (mgf) {
    case AT_MGF_SHA256:
      return EVP_sha256();
    case AT_MGF_SHA384:
      return EVP_sha384();
    case AT_MGF_UNDEFINED:
    default:
      return absl::InvalidArgumentError(
          "Unknown hash type for mask generation hash function.");
  }
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> GetRsaSqrtTwo(int x) {
  // Compute hard-coded sqrt(2).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> sqrt2, NewBigNum());
  // TODO(b/277606961): simplify RsaSqrtTwo initialization logic
  for (int i = internal::kBoringSSLRSASqrtTwo.size() - 2; i >= 0; i = i - 2) {
    // Add the uint32_t values as words directly and shift.
    // 'i' is the "hi" value of a uint64_t, and 'i+1' is the "lo" value.
    if (BN_add_word(sqrt2.get(), internal::kBoringSSLRSASqrtTwo[i]) != 1) {
      return absl::InternalError(absl::StrCat(
          "Cannot add word to compute RSA sqrt(2): ", GetSslErrors()));
    }
    if (BN_lshift(sqrt2.get(), sqrt2.get(), 32) != 1) {
        return absl::InternalError(absl::StrCat(
            "Cannot shift to compute RSA sqrt(2): ", GetSslErrors()));
    }
    if (BN_add_word(sqrt2.get(), internal::kBoringSSLRSASqrtTwo[i+1]) != 1) {
      return absl::InternalError(absl::StrCat(
          "Cannot add word to compute RSA sqrt(2): ", GetSslErrors()));
    }
    if (i > 0) {
      if (BN_lshift(sqrt2.get(), sqrt2.get(), 32) != 1) {
        return absl::InternalError(absl::StrCat(
            "Cannot shift to compute RSA sqrt(2): ", GetSslErrors()));
      }
    }
  }

  // Check that hard-coded result is correct length.
  int sqrt2_bits = 32 * internal::kBoringSSLRSASqrtTwo.size();
  if (BN_num_bits(sqrt2.get()) != sqrt2_bits) {
    return absl::InternalError("RSA sqrt(2) is not correct length.");
  }

  // Either shift left or right depending on value x.
  if (sqrt2_bits > x) {
    if (BN_rshift(sqrt2.get(), sqrt2.get(), sqrt2_bits - x) != 1) {
      return absl::InternalError(
          absl::StrCat("Cannot rshift to compute 2^(x-1/2): ", GetSslErrors()));
    }
  } else {
    // Round up and be pessimistic about minimium factors.
    if (BN_add_word(sqrt2.get(), 1) != 1 ||
        BN_lshift(sqrt2.get(), sqrt2.get(), x - sqrt2_bits) != 1) {
      return absl::InternalError(absl::StrCat(
          "Cannot add/lshift to compute 2^(x-1/2): ", GetSslErrors()));
    }
  }

  // Check that 2^(x - 1/2) is correct length.
  if (BN_num_bits(sqrt2.get()) != x) {
    return absl::InternalError(
        "2^(x-1/2) is not correct length after shifting.");
  }

  return std::move(sqrt2);
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> ComputePowerOfTwo(int x) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> ret, NewBigNum());
  if (BN_set_bit(ret.get(), x) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to set bit to compute 2^x: ", GetSslErrors()));
  }
  if (!BN_is_pow2(ret.get()) || !BN_is_bit_set(ret.get(), x)) {
    return absl::InternalError(absl::StrCat("Unable to compute 2^", x, "."));
  }
  return ret;
}

absl::StatusOr<std::string> ComputeHash(absl::string_view input,
                                        const EVP_MD& hasher) {
  std::string digest;
  digest.resize(EVP_MAX_MD_SIZE);

  uint32_t digest_length = 0;
  if (EVP_Digest(input.data(), input.length(),
                 reinterpret_cast<uint8_t*>(&digest[0]), &digest_length,
                 &hasher, /*impl=*/nullptr) != 1) {
    return absl::InternalError(absl::StrCat(
        "Openssl internal error computing hash: ", GetSslErrors()));
  }
  digest.resize(digest_length);
  return digest;
}

absl::StatusOr<bssl::UniquePtr<RSA>> AnonymousTokensRSAPrivateKeyToRSA(
    const RSAPrivateKey& private_key) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> n,
                               StringToBignum(private_key.n()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> e,
                               StringToBignum(private_key.e()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> d,
                               StringToBignum(private_key.d()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> p,
                               StringToBignum(private_key.p()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> q,
                               StringToBignum(private_key.q()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dp,
                               StringToBignum(private_key.dp()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dq,
                               StringToBignum(private_key.dq()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> crt,
                               StringToBignum(private_key.crt()));

  bssl::UniquePtr<RSA> rsa_private_key(RSA_new());
  // Populate private key.
  if (!rsa_private_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new failed: ", GetSslErrors()));
  } else if (RSA_set0_key(rsa_private_key.get(), n.get(), e.get(), d.get()) !=
             kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("RSA_set0_key failed: ", GetSslErrors()));
  } else if (RSA_set0_factors(rsa_private_key.get(), p.get(), q.get()) !=
             kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("RSA_set0_factors failed: ", GetSslErrors()));
  } else if (RSA_set0_crt_params(rsa_private_key.get(), dp.get(), dq.get(),
                                 crt.get()) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("RSA_set0_crt_params failed: ", GetSslErrors()));
  } else {
    n.release();
    e.release();
    d.release();
    p.release();
    q.release();
    dp.release();
    dq.release();
    crt.release();
  }
  return std::move(rsa_private_key);
}

absl::StatusOr<bssl::UniquePtr<RSA>> AnonymousTokensRSAPublicKeyToRSA(
    const RSAPublicKey& public_key) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(public_key.n()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_e,
                               StringToBignum(public_key.e()));
  // Convert to OpenSSL RSA.
  bssl::UniquePtr<RSA> rsa_public_key(RSA_new());
  if (!rsa_public_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new failed: ", GetSslErrors()));
  } else if (RSA_set0_key(rsa_public_key.get(), rsa_modulus.get(), rsa_e.get(),
                          nullptr) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("RSA_set0_key failed: ", GetSslErrors()));
  }
  // RSA_set0_key takes ownership of the pointers under rsa_modulus, new_e on
  // success.
  rsa_modulus.release();
  rsa_e.release();
  return rsa_public_key;
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> ComputeCarmichaelLcm(
    const BIGNUM& phi_p, const BIGNUM& phi_q, BN_CTX& bn_ctx) {
  // To compute lcm(phi(p), phi(q)), we first compute phi(n) =
  // (p-1)(q-1). As n is assumed to be a safe RSA modulus (signing_key is
  // assumed to be part of a strong rsa key pair), phi(n) = (p-1)(q-1) =
  // (2 phi(p))(2 phi(q)) = 4 * phi(p) * phi(q) where phi(p) and phi(q) are also
  // primes. So we get the lcm by outputting phi(n) >> 1 = 2 * phi(p) * phi(q).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_n, NewBigNum());
  if (BN_mul(phi_n.get(), &phi_p, &phi_q, &bn_ctx) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(n): ", GetSslErrors()));
  }
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> lcm, NewBigNum());
  if (BN_rshift1(lcm.get(), phi_n.get()) != 1) {
    return absl::InternalError(absl::StrCat(
        "Could not compute LCM(phi(p), phi(q)): ", GetSslErrors()));
  }
  return lcm;
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> PublicMetadataExponent(
    const BIGNUM& n, absl::string_view public_metadata) {
  // Check modulus length.
  if (BN_num_bits(&n) % 2 == 1) {
    return absl::InvalidArgumentError(
        "Strong RSA modulus should be even length.");
  }
  int modulus_bytes = BN_num_bytes(&n);
  // The integer modulus_bytes is expected to be a power of 2.
  int prime_bytes = modulus_bytes / 2;

  ANON_TOKENS_ASSIGN_OR_RETURN(std::string rsa_modulus_str,
                               BignumToString(n, modulus_bytes));

  // Get HKDF output of length prime_bytes.
  ANON_TOKENS_ASSIGN_OR_RETURN(
      bssl::UniquePtr<BIGNUM> exponent,
      internal::PublicMetadataHashWithHKDF(public_metadata, rsa_modulus_str,
                                           prime_bytes));

  // We need to generate random odd exponents < 2^(primes_bits - 2) where
  // prime_bits = prime_bytes * 8. This will guarantee that the resulting
  // exponent is coprime to phi(N) = 4p'q' as 2^(prime_bits - 2) < p', q' <
  // 2^(prime_bits - 1).
  //
  // To do this, we can truncate the HKDF output (exponent) which is prime_bits
  // long, to prime_bits - 2, by clearing its top two bits. We then set the
  // least significant bit to 1. This way the final exponent will be less than
  // 2^(primes_bits - 2) and will always be odd.
  if (BN_clear_bit(exponent.get(), (prime_bytes * 8) - 1) != kBsslSuccess ||
      BN_clear_bit(exponent.get(), (prime_bytes * 8) - 2) != kBsslSuccess ||
      BN_set_bit(exponent.get(), 0) != kBsslSuccess) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Could not clear the two most significant bits and set the least "
        "significant bit to zero: ",
        GetSslErrors()));
  }
  // Check that exponent is small enough to ensure it is coprime to phi(n).
  if (BN_num_bits(exponent.get()) >= (8 * prime_bytes - 1)) {
    return absl::InternalError("Generated exponent is too large.");
  }

  return exponent;
}

absl::StatusOr<bssl::UniquePtr<BIGNUM>> ComputeFinalExponentUnderPublicMetadata(
    const BIGNUM& n, const BIGNUM& e, absl::string_view public_metadata) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> md_exp,
                               PublicMetadataExponent(n, public_metadata));
  ANON_TOKENS_ASSIGN_OR_RETURN(BnCtxPtr bn_ctx, GetAndStartBigNumCtx());
  // new_e=e*md_exp
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_e, NewBigNum());
  if (BN_mul(new_e.get(), md_exp.get(), &e, bn_ctx.get()) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("Unable to multiply e with md_exp: ", GetSslErrors()));
  }
  return new_e;
}

absl::Status RsaBlindSignatureVerify(
    const int salt_length, const EVP_MD* sig_hash, const EVP_MD* mgf1_hash,
    RSA* rsa_public_key, const BIGNUM& rsa_modulus,
    const BIGNUM& augmented_rsa_e, absl::string_view signature,
    absl::string_view message,
    std::optional<absl::string_view> public_metadata) {
  std::string augmented_message(message);
  if (public_metadata.has_value()) {
    augmented_message = EncodeMessagePublicMetadata(message, *public_metadata);
  }
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string message_digest,
                               ComputeHash(augmented_message, *sig_hash));
  const int hash_size = EVP_MD_size(sig_hash);
  // Make sure the size of the digest is correct.
  if (message_digest.size() != hash_size) {
    return absl::InvalidArgumentError(
        absl::StrCat("Size of the digest doesn't match the one "
                     "of the hashing algorithm; expected ",
                     hash_size, " got ", message_digest.size()));
  }
  const int rsa_modulus_size = BN_num_bytes(&rsa_modulus);
  if (signature.size() != rsa_modulus_size) {
    return absl::InvalidArgumentError(
        "Signature size not equal to modulus size.");
  }

  std::string recovered_message_digest(rsa_modulus_size, 0);
  if (!public_metadata.has_value()) {
    int recovered_message_digest_size = RSA_public_decrypt(
        /*flen=*/signature.size(),
        /*from=*/reinterpret_cast<const uint8_t*>(signature.data()),
        /*to=*/
        reinterpret_cast<uint8_t*>(recovered_message_digest.data()),
        /*rsa=*/rsa_public_key,
        /*padding=*/RSA_NO_PADDING);
    if (recovered_message_digest_size != rsa_modulus_size) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid signature size (likely an incorrect key is "
                       "used); expected ",
                       rsa_modulus_size, " got ", recovered_message_digest_size,
                       ": ", GetSslErrors()));
    }
  } else {
    ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> signature_bn,
                                 StringToBignum(signature));
    if (BN_ucmp(signature_bn.get(), &rsa_modulus) >= 0) {
      return absl::InternalError("Data too large for modulus.");
    }
    ANON_TOKENS_ASSIGN_OR_RETURN(BnCtxPtr bn_ctx, GetAndStartBigNumCtx());
    bssl::UniquePtr<BN_MONT_CTX> bn_mont_ctx(
        BN_MONT_CTX_new_for_modulus(&rsa_modulus, bn_ctx.get()));
    if (!bn_mont_ctx) {
      return absl::InternalError("BN_MONT_CTX_new_for_modulus failed.");
    }
    ANON_TOKENS_ASSIGN_OR_RETURN(
        bssl::UniquePtr<BIGNUM> recovered_message_digest_bn, NewBigNum());
    if (BN_mod_exp_mont(recovered_message_digest_bn.get(), signature_bn.get(),
                        &augmented_rsa_e, &rsa_modulus, bn_ctx.get(),
                        bn_mont_ctx.get()) != kBsslSuccess) {
      return absl::InternalError("Exponentiation failed.");
    }
    ANON_TOKENS_ASSIGN_OR_RETURN(
        recovered_message_digest,
        BignumToString(*recovered_message_digest_bn, rsa_modulus_size));
  }
  if (RSA_verify_PKCS1_PSS_mgf1(
          rsa_public_key, reinterpret_cast<const uint8_t*>(&message_digest[0]),
          sig_hash, mgf1_hash,
          reinterpret_cast<const uint8_t*>(recovered_message_digest.data()),
          salt_length) != kBsslSuccess) {
    return absl::InvalidArgumentError(
        absl::StrCat("PSS padding verification failed: ", GetSslErrors()));
  }
  return absl::OkStatus();
}

}  // namespace anonymous_tokens
}  // namespace private_membership
