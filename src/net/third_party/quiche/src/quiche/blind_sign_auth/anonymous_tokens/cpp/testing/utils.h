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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_UTILS_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_UTILS_H_

#include <stdint.h>

#include <random>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/base.h"

namespace private_membership {
namespace anonymous_tokens {

struct IetfStandardRsaBlindSignatureTestVector {
  std::string n;
  std::string e;
  std::string d;
  std::string p;
  std::string q;
  std::string message;
  std::string salt;
  std::string inv;
  std::string encoded_message;
  std::string blinded_message;
  std::string blinded_signature;
  std::string signature;
};

struct IetfRsaBlindSignatureWithPublicMetadataTestVector {
  std::string n;
  std::string e;
  std::string d;
  std::string p;
  std::string q;
  std::string message;
  std::string public_metadata;
  std::string message_mask;
  std::string blinded_message;
  std::string blinded_signature;
  std::string signature;
};

// Creates a pair containing a standard RSA Private key and an Anonymous Tokens
// RSABlindSignaturePublicKey using RSA_F4 (65537) as the public exponent and
// other input parameters.
absl::StatusOr<std::pair<bssl::UniquePtr<RSA>, RSABlindSignaturePublicKey>>
CreateTestKey(int key_size = 512, HashType sig_hash = AT_HASH_TYPE_SHA384,
              MaskGenFunction mfg1_hash = AT_MGF_SHA384, int salt_length = 48,
              MessageMaskType message_mask_type = AT_MESSAGE_MASK_CONCAT,
              int message_mask_size = kRsaMessageMaskSizeInBytes32);

// Prepares message for signing by computing its hash and then applying the PSS
// padding to the result by executing RSA_padding_add_PKCS1_PSS_mgf1 from the
// openssl library, using the input parameters.
//
// This is a test function and it skips the message blinding part.
absl::StatusOr<std::string> EncodeMessageForTests(absl::string_view message,
                                                  RSAPublicKey public_key,
                                                  const EVP_MD* sig_hasher,
                                                  const EVP_MD* mgf1_hasher,
                                                  int32_t salt_length);

// TestSign can be removed once rsa_blind_signer is moved to
// anonympous_tokens/public/cpp/crypto
absl::StatusOr<std::string> TestSign(absl::string_view blinded_data,
                                     RSA* rsa_key);

// TestSignWithPublicMetadata can be removed once rsa_blind_signer is moved to
// anonympous_tokens/public/cpp/crypto
absl::StatusOr<std::string> TestSignWithPublicMetadata(
    absl::string_view blinded_data, absl::string_view public_metadata,
    const RSA& rsa_key);

// This method returns a newly generated RSA key pair, setting the public
// exponent to be the standard RSA_F4 (65537) and the default modulus size to
// 512 bytes.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStandardRsaKeyPair(
    int modulus_size_in_bytes = kRsaModulusSizeInBytes512);

// Method returns fixed 2048-bit strong RSA modulus for testing.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys2048();

// Method returns another fixed 2048-bit strong RSA modulus for testing.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetAnotherStrongRsaKeys2048();

// Method returns fixed 3072-bit strong RSA modulus for testing.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys3072();

// Method returns fixed 4096-bit strong RSA modulus for testing.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>> GetStrongRsaKeys4096();

// Returns the IETF test example from
// https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/
IetfStandardRsaBlindSignatureTestVector
GetIetfStandardRsaBlindSignatureTestVector();

// This method returns a RSA key pair as described in the IETF test example
// above.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfStandardRsaBlindSignatureTestKeys();

// Returns the IETF test with Public Metadata examples from
// https://datatracker.ietf.org/doc/draft-amjad-cfrg-partially-blind-rsa/
//
// Note that all test vectors use the same RSA key pair.
std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfRsaBlindSignatureWithPublicMetadataTestVectors();

// This method returns a RSA key pair as described in the IETF test with Public
// Metadata example. It can be used for all test vectors returned by
// GetIetfRsaBlindSignatureWithPublicMetadataTestVectors.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfRsaBlindSignatureWithPublicMetadataTestKeys();

// Outputs a random string of n characters.
std::string RandomString(int n, std::uniform_int_distribution<int>* distr_u8,
                         std::mt19937_64* generator);

#define ANON_TOKENS_ASSERT_OK_AND_ASSIGN(lhs, rexpr)                       \
  ANON_TOKENS_ASSERT_OK_AND_ASSIGN_IMPL_(                                  \
      ANON_TOKENS_STATUS_TESTING_IMPL_CONCAT_(_status_or_value, __LINE__), \
      lhs, rexpr)

#define ANON_TOKENS_ASSERT_OK_AND_ASSIGN_IMPL_(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                           \
  ASSERT_THAT(statusor.ok(), ::testing::Eq(true));                   \
  lhs = std::move(statusor).value()

#define ANON_TOKENS_STATUS_TESTING_IMPL_CONCAT_INNER_(x, y) x##y
#define ANON_TOKENS_STATUS_TESTING_IMPL_CONCAT_(x, y) \
  ANON_TOKENS_STATUS_TESTING_IMPL_CONCAT_INNER_(x, y)

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_UTILS_H_
