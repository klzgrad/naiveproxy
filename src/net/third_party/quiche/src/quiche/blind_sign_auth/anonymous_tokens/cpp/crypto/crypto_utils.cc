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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "openssl/bytestring.h"
#include "openssl/err.h"
#include "openssl/hkdf.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

namespace internal {

// Approximation of sqrt(2) taken from
// //depot/google3/third_party/openssl/boringssl/src/crypto/fipsmodule/rsa/rsa_impl.c;l=997
constexpr uint32_t kBoringSSLRSASqrtTwo[] = {
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

namespace {

// Marshals an RSA public key in the DER format.
absl::StatusOr<std::string> MarshalRsaPublicKey(const RSA* rsa) {
  uint8_t* rsa_public_key_bytes;
  size_t rsa_public_key_bytes_len = 0;
  if (!RSA_public_key_to_bytes(&rsa_public_key_bytes, &rsa_public_key_bytes_len,
                               rsa)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to marshall rsa public key to a DER encoded RSAPublicKey "
        "structure (RFC 8017): ",
        GetSslErrors()));
  }
  std::string rsa_public_key_str(reinterpret_cast<char*>(rsa_public_key_bytes),
                                 rsa_public_key_bytes_len);
  OPENSSL_free(rsa_public_key_bytes);
  return rsa_public_key_str;
}

}  // namespace

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

absl::StatusOr<bssl::UniquePtr<BIGNUM>> GetRsaSqrtTwo(int x) {
  // Compute hard-coded sqrt(2).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> sqrt2, NewBigNum());
  // TODO(b/277606961): simplify RsaSqrtTwo initialization logic
  const int sqrt2_size = sizeof(internal::kBoringSSLRSASqrtTwo) /
                         sizeof(*internal::kBoringSSLRSASqrtTwo);
  for (int i = sqrt2_size - 2; i >= 0; i = i - 2) {
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
    if (BN_add_word(sqrt2.get(), internal::kBoringSSLRSASqrtTwo[i + 1]) != 1) {
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
  int sqrt2_bits = 32 * sqrt2_size;
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

absl::StatusOr<bssl::UniquePtr<RSA>> CreatePrivateKeyRSA(
    const absl::string_view rsa_modulus,
    const absl::string_view public_exponent,
    const absl::string_view private_exponent, const absl::string_view p,
    const absl::string_view q, const absl::string_view dp,
    const absl::string_view dq, const absl::string_view crt) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> n_bn,
                               StringToBignum(rsa_modulus));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> e_bn,
                               StringToBignum(public_exponent));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> d_bn,
                               StringToBignum(private_exponent));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> p_bn, StringToBignum(p));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> q_bn, StringToBignum(q));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dp_bn,
                               StringToBignum(dp));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dq_bn,
                               StringToBignum(dq));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> crt_bn,
                               StringToBignum(crt));

  bssl::UniquePtr<RSA> rsa_private_key(
      RSA_new_private_key(n_bn.get(), e_bn.get(), d_bn.get(), p_bn.get(),
                          q_bn.get(), dp_bn.get(), dq_bn.get(), crt_bn.get()));
  if (!rsa_private_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new_private_key failed: ", GetSslErrors()));
  }
  return rsa_private_key;
}

absl::StatusOr<bssl::UniquePtr<RSA>> CreatePublicKeyRSA(
    const absl::string_view rsa_modulus,
    const absl::string_view public_exponent) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> n_bn,
                               StringToBignum(rsa_modulus));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> e_bn,
                               StringToBignum(public_exponent));
  // Convert to OpenSSL RSA.
  bssl::UniquePtr<RSA> rsa_public_key(
      RSA_new_public_key(n_bn.get(), e_bn.get()));
  if (!rsa_public_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new_public_key failed: ", GetSslErrors()));
  }
  return rsa_public_key;
}

absl::StatusOr<bssl::UniquePtr<RSA>> CreatePublicKeyRSAWithPublicMetadata(
    const BIGNUM& rsa_modulus, const BIGNUM& public_exponent,
    absl::string_view public_metadata, const bool use_rsa_public_exponent) {
  bssl::UniquePtr<BIGNUM> derived_rsa_e;
  if (use_rsa_public_exponent) {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        derived_rsa_e, ComputeExponentWithPublicMetadataAndPublicExponent(
                           rsa_modulus, public_exponent, public_metadata));
  } else {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        derived_rsa_e,
        ComputeExponentWithPublicMetadata(rsa_modulus, public_metadata));
  }
  bssl::UniquePtr<RSA> rsa_public_key = bssl::UniquePtr<RSA>(
      RSA_new_public_key_large_e(&rsa_modulus, derived_rsa_e.get()));
  if (!rsa_public_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new_public_key_large_e failed: ", GetSslErrors()));
  }
  return rsa_public_key;
}

