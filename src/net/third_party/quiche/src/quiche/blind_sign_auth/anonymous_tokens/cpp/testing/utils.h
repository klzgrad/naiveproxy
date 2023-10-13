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

#include <random>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"

namespace private_membership {
namespace anonymous_tokens {

struct TestRsaPublicKey {
  std::string n;
  std::string e;
};

struct TestRsaPrivateKey {
  std::string n;
  std::string e;
  std::string d;
  std::string p;
  std::string q;
  std::string dp;
  std::string dq;
  std::string crt;
};

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

// TestSign can be removed once rsa_blind_signer is moved to
// anonympous_tokens/public/cpp/crypto
absl::StatusOr<std::string> TestSign(absl::string_view blinded_data,
                                     RSA* rsa_key);

// TestSignWithPublicMetadata can be removed once rsa_blind_signer is moved to
// anonympous_tokens/public/cpp/crypto
absl::StatusOr<std::string> TestSignWithPublicMetadata(
    absl::string_view blinded_data, absl::string_view public_metadata,
    const RSA& rsa_key, bool use_rsa_public_exponent);

// Returns the IETF test example from
// https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/
IetfStandardRsaBlindSignatureTestVector
GetIetfStandardRsaBlindSignatureTestVector();

// Returns the IETF test with Public Metadata examples from
// https://datatracker.ietf.org/doc/draft-amjad-cfrg-partially-blind-rsa/
//
// Note that all test vectors use the same RSA key pair.
std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfRsaBlindSignatureWithPublicMetadataTestVectors();

// Returns the IETF test with Public Metadata examples that disregard the RSA
// public exponent during partially blind RSA signatures protocol execution.
//
// Note that all test vectors use the same RSA key pair.
std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfPartiallyBlindRSASignatureNoPublicExponentTestVectors();

// Method returns fixed 2048-bit strong RSA modulus based key pair for testing.
std::pair<TestRsaPublicKey, TestRsaPrivateKey> GetStrongTestRsaKeyPair2048();

// Method returns another fixed 2048-bit strong RSA modulus based key pair for
// testing.
std::pair<TestRsaPublicKey, TestRsaPrivateKey>
GetAnotherStrongTestRsaKeyPair2048();

// Method returns fixed 3072-bit strong RSA modulus based key pair for testing.
std::pair<TestRsaPublicKey, TestRsaPrivateKey> GetStrongTestRsaKeyPair3072();

// Method returns fixed 4096-bit strong RSA modulus based key pair for testing.
std::pair<TestRsaPublicKey, TestRsaPrivateKey> GetStrongTestRsaKeyPair4096();

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
