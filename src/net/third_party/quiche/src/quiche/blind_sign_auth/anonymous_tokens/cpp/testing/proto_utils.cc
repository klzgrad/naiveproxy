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
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/testing/proto_utils.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/testing/utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "openssl/base.h"
#include "openssl/bn.h"
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

}  // namespace anonymous_tokens
}  // namespace private_membership