absl::StatusOr<bssl::UniquePtr<RSA>> CreatePublicKeyRSAWithPublicMetadata(
    const absl::string_view rsa_modulus,
    const absl::string_view public_exponent,
    const absl::string_view public_metadata,
    const bool use_rsa_public_exponent) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_n,
                               StringToBignum(rsa_modulus));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_e,
                               StringToBignum(public_exponent));
  return CreatePublicKeyRSAWithPublicMetadata(
      *rsa_n.get(), *rsa_e.get(), public_metadata, use_rsa_public_exponent);
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

absl::StatusOr<bssl::UniquePtr<BIGNUM>> ComputeExponentWithPublicMetadata(
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

absl::StatusOr<bssl::UniquePtr<BIGNUM>>
ComputeExponentWithPublicMetadataAndPublicExponent(
    const BIGNUM& n, const BIGNUM& e, absl::string_view public_metadata) {
  ANON_TOKENS_ASSIGN_OR_RETURN(
      bssl::UniquePtr<BIGNUM> md_exp,
      ComputeExponentWithPublicMetadata(n, public_metadata));
  ANON_TOKENS_ASSIGN_OR_RETURN(BnCtxPtr bn_ctx, GetAndStartBigNumCtx());
  // new_e=e*md_exp
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_e, NewBigNum());
  if (BN_mul(new_e.get(), md_exp.get(), &e, bn_ctx.get()) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("Unable to multiply e with md_exp: ", GetSslErrors()));
  }
  return new_e;
}

absl::Status RsaBlindSignatureVerify(const int salt_length,
                                     const EVP_MD* sig_hash,
                                     const EVP_MD* mgf1_hash,
                                     const absl::string_view signature,
                                     const absl::string_view message,
                                     RSA* rsa_public_key) {
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string message_digest,
                               ComputeHash(message, *sig_hash));
  const int hash_size = EVP_MD_size(sig_hash);
  // Make sure the size of the digest is correct.
  if (message_digest.size() != hash_size) {
    return absl::InvalidArgumentError(
        absl::StrCat("Size of the digest doesn't match the one "
                     "of the hashing algorithm; expected ",
                     hash_size, " got ", message_digest.size()));
  }
  // Make sure the size of the signature is correct.
  const int rsa_modulus_size = BN_num_bytes(RSA_get0_n(rsa_public_key));
  if (signature.size() != rsa_modulus_size) {
    return absl::InvalidArgumentError(
        "Signature size not equal to modulus size.");
  }

  std::string recovered_message_digest(rsa_modulus_size, 0);
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

