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
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/testing/utils.h"

#include <stddef.h>
#include <stdint.h>

#include <fstream>
#include <ios>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

namespace {

absl::StatusOr<std::string> ReadFileToString(absl::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file.is_open()) {
    return absl::InternalError("Reading file failed.");
  }
  std::ostringstream ss(std::ios::binary);
  ss << file.rdbuf();
  return ss.str();
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> ParseRsaKeysFromFile(
    absl::string_view path) {
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string binary_proto,
                               ReadFileToString(path));
  RSAPrivateKey private_key;
  if (!private_key.ParseFromString(binary_proto)) {
    return absl::InternalError("Parsing binary proto failed.");
  }
  RSAPublicKey public_key;
  public_key.set_n(private_key.n());
  public_key.set_e(private_key.e());
  return std::make_pair(std::move(public_key), std::move(private_key));
}

absl::StatusOr<bssl::UniquePtr<RSA>> GenerateRSAKey(int modulus_bit_size,
                                                    const BIGNUM& e) {
  bssl::UniquePtr<RSA> rsa(RSA_new());
  if (!rsa.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new failed: ", GetSslErrors()));
  }
  if (RSA_generate_key_ex(rsa.get(), modulus_bit_size, &e,
                          /*cb=*/nullptr) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("Error generating private key: ", GetSslErrors()));
  }
  return rsa;
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> PopulateTestVectorKeys(
    const std::string& n, const std::string& e, const std::string& d,
    const std::string& p, const std::string& q) {
  RSAPublicKey public_key;
  RSAPrivateKey private_key;

  public_key.set_n(n);
  public_key.set_e(e);

  private_key.set_n(n);
  private_key.set_e(e);
  private_key.set_d(d);
  private_key.set_p(p);
  private_key.set_q(q);

  // Computing CRT parameters
  ANON_TOKENS_ASSIGN_OR_RETURN(BnCtxPtr bn_ctx, GetAndStartBigNumCtx());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dp_bn, NewBigNum());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> dq_bn, NewBigNum());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> crt_bn, NewBigNum());

  // p - 1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> pm1, StringToBignum(p));
  BN_sub_word(pm1.get(), 1);
  // q - 1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> qm1, StringToBignum(q));
  BN_sub_word(qm1.get(), 1);
  // d mod p-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> d_bn, StringToBignum(d));
  BN_mod(dp_bn.get(), d_bn.get(), pm1.get(), bn_ctx.get());
  // d mod q-1
  BN_mod(dq_bn.get(), d_bn.get(), qm1.get(), bn_ctx.get());
  // crt q^(-1) mod p
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> q_bn, StringToBignum(q));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> p_bn, StringToBignum(p));
  BN_mod_inverse(crt_bn.get(), q_bn.get(), p_bn.get(), bn_ctx.get());

  // Populating crt params in private key
  ANON_TOKENS_ASSIGN_OR_RETURN(
      std::string dp_str, BignumToString(*dp_bn, BN_num_bytes(dp_bn.get())));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      std::string dq_str, BignumToString(*dq_bn, BN_num_bytes(dq_bn.get())));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      std::string crt_str, BignumToString(*crt_bn, BN_num_bytes(crt_bn.get())));
  private_key.set_dp(dp_str);
  private_key.set_dq(dq_str);
  private_key.set_crt(crt_str);

  return std::make_pair(std::move(public_key), std::move(private_key));
}

}  // namespace

absl::StatusOr<std::pair<bssl::UniquePtr<RSA>, RSABlindSignaturePublicKey>>
CreateTestKey(int key_size, HashType sig_hash, MaskGenFunction mfg1_hash,
              int salt_length, MessageMaskType message_mask_type,
              int message_mask_size) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_f4, NewBigNum());
  BN_set_u64(rsa_f4.get(), RSA_F4);

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<RSA> rsa_key,
                               GenerateRSAKey(key_size * 8, *rsa_f4));

  RSAPublicKey rsa_public_key;
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_public_key.mutable_n(),
      BignumToString(*RSA_get0_n(rsa_key.get()), key_size));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_public_key.mutable_e(),
      BignumToString(*RSA_get0_e(rsa_key.get()), key_size));

  RSABlindSignaturePublicKey public_key;
  public_key.set_serialized_public_key(rsa_public_key.SerializeAsString());
  public_key.set_sig_hash_type(sig_hash);
  public_key.set_mask_gen_function(mfg1_hash);
  public_key.set_salt_length(salt_length);
  public_key.set_key_size(key_size);
  public_key.set_message_mask_type(message_mask_type);
  public_key.set_message_mask_size(message_mask_size);

  return std::make_pair(std::move(rsa_key), std::move(public_key));
}