absl::StatusOr<std::string> RsaSsaPssPublicKeyToDerEncoding(const RSA* rsa) {
  if (rsa == NULL) {
    return absl::InvalidArgumentError("Public Key rsa is null.");
  }
  // Create DER encoded RSA public key string.
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string rsa_public_key_str,
                               MarshalRsaPublicKey(rsa));
  // Main CRYPTO ByteBuilder object cbb which will be passed to CBB_finish to
  // finalize and output the DER encoding of the RsaSsaPssPublicKey.
  bssl::ScopedCBB cbb;
  // initial_capacity only serves as a hint.
  if (!CBB_init(cbb.get(), /*initial_capacity=*/2 * RSA_size(rsa))) {
    return absl::InternalError("CBB_init() failed.");
  }

  // Temporary CBB objects to write ASN1 sequences and object identifiers into.
  CBB outer_seq, inner_seq, param_seq, sha384_seq, mgf1_seq, mgf1_sha384_seq;
  CBB param0_tag, param1_tag, param2_tag;
  CBB rsassa_pss_oid, sha384_oid, mgf1_oid, mgf1_sha384_oid;
  CBB public_key_bit_str_cbb;
  // RsaSsaPssPublicKey ASN.1 structure example:
  //
  //  SEQUENCE {                                               # outer_seq
  //    SEQUENCE {                                             # inner_seq
  //      OBJECT_IDENTIFIER{1.2.840.113549.1.1.10}             # rsassa_pss_oid
  //      SEQUENCE {                                           # param_seq
  //        [0] {                                              # param0_tag
  //              {                                            # sha384_seq
  //                OBJECT_IDENTIFIER{2.16.840.1.101.3.4.2.2}  # sha384_oid
  //              }
  //            }
  //        [1] {                                              # param1_tag
  //              {                                            # mgf1_seq
  //                OBJECT_IDENTIFIER{1.2.840.113549.1.1.8}    # mgf1_oid
  //                {                                          # mgf1_sha384_seq
  //                  OBJECT_IDENTIFIER{2.16.840.1.101.3.4.2.2}# mgf1_sha384_oid
  //                }
  //              }
  //            }
  //        [2] {                                              # param2_tag
  //              INTEGER { 48 }                               # salt length
  //            }
  //      }
  //    }
  //    BIT STRING {                                    # public_key_bit_str_cbb
  //      0                                             # unused bits
  //      der_encoded_rsa_public_key_structure
  //    }
  //  }
  //
  // Start with the outer sequence.
  if (!CBB_add_asn1(cbb.get(), &outer_seq, CBS_ASN1_SEQUENCE) ||
      // The outer sequence consists of two parts; the inner sequence and the
      // encoded rsa public key.
      //
      // Add the inner sequence to the outer sequence.
      !CBB_add_asn1(&outer_seq, &inner_seq, CBS_ASN1_SEQUENCE) ||
      // Add object identifier for RSASSA-PSS algorithm to the inner sequence.
      !CBB_add_asn1(&inner_seq, &rsassa_pss_oid, CBS_ASN1_OBJECT) ||
      !CBB_add_asn1_oid_from_text(&rsassa_pss_oid, kRsaSsaPssOid,
                                  strlen(kRsaSsaPssOid)) ||
      // Add a parameter sequence to the inner sequence.
      !CBB_add_asn1(&inner_seq, &param_seq, CBS_ASN1_SEQUENCE) ||
      // SHA384 hash function algorithm identifier will be parameter 0 in the
      // parameter sequence.
      !CBB_add_asn1(&param_seq, &param0_tag,
                    CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 0) ||
      !CBB_add_asn1(&param0_tag, &sha384_seq, CBS_ASN1_SEQUENCE) ||
      // Add SHA384 object identifier to finish the SHA384 algorithm identifier
      // and parameter 0.
      !CBB_add_asn1(&sha384_seq, &sha384_oid, CBS_ASN1_OBJECT) ||
      !CBB_add_asn1_oid_from_text(&sha384_oid, kSha384Oid,
                                  strlen(kSha384Oid)) ||
      // mgf1-SHA384 algorithm identifier as parameter 1 to the parameter
      // sequence.
      !CBB_add_asn1(&param_seq, &param1_tag,
                    CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 1) ||
      !CBB_add_asn1(&param1_tag, &mgf1_seq, CBS_ASN1_SEQUENCE) ||
      // Add mgf1 object identifier to the mgf1-SHA384 algorithm identifier.
      !CBB_add_asn1(&mgf1_seq, &mgf1_oid, CBS_ASN1_OBJECT) ||
      !CBB_add_asn1_oid_from_text(&mgf1_oid, kRsaSsaPssMgf1Oid,
                                  strlen(kRsaSsaPssMgf1Oid)) ||
      // Add SHA384 algorithm identifier to the mgf1-SHA384 algorithm
      // identifier.
      !CBB_add_asn1(&mgf1_seq, &mgf1_sha384_seq, CBS_ASN1_SEQUENCE) ||
      // Add SHA384 object identifier to finish SHA384 algorithm identifier,
      // mgf1-SHA384 algorithm identifier and parameter 1.
      !CBB_add_asn1(&mgf1_sha384_seq, &mgf1_sha384_oid, CBS_ASN1_OBJECT) ||
      !CBB_add_asn1_oid_from_text(&mgf1_sha384_oid, kSha384Oid,
                                  strlen(kSha384Oid)) ||
      // Add salt length as parameter 2 to the parameter sequence to finish the
      // parameter sequence and the inner sequence.
      !CBB_add_asn1(&param_seq, &param2_tag,
                    CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC | 2) ||
      !CBB_add_asn1_int64(&param2_tag, kSaltLengthInBytes48) ||
      // Add public key to the outer sequence as an ASN1 bitstring.
      !CBB_add_asn1(&outer_seq, &public_key_bit_str_cbb, CBS_ASN1_BITSTRING) ||
      !CBB_add_u8(&public_key_bit_str_cbb, 0 /* no unused bits */) ||
      !CBB_add_bytes(
          &public_key_bit_str_cbb,
          reinterpret_cast<const uint8_t*>(rsa_public_key_str.data()),
          rsa_public_key_str.size())) {
    return absl::InvalidArgumentError(
        "Failed to set the crypto byte builder object.");
  }
  // Finish creating the DER-encoding of RsaSsaPssPublicKey.
  uint8_t* rsa_ssa_pss_public_key_der;
  size_t rsa_ssa_pss_public_key_der_len;
  if (!CBB_finish(cbb.get(), &rsa_ssa_pss_public_key_der,
                  &rsa_ssa_pss_public_key_der_len)) {
    return absl::InternalError("CBB_finish() failed.");
  }
  std::string rsa_ssa_pss_public_key_der_str(
      reinterpret_cast<const char*>(rsa_ssa_pss_public_key_der),
      rsa_ssa_pss_public_key_der_len);
  // Free memory.
  OPENSSL_free(rsa_ssa_pss_public_key_der);
  // Return the DER encoding as string.
  return rsa_ssa_pss_public_key_der_str;
}

}  // namespace anonymous_tokens
}  // namespace private_membership