absl::StatusOr<std::string> TestSign(const absl::string_view blinded_data,
                                     RSA* rsa_key) {
  if (blinded_data.empty()) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  }
  const size_t mod_size = RSA_size(rsa_key);
  if (blinded_data.size() != mod_size) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", mod_size,
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }
  // Compute a raw RSA signature.
  std::string signature(mod_size, 0);
  size_t out_len;
  if (RSA_sign_raw(/*rsa=*/rsa_key, /*out_len=*/&out_len,
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
  return signature;
}

absl::StatusOr<std::string> TestSignWithPublicMetadata(
    const absl::string_view blinded_data, absl::string_view public_metadata,
    const RSA& rsa_key) {
  if (blinded_data.empty()) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  } else if (blinded_data.size() != RSA_size(&rsa_key)) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", RSA_size(&rsa_key),
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }
  ANON_TOKENS_ASSIGN_OR_RETURN(
      bssl::UniquePtr<BIGNUM> new_e,
      ComputeFinalExponentUnderPublicMetadata(
          *RSA_get0_n(&rsa_key), *RSA_get0_e(&rsa_key), public_metadata));
  // Compute phi(p) = p-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_p, NewBigNum());
  if (BN_sub(phi_p.get(), RSA_get0_p(&rsa_key), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(p): ", GetSslErrors()));
  }
  // Compute phi(q) = q-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_q, NewBigNum());
  if (BN_sub(phi_q.get(), RSA_get0_q(&rsa_key), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(q): ", GetSslErrors()));
  }
  // Compute phi(n) = phi(p)*phi(q)
  ANON_TOKENS_ASSIGN_OR_RETURN(auto ctx, GetAndStartBigNumCtx());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_n, NewBigNum());
  if (BN_mul(phi_n.get(), phi_p.get(), phi_q.get(), ctx.get()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(n): ", GetSslErrors()));
  }
  // Compute lcm(phi(p), phi(q)).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> lcm, NewBigNum());
  if (BN_rshift1(lcm.get(), phi_n.get()) != 1) {
    return absl::InternalError(absl::StrCat(
        "Could not compute LCM(phi(p), phi(q)): ", GetSslErrors()));
  }
  // Compute the new private exponent new_d
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_d, NewBigNum());
  if (!BN_mod_inverse(new_d.get(), new_e.get(), lcm.get(), ctx.get())) {
    return absl::InternalError(
        absl::StrCat("Could not compute private exponent d: ", GetSslErrors()));
  }

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> input_bn,
                               StringToBignum(blinded_data));
  if (BN_ucmp(input_bn.get(), RSA_get0_n(&rsa_key)) >= 0) {
    return absl::InvalidArgumentError(
        "RsaSign input size too large for modulus size");
  }

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> result, NewBigNum());
  if (!BN_mod_exp(result.get(), input_bn.get(), new_d.get(),
                  RSA_get0_n(&rsa_key), ctx.get())) {
    return absl::InternalError(
        "BN_mod_exp failed in TestSignWithPublicMetadata");
  }

  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> vrfy, NewBigNum());
  if (vrfy == nullptr ||
      !BN_mod_exp(vrfy.get(), result.get(), new_e.get(), RSA_get0_n(&rsa_key),
                  ctx.get()) ||
      BN_cmp(vrfy.get(), input_bn.get()) != 0) {
    return absl::InternalError("Signature verification failed in RsaSign");
  }

  return BignumToString(*result, BN_num_bytes(RSA_get0_n(&rsa_key)));
}

absl::StatusOr<std::string> EncodeMessageForTests(absl::string_view message,
                                                  RSAPublicKey public_key,
                                                  const EVP_MD* sig_hasher,
                                                  const EVP_MD* mgf1_hasher,
                                                  int32_t salt_length) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_modulus,
                               StringToBignum(public_key.n()));
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> e,
                               StringToBignum(public_key.e()));
  // Convert to OpenSSL RSA.
  bssl::UniquePtr<RSA> rsa_public_key(RSA_new());
  if (!rsa_public_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new failed: ", GetSslErrors()));
  } else if (RSA_set0_key(rsa_public_key.get(), rsa_modulus.release(),
                          e.release(), nullptr) != kBsslSuccess) {
    return absl::InternalError(
        absl::StrCat("RSA_set0_key failed: ", GetSslErrors()));
  }

  const int padded_len = RSA_size(rsa_public_key.get());
  std::vector<uint8_t> padded(padded_len);
  ANON_TOKENS_ASSIGN_OR_RETURN(std::string digest,
                               ComputeHash(message, *sig_hasher));
  if (RSA_padding_add_PKCS1_PSS_mgf1(
          /*rsa=*/rsa_public_key.get(), /*EM=*/padded.data(),
          /*mHash=*/reinterpret_cast<uint8_t*>(&digest[0]), /*Hash=*/sig_hasher,
          /*mgf1Hash=*/mgf1_hasher,
          /*sLen=*/salt_length) != kBsslSuccess) {
    return absl::InternalError(
        "RSA_padding_add_PKCS1_PSS_mgf1 failed when called from "
        "testing_utils");
  }
  std::string encoded_message(padded.begin(), padded.end());
  return encoded_message;
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStandardRsaKeyPair(
    int modulus_size_in_bytes) {
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> rsa_f4, NewBigNum());
  BN_set_u64(rsa_f4.get(), RSA_F4);
  ANON_TOKENS_ASSIGN_OR_RETURN(
      bssl::UniquePtr<RSA> rsa_key,
      GenerateRSAKey(modulus_size_in_bytes * 8, *rsa_f4));

  RSAPublicKey rsa_public_key;
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_public_key.mutable_n(),
      BignumToString(*RSA_get0_n(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_public_key.mutable_e(),
      BignumToString(*RSA_get0_e(rsa_key.get()), modulus_size_in_bytes));

  RSAPrivateKey rsa_private_key;
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_n(),
      BignumToString(*RSA_get0_n(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_e(),
      BignumToString(*RSA_get0_e(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_d(),
      BignumToString(*RSA_get0_d(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_p(),
      BignumToString(*RSA_get0_p(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_q(),
      BignumToString(*RSA_get0_q(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_dp(),
      BignumToString(*RSA_get0_dmp1(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_dq(),
      BignumToString(*RSA_get0_dmq1(rsa_key.get()), modulus_size_in_bytes));
  ANON_TOKENS_ASSIGN_OR_RETURN(
      *rsa_private_key.mutable_crt(),
      BignumToString(*RSA_get0_iqmp(rsa_key.get()), modulus_size_in_bytes));

  return std::make_pair(std::move(rsa_public_key), std::move(rsa_private_key));
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys2048() {
  std::string path = absl::StrCat(quiche::test::QuicheGetCommonSourcePath(),
                                  "/anonymous_tokens/testdata/strong_rsa_modulus2048_example.binarypb");
  ANON_TOKENS_ASSIGN_OR_RETURN(auto key_pair, ParseRsaKeysFromFile(path));
  return std::make_pair(std::move(key_pair.first), std::move(key_pair.second));
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetAnotherStrongRsaKeys2048() {
  std::string path = absl::StrCat(quiche::test::QuicheGetCommonSourcePath(),
                                  "/anonymous_tokens/testdata/strong_rsa_modulus2048_example_2.binarypb");
  ANON_TOKENS_ASSIGN_OR_RETURN(auto key_pair, ParseRsaKeysFromFile(path));
  return std::make_pair(std::move(key_pair.first), std::move(key_pair.second));
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys3072() {
  std::string path = absl::StrCat(quiche::test::QuicheGetCommonSourcePath(),
                                  "/anonymous_tokens/testdata/strong_rsa_modulus3072_example.binarypb");
  ANON_TOKENS_ASSIGN_OR_RETURN(auto key_pair, ParseRsaKeysFromFile(path));
  return std::make_pair(std::move(key_pair.first), std::move(key_pair.second));
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys4096() {
  std::string path = absl::StrCat(quiche::test::QuicheGetCommonSourcePath(),
                                  "/anonymous_tokens/testdata/strong_rsa_modulus4096_example.binarypb");
  ANON_TOKENS_ASSIGN_OR_RETURN(auto key_pair, ParseRsaKeysFromFile(path));
  return std::make_pair(std::move(key_pair.first), std::move(key_pair.second));
}

IetfStandardRsaBlindSignatureTestVector
GetIetfStandardRsaBlindSignatureTestVector() {
  IetfStandardRsaBlindSignatureTestVector test_vector = {
      // n
      absl::HexStringToBytes(
          "aec4d69addc70b990ea66a5e70603b6fee27aafebd08f2d94cbe1250c556e047a9"
          "28d635c3f45ee9b66d1bc628a03bac9b7c3f416fe20dabea8f3d7b4bbf7f963be3"
          "35d2328d67e6c13ee4a8f955e05a3283720d3e1f139c38e43e0338ad058a9495c5"
          "3377fc35be64d208f89b4aa721bf7f7d3fef837be2a80e0f8adf0bcd1eec5bb040"
          "443a2b2792fdca522a7472aed74f31a1ebe1eebc1f408660a0543dfe2a850f106a"
          "617ec6685573702eaaa21a5640a5dcaf9b74e397fa3af18a2f1b7c03ba91a63361"
          "58de420d63188ee143866ee415735d155b7c2d854d795b7bc236cffd71542df342"
          "34221a0413e142d8c61355cc44d45bda94204974557ac2704cd8b593f035a5724b"
          "1adf442e78c542cd4414fce6f1298182fb6d8e53cef1adfd2e90e1e4deec52999b"
          "dc6c29144e8d52a125232c8c6d75c706ea3cc06841c7bda33568c63a6c03817f72"
          "2b50fcf898237d788a4400869e44d90a3020923dc646388abcc914315215fcd1ba"
          "e11b1c751fd52443aac8f601087d8d42737c18a3fa11ecd4131ecae017ae0a14ac"
          "fc4ef85b83c19fed33cfd1cd629da2c4c09e222b398e18d822f77bb378dea3cb36"
          "0b605e5aa58b20edc29d000a66bd177c682a17e7eb12a63ef7c2e4183e0d898f3d"
          "6bf567ba8ae84f84f1d23bf8b8e261c3729e2fa6d07b832e07cddd1d14f55325c6"
          "f924267957121902dc19b3b32948bdead5"),
      // e
      absl::HexStringToBytes("010001"),
      // d
      absl::HexStringToBytes(
          "0d43242aefe1fb2c13fbc66e20b678c4336d20b1808c558b6e62ad16a287077180b1"
          "77e1f01b12f9c6cd6c52630257ccef26a45135a990928773f3bd2fc01a313f1dac97"
          "a51cec71cb1fd7efc7adffdeb05f1fb04812c924ed7f4a8269925dad88bd7dcfbc4e"
          "f01020ebfc60cb3e04c54f981fdbd273e69a8a58b8ceb7c2d83fbcbd6f784d052201"
          "b88a9848186f2a45c0d2826870733e6fd9aa46983e0a6e82e35ca20a439c5ee7b502"
          "a9062e1066493bdadf8b49eb30d9558ed85abc7afb29b3c9bc644199654a4676681a"
          "f4babcea4e6f71fe4565c9c1b85d9985b84ec1abf1a820a9bbebee0df1398aae2c85"
          "ab580a9f13e7743afd3108eb32100b870648fa6bc17e8abac4d3c99246b1f0ea9f7f"
          "93a5dd5458c56d9f3f81ff2216b3c3680a13591673c43194d8e6fc93fc1e37ce2986"
          "bd628ac48088bc723d8fbe293861ca7a9f4a73e9fa63b1b6d0074f5dea2a624c5249"
          "ff3ad811b6255b299d6bc5451ba7477f19c5a0db690c3e6476398b1483d10314afd3"
          "8bbaf6e2fbdbcd62c3ca9797a420ca6034ec0a83360a3ee2adf4b9d4ba29731d131b"
          "099a38d6a23cc463db754603211260e99d19affc902c915d7854554aabf608e3ac52"
          "c19b8aa26ae042249b17b2d29669b5c859103ee53ef9bdc73ba3c6b537d5c34b6d8f"
          "034671d7f3a8a6966cc4543df223565343154140fd7391c7e7be03e241f4ecfeb877"
          "a051"),
      // p
      absl::HexStringToBytes(
          "e1f4d7a34802e27c7392a3cea32a262a34dc3691bd87f3f310dc75673488930559c1"
          "20fd0410194fb8a0da55bd0b81227e843fdca6692ae80e5a5d414116d4803fca7d8c"
          "30eaaae57e44a1816ebb5c5b0606c536246c7f11985d731684150b63c9a3ad9e41b0"
          "4c0b5b27cb188a692c84696b742a80d3cd00ab891f2457443dadfeba6d6daf108602"
          "be26d7071803c67105a5426838e6889d77e8474b29244cefaf418e381b312048b457"
          "d73419213063c60ee7b0d81820165864fef93523c9635c22210956e53a8d96322493"
          "ffc58d845368e2416e078e5bcb5d2fd68ae6acfa54f9627c42e84a9d3f2774017e32"
          "ebca06308a12ecc290c7cd1156dcccfb2311"),
      // q
      absl::HexStringToBytes(
          "c601a9caea66dc3835827b539db9df6f6f5ae77244692780cd334a006ab353c80642"
          "6b60718c05245650821d39445d3ab591ed10a7339f15d83fe13f6a3dfb20b9452c6a"
          "9b42eaa62a68c970df3cadb2139f804ad8223d56108dfde30ba7d367e9b0a7a80c4f"
          "dba2fd9dde6661fc73fc2947569d2029f2870fc02d8325acf28c9afa19ecf962daa7"
          "916e21afad09eb62fe9f1cf91b77dc879b7974b490d3ebd2e95426057f35d0a3c9f4"
          "5f79ac727ab81a519a8b9285932d9b2e5ccd347e59f3f32ad9ca359115e7da008ab7"
          "406707bd0e8e185a5ed8758b5ba266e8828f8d863ae133846304a2936ad7bc7c9803"
          "879d2fc4a28e69291d73dbd799f8bc238385"),
      // message
      absl::HexStringToBytes("8f3dc6fb8c4a02f4d6352edf0907822c1210a"
                             "9b32f9bdda4c45a698c80023aa6b5"
                             "9f8cfec5fdbb36331372ebefedae7d"),
      // salt
      absl::HexStringToBytes("051722b35f458781397c3a671a7d3bd3096503940e4c4f1aa"
                             "a269d60300ce449555cd7340100df9d46944c5356825abf"),
      // inv
      absl::HexStringToBytes(
          "80682c48982407b489d53d1261b19ec8627d02b8cda5336750b8cee332ae260de57b"
          "02d72609c1e0e9f28e2040fc65b6f02d56dbd6aa9af8fde656f70495dfb723ba0117"
          "3d4707a12fddac628ca29f3e32340bd8f7ddb557cf819f6b01e445ad96f874ba2355"
          "84ee71f6581f62d4f43bf03f910f6510deb85e8ef06c7f09d9794a008be7ff2529f0"
          "ebb69decef646387dc767b74939265fec0223aa6d84d2a8a1cc912d5ca25b4e144ab"
          "8f6ba054b54910176d5737a2cff011da431bd5f2a0d2d66b9e70b39f4b050e45c0d9"
          "c16f02deda9ddf2d00f3e4b01037d7029cd49c2d46a8e1fc2c0c17520af1f4b5e25b"
          "a396afc4cd60c494a4c426448b35b49635b337cfb08e7c22a39b256dd032c00addda"
          "fb51a627f99a0e1704170ac1f1912e49d9db10ec04c19c58f420212973e0cb329524"
          "223a6aa56c7937c5dffdb5d966b6cd4cbc26f3201dd25c80960a1a111b32947bb789"
          "73d269fac7f5186530930ed19f68507540eed9e1bab8b00f00d8ca09b3f099aae461"
          "80e04e3584bd7ca054df18a1504b89d1d1675d0966c4ae1407be325cdf623cf13ff1"
          "3e4a28b594d59e3eadbadf6136eee7a59d6a444c9eb4e2198e8a974f27a39eb63af2"
          "c9af3870488b8adaad444674f512133ad80b9220e09158521614f1faadfe8505ef57"
          "b7df6813048603f0dd04f4280177a11380fbfc861dbcbd7418d62155248dad5fdec0"
          "991f"),
      // encoded_message
      absl::HexStringToBytes(
          "6e0c464d9c2f9fbc147b43570fc4f238e0d0b38870b3addcf7a4217df912ccef17a7"
          "f629aa850f63a063925f312d61d6437be954b45025e8282f9c0b1131bc8ff19a8a92"
          "8d859b37113db1064f92a27f64761c181c1e1f9b251ae5a2f8a4047573b67a270584"
          "e089beadcb13e7c82337797119712e9b849ff56e04385d144d3ca9d8d92bf78adb20"
          "b5bbeb3685f17038ec6afade3ef354429c51c687b45a7018ee3a6966b3af15c9ba8f"
          "40e6461ba0a17ef5a799672ad882bab02b518f9da7c1a962945c2e9b0f02f29b31b9"
          "cdf3e633f9d9d2a22e96e1de28e25241ca7dd04147112f578973403e0f4fd8086596"
          "5475d22294f065e17a1c4a201de93bd14223e6b1b999fd548f2f759f52db71964528"
          "b6f15b9c2d7811f2a0a35d534b8216301c47f4f04f412cae142b48c4cdff78bc54df"
          "690fd43142d750c671dd8e2e938e6a440b2f825b6dbb3e19f1d7a3c0150428a47948"
          "037c322365b7fe6fe57ac88d8f80889e9ff38177bad8c8d8d98db42908b389cb5969"
          "2a58ce275aa15acb032ca951b3e0a3404b7f33f655b7c7d83a2f8d1b6bbff49d5fce"
          "df2e030e80881aa436db27a5c0dea13f32e7d460dbf01240c2320c2bb5b3225b1714"
          "5c72d61d47c8f84d1e19417ebd8ce3638a82d395cc6f7050b6209d9283dc7b93fecc"
          "04f3f9e7f566829ac41568ef799480c733c09759aa9734e2013d7640dc6151018ea9"
          "02bc"),
      // blinded_message
      absl::HexStringToBytes(
          "10c166c6a711e81c46f45b18e5873cc4f494f003180dd7f115585d871a2893025965"
          "4fe28a54dab319cc5011204c8373b50a57b0fdc7a678bd74c523259dfe4fd5ea9f52"
          "f170e19dfa332930ad1609fc8a00902d725cfe50685c95e5b2968c9a2828a21207fc"
          "f393d15f849769e2af34ac4259d91dfd98c3a707c509e1af55647efaa31290ddf48e"
          "0133b798562af5eabd327270ac2fb6c594734ce339a14ea4fe1b9a2f81c0bc230ca5"
          "23bda17ff42a377266bc2778a274c0ae5ec5a8cbbe364fcf0d2403f7ee178d77ff28"
          "b67a20c7ceec009182dbcaa9bc99b51ebbf13b7d542be337172c6474f2cd3561219f"
          "e0dfa3fb207cff89632091ab841cf38d8aa88af6891539f263adb8eac6402c41b6eb"
          "d72984e43666e537f5f5fe27b2b5aa114957e9a580730308a5f5a9c63a1eb599f093"
          "ab401d0c6003a451931b6d124180305705845060ebba6b0036154fcef3e5e9f9e4b8"
          "7e8f084542fd1dd67e7782a5585150181c01eb6d90cb95883837384a5b91dbb606f2"
          "66059ecc51b5acbaa280e45cfd2eec8cc1cdb1b7211c8e14805ba683f9b78824b2eb"
          "005bc8a7d7179a36c152cb87c8219e5569bba911bb32a1b923ca83de0e03fb10fba7"
          "5d85c55907dda5a2606bf918b056c3808ba496a4d95532212040a5f44f37e1097f26"
          "dc27b98a51837daa78f23e532156296b64352669c94a8a855acf30533d8e0594ace7"
          "c442"),
      // blinded_signature
      absl::HexStringToBytes(
          "364f6a40dbfbc3bbb257943337eeff791a0f290898a6791283bba581d9eac90a6376"
          "a837241f5f73a78a5c6746e1306ba3adab6067c32ff69115734ce014d354e2f259d4"
          "cbfb890244fd451a497fe6ecf9aa90d19a2d441162f7eaa7ce3fc4e89fd4e76b7ae5"
          "85be2a2c0fd6fb246b8ac8d58bcb585634e30c9168a434786fe5e0b74bfe8187b47a"
          "c091aa571ffea0a864cb906d0e28c77a00e8cd8f6aba4317a8cc7bf32ce566bd1ef8"
          "0c64de041728abe087bee6cadd0b7062bde5ceef308a23bd1ccc154fd0c3a26110df"
          "6193464fc0d24ee189aea8979d722170ba945fdcce9b1b4b63349980f3a92dc2e541"
          "8c54d38a862916926b3f9ca270a8cf40dfb9772bfbdd9a3e0e0892369c18249211ba"
          "857f35963d0e05d8da98f1aa0c6bba58f47487b8f663e395091275f82941830b050b"
          "260e4767ce2fa903e75ff8970c98bfb3a08d6db91ab1746c86420ee2e909bf681cac"
          "173697135983c3594b2def673736220452fde4ddec867d40ff42dd3da36c84e3e525"
          "08b891a00f50b4f62d112edb3b6b6cc3dbd546ba10f36b03f06c0d82aeec3b25e127"
          "af545fac28e1613a0517a6095ad18a98ab79f68801e05c175e15bae21f821e80c80a"
          "b4fdec6fb34ca315e194502b8f3dcf7892b511aee45060e3994cd15e003861bc7220"
          "a2babd7b40eda03382548a34a7110f9b1779bf3ef6011361611e6bc5c0dc851e1509"
          "de1a"),
      // signature
      absl::HexStringToBytes(
          "6fef8bf9bc182cd8cf7ce45c7dcf0e6f3e518ae48f06f3c670c649ac737a8b8119"
          "a34d51641785be151a697ed7825fdfece82865123445eab03eb4bb91cecf4d6951"
          "738495f8481151b62de869658573df4e50a95c17c31b52e154ae26a04067d5ecdc"
          "1592c287550bb982a5bb9c30fd53a768cee6baabb3d483e9f1e2da954c7f4cf492"
          "fe3944d2fe456c1ecaf0840369e33fb4010e6b44bb1d721840513524d8e9a3519f"
          "40d1b81ae34fb7a31ee6b7ed641cb16c2ac999004c2191de0201457523f5a4700d"
          "d649267d9286f5c1d193f1454c9f868a57816bf5ff76c838a2eeb616a3fc9976f6"
          "5d4371deecfbab29362caebdff69c635fe5a2113da4d4d8c24f0b16a0584fa05e8"
          "0e607c5d9a2f765f1f069f8d4da21f27c2a3b5c984b4ab24899bef46c6d9323df4"
          "862fe51ce300fca40fb539c3bb7fe2dcc9409e425f2d3b95e70e9c49c5feb6ecc9"
          "d43442c33d50003ee936845892fb8be475647da9a080f5bc7f8a716590b3745c22"
          "09fe05b17992830ce15f32c7b22cde755c8a2fe50bd814a0434130b807dc1b7218"
          "d4e85342d70695a5d7f29306f25623ad1e8aa08ef71b54b8ee447b5f64e73d09bd"
          "d6c3b7ca224058d7c67cc7551e9241688ada12d859cb7646fbd3ed8b34312f3b49"
          "d69802f0eaa11bc4211c2f7a29cd5c01ed01a39001c5856fab36228f5ee2f2e111"
          "0811872fe7c865c42ed59029c706195d52"),
  };
  return test_vector;
}

std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfRsaBlindSignatureWithPublicMetadataTestVectors() {
  // n
  std::string n = absl::HexStringToBytes(
      "d6930820f71fe517bf3259d14d40209b02a5c0d3d61991c731dd7da39f8d69821552"
      "e2318d6c9ad897e603887a476ea3162c1205da9ac96f02edf31df049bd55f142134c"
      "17d4382a0e78e275345f165fbe8e49cdca6cf5c726c599dd39e09e75e0f330a33121"
      "e73976e4facba9cfa001c28b7c96f8134f9981db6750b43a41710f51da4240fe0310"
      "6c12acb1e7bb53d75ec7256da3fddd0718b89c365410fce61bc7c99b115fb4c3c318"
      "081fa7e1b65a37774e8e50c96e8ce2b2cc6b3b367982366a2bf9924c4bafdb3ff5e7"
      "22258ab705c76d43e5f1f121b984814e98ea2b2b8725cd9bc905c0bc3d75c2a8db70"
      "a7153213c39ae371b2b5dc1dafcb19d6fae9");
  std::string e = absl::HexStringToBytes("010001");
  std::string d = absl::HexStringToBytes(
      "4e21356983722aa1adedb084a483401c1127b781aac89eab103e1cfc52215494981d"
      "18dd8028566d9d499469c25476358de23821c78a6ae43005e26b394e3051b5ca206a"
      "a9968d68cae23b5affd9cbb4cb16d64ac7754b3cdba241b72ad6ddfc000facdb0f0d"
      "d03abd4efcfee1730748fcc47b7621182ef8af2eeb7c985349f62ce96ab373d2689b"
      "aeaea0e28ea7d45f2d605451920ca4ea1f0c08b0f1f6711eaa4b7cca66d58a6b916f"
      "9985480f90aca97210685ac7b12d2ec3e30a1c7b97b65a18d38a93189258aa346bf2"
      "bc572cd7e7359605c20221b8909d599ed9d38164c9c4abf396f897b9993c1e805e57"
      "4d704649985b600fa0ced8e5427071d7049d");
  std::string p = absl::HexStringToBytes(
      "dcd90af1be463632c0d5ea555256a20605af3db667475e190e3af12a34a3324c46a3"
      "094062c59fb4b249e0ee6afba8bee14e0276d126c99f4784b23009bf6168ff628ac1"
      "486e5ae8e23ce4d362889de4df63109cbd90ef93db5ae64372bfe1c55f832766f21e"
      "94ea3322eb2182f10a891546536ba907ad74b8d72469bea396f3");
  std::string q = absl::HexStringToBytes(
      "f8ba5c89bd068f57234a3cf54a1c89d5b4cd0194f2633ca7c60b91a795a56fa8c868"
      "6c0e37b1c4498b851e3420d08bea29f71d195cfbd3671c6ddc49cf4c1db5b478231e"
      "a9d91377ffa98fe95685fca20ba4623212b2f2def4da5b281ed0100b651f6db32112"
      "e4017d831c0da668768afa7141d45bbc279f1e0f8735d74395b3");

  std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector> test_vectors;
  // test_vector 1.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      absl::HexStringToBytes(
          "64b5c5d2b2ca672690df59bab774a389606d85d56f92a18a57c42eb4cb164d43"),
      // blinded_message
      absl::HexStringToBytes(
          "1b9e1057dd2d05a17ad2feba5f87a4083cc825fe06fc70f0b782062ea0043fa65ec8"
          "096ce5d403cfa2aa3b11195b2a655d694386058f6266450715a936b5764f42977c0a"
          "0933ff3054d456624734fd2c019def792f00d30b3ac2f27859ea56d835f80564a3ba"
          "59f3c876dc926b2a785378ca83f177f7b378513b36a074e7db59448fd4007b54c647"
          "91a33b61721ab3b5476165193af30f25164d480684d045a8d0782a53dd73774563e8"
          "d29e48b175534f696763abaab49fa03a055ec9246c5e398a5563cc88d02eb57d725d"
          "3fc9231ae5139aa7fcb9941060b0bf0192b8c81944fa0c54568b0ab4ea9c4c4c9829"
          "d6dbcbf8b48006b322ee51d784ac93e4bf13"),
      // blinded_signature
      absl::HexStringToBytes(
          "7ef75d9887f29f2232602acab43263afaea70313a0c90374388df5a7a7440d2584c4"
          "b4e5b886accc065bf4824b4b22370ddde7fea99d4cd67f8ed2e4a6a2b7b5869e8d4d"
          "0c52318320c5bf7b9f02bb132af7365c471e799edd111ca9441934c7db76c164b051"
          "5afc5607b8ceb584f5b1d2177d5180e57218265c07aec9ebde982f3961e7ddaa432e"
          "47297884da8f4512fe3dc9ab820121262e6a73850920299999c293b017cd800c6ec9"
          "94f76b6ace35ff4232f9502e6a52262e19c03de7cc27d95ccbf4c381d698fcfe1f20"
          "0209814e04ae2d6279883015bbf36cabf3e2350be1e175020ee9f4bb861ba409b467"
          "e23d08027a699ac36b2e5ab988390f3c0ee9"),
      // signature
      absl::HexStringToBytes(
          "abd6813bb4bbe3bc8dc9f8978655b22305e5481b35c5bdc4869b60e2d5cc74b84356"
          "416abaaca0ca8602cd061248587f0d492fee3534b19a3fe089de18e4df9f3a6ad289"
          "afb5323d7934487b8fafd25943766072bab873fa9cd69ce7328a57344c2c529fe969"
          "83ca701483ca353a98a1a9610391b7d32b13e14e8ef87d04c0f56a724800655636cf"
          "ff280d35d6b468f68f09f56e1b3acdb46bc6634b7a1eab5c25766cec3b5d97c37bbc"
          "a302286c17ff557bcf1a4a0e342ea9b2713ab7f935c8174377bace2e5926b3983407"
          "9761d9121f5df1fad47a51b03eab3d84d050c99cf1f68718101735267cca3213c0a4"
          "6c0537887ffe92ca05371e26d587313cc3f4"),
  });

  // test_vector 2.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      "",
      // message_mask
      absl::HexStringToBytes(
          "ebb56541b9a1758028033cfb085a4ffe048f072c6c82a71ce21d40842b5c0a89"),
      // blinded_message
      absl::HexStringToBytes(
          "d1fc97f30efbf116fadd9895130cdd55f939211f7db19ce9a85287227a02b33fb698"
          "b52399f81be0e1f598482000202ec89968085753eae1810f14676b514e08238c8aa7"
          "9d8b999af54e9f4282c6220d4d760716e48e5413f3228cc59ce10b8252916640de7b"
          "9b5c7dc9c2bff9f53b4fb5eb4a5f8bab49af3fd1b955d34312073d15030e7fdb44bd"
          "b23460d1c5662597f9947092def7fff955a5f3e63419ae9858c6405f9609b63c4331"
          "e0cf90d24c196bee554f2b78e0d8f6da3d4308c8d4ae9fbe18a8bb7fa4fc3b9cacd4"
          "263e5bd6e12ed891cfdfba8b50d0f37d7a9abe065238367907c685ed2c224924caf5"
          "d8fe41f5db898b09a0501d318d9f65d88cb8"),
      // blinded_signature
      absl::HexStringToBytes(
          "400c1bcdfa56624f15d04f6954908b5605dbeff4cd56f384d7531669970290d70652"
          "9d44cde4c972a1399635525a2859ef1d914b4130068ed407cfda3bd9d1259790a30f"
          "6d8c07d190aa98bf21ae9581e5d61801565d96e9eec134335958b3d0b905739e2fd9"
          "f39074da08f869089fe34de2d218062afa16170c1505c67b65af4dcc2f1aeccd4827"
          "5c3dacf96116557b7f8c7044d84e296a0501c511ba1e6201703e1dd834bf47a96e1a"
          "c4ec9b935233ed751239bd4b514b031522cd51615c1555e520312ed1fa43f55d4abe"
          "b222ee48b4746c79006966590004714039bac7fd18cdd54761924d91a4648e871458"
          "937061ef6549dd12d76e37ed417634d88914"),
      // signature
      absl::HexStringToBytes(
          "4062960edb71cc071e7d101db4f595aae4a98e0bfe6843aca3e5f48c9dfb46d505e8"
          "c19806ffa07f040313d44d0996ef9f69a86fa5946cb818a32627fe2df2a0e8035028"
          "8ae4fedfbee4193554cc1433d9d27639db8b4635265504d87dca7054c85e0c882d32"
          "887534405e6cc4e7eb4b174383e5ce4eebbfffb217f353102f6d1a0461ef89238de3"
          "1b0a0c134dfac0d2a8c533c807ccdd557c6510637596a490d5258b77410421be4076"
          "ecdf2d7e9044327e36e349751f3239681bba10fe633f1b246f5a9f694706316898c9"
          "00af2294f47267f2e9ad1e61c7f56bf643280258875d29f3745dfdb74b9bbcd5fe3d"
          "ea62d9be85e2c6f5aed68bc79f8b4a27b3de"),
  });

  // test_vector 3.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      absl::HexStringToBytes(
          "f2a4ed7c5aa338430c7026d7d92017f994ca1c8b123b236dae8666b1899059d0"),
      // blinded_message
      absl::HexStringToBytes(
          "7756a1f89fa33cfc083567e02fd865d07d6e5cd4943f030a2f94b5c23f3fe79c83c4"
          "9c594247d02885e2cd161638cff60803184c9e802a659d76a1c53340972e62e728cc"
          "70cf684ef03ce2d05cefc729e6eee2ae46afa17b6b27a64f91e4c46cc12adc58d9cb"
          "61a4306dac732c9789199cfe8bd28359d1911678e9709bc159dae34ac7aa59fd0c95"
          "962c9f4904bf04aaba8a7e774735bd03be4a02fb0864a53354a2e2f3502506318a5b"
          "03961366005c7b120f0e6b87b44bc15658c3e8985d69f6adea38c24fe5e7b4bafa1a"
          "d6dc7d729281c26dffc88bd34fcc5a5f9df9b9781f99ea47472ba8bd679aaada5952"
          "5b978ebc8a3ea2161de84b7398e4878b751b"),
      // blinded_signature
      absl::HexStringToBytes(
          "2a13f73e4e255a9d5bc6f76cf48dfbf189581c2b170600fd3ab1a3def14884621323"
          "9b9d0a981537541cb4f481a602aeebca9ef28c9fcdc63d15d4296f85d864f799edf0"
          "8e9045180571ce1f1d3beff293b18aae9d8845068cc0d9a05b822295042dc56a1a2b"
          "604c51aa65fd89e6d163fe1eac63cf603774797b7936a8b7494d43fa37039d3777b8"
          "e57cf0d95227ab29d0bd9c01b3eae9dde5fca7141919bd83a17f9b1a3b401507f3e3"
          "a8e8a2c8eb6c5c1921a781000fee65b6dd851d53c89cba2c3375f0900001c0485594"
          "9b7fa499f2a78089a6f0c9b4d36fdfcac2d846076736c5eaedaf0ae70860633e51b0"
          "de21d96c8b43c600afa2e4cc64cd66d77a8f"),
      // signature
      absl::HexStringToBytes(
          "67985949f4e7c91edd5647223170d2a9b6611a191ca48ceadb6c568828b4c415b627"
          "0b037cd8a68b5bca1992eb769aaef04549422889c8b156b9378c50e8a31c07dc1fe0"
          "a80d25b870fadbcc1435197f0a31723740f3084ecb4e762c623546f6bd7d072aa565"
          "bc2105b954244a2b03946c7d4093ba1216ec6bb65b8ca8d2f3f3c43468e80b257c54"
          "a2c2ea15f640a08183a00488c7772b10df87232ee7879bee93d17e194d6b703aeceb"
          "348c1b02ec7ce202086b6494f96a0f2d800f12e855f9c33dcd3abf6bd8044efd69d4"
          "594a974d6297365479fe6c11f6ecc5ea333031c57deb6e14509777963a25cdf8db62"
          "d6c8c68aa038555e4e3ae4411b28e43c8f57"),
  });

  // test_vector 4.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      "",
      // message_mask
      absl::HexStringToBytes(
          "ba3ea4b1e475eebe11d4bfe3a48521d3ba8cd62f3baed9ec29fbbf7ff0478bc0"),
      // blinded_message
      absl::HexStringToBytes(
          "99d725c5613ff87d16464b0375b0976bf4d47319d6946e85f0d0c2ca79eb02a4c0c2"
          "82642e090a910b80fee288f0b3b6777e517b757fc6c96ea44ac570216c8fcd868e15"
          "da4b389b0c70898c5a2ed25c1d13451e4d407fe1301c231b4dc76826b1d4cc5e64b0"
          "e28fb9c71f928ba48c87e308d851dd07fb5a7e0aa5d0dce61d1348afb4233355374e"
          "5898f63adbd5ba215332d3329786fb7c30ef04c181b267562828d8cf1295f2ef4a05"
          "ef1e03ed8fee65efb7725d8c8ae476f61a35987e40efc481bcb4b89cb363addfb2ad"
          "acf690aff5425107d29b2a75b4665d49f255c5caa856cdc0c5667de93dbf3f500db8"
          "fcce246a70a159526729d82c34df69c926a8"),
      // blinded_signature
      absl::HexStringToBytes(
          "a9678acee80b528a836e4784f0690fdddce147e5d4ac506e9ec51c11b16ee2fd5a32"
          "e382a3c3d276a681bb638b63040388d53894afab79249e159835cd6bd65849e5d139"
          "7666f03d1351aaec3eae8d3e7cba3135e7ec4e7b478ef84d79d81039693adc6b130b"
          "0771e3d6f0879723a20b7f72b476fe6fef6f21e00b9e3763a364ed918180f939c351"
          "0bb5f46b35c06a00e51f049ade9e47a8e1c3d5689bd5a43df20b73d70dcacfeed9fa"
          "23cabfbe750779997da6bc7269d08b2620acaa3daa0d9e9d4b87ef841ebcc06a4c0a"
          "f13f1d13f0808f512c50898586b4fc76d2b32858a7ddf715a095b7989d8df50654e3"
          "e05120a83cec275709cf79571d8f46af2b8e"),
      // signature
      absl::HexStringToBytes(
          "ba57951810dbea7652209eb73e3b8edafc56ca7061475a048751cbfb995aeb4ccda2"
          "e9eb309698b7c61012accc4c0414adeeb4b89cd29ba2b49b1cc661d5e7f30caee7a1"
          "2ab36d6b52b5e4d487dbff98eb2d27d552ecd09ca022352c9480ae27e10c3a49a1fd"
          "4912699cc01fba9dbbfd18d1adcec76ca4bc44100ea67b9f1e00748d80255a03371a"
          "7b8f2c160cf632499cea48f99a6c2322978bd29107d0dffdd2e4934bb7dc81c90dd6"
          "3ae744fd8e57bff5e83f98014ca502b6ace876b455d1e3673525ba01687dce998406"
          "e89100f55316147ad510e854a064d99835554de8949d3662708d5f1e43bca473c14a"
          "8b1729846c6092f18fc0e08520e9309a32de"),
  });
  return test_vectors;
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfStandardRsaBlindSignatureTestKeys() {
  IetfStandardRsaBlindSignatureTestVector test_vector =
      GetIetfStandardRsaBlindSignatureTestVector();
  return PopulateTestVectorKeys(test_vector.n, test_vector.e, test_vector.d,
                                test_vector.p, test_vector.q);
}

absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfRsaBlindSignatureWithPublicMetadataTestKeys() {
  auto test_vectors = GetIetfRsaBlindSignatureWithPublicMetadataTestVectors();
  return PopulateTestVectorKeys(test_vectors[0].n, test_vectors[0].e,
                                test_vectors[0].d, test_vectors[0].p,
                                test_vectors[0].q);
}

std::string RandomString(int n, std::uniform_int_distribution<int>* distr_u8,
                         std::mt19937_64* generator) {
  std::string rand(n, 0);
  for (int i = 0; i < n; ++i) {
    rand[i] = static_cast<uint8_t>((*distr_u8)(*generator));
  }
  return rand;
}

}  // namespace anonymous_tokens
}  // namespace private_membership
