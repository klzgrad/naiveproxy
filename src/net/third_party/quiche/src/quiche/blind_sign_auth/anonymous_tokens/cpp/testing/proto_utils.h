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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_PROTO_UTILS_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_PROTO_UTILS_H_

#include <string>
#include <utility>
#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/base.h"

namespace private_membership {
namespace anonymous_tokens {

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

// This method returns a RSA key pair as described in the IETF test example
// above.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfStandardRsaBlindSignatureTestKeys();

// This method returns a RSA key pair as described in the IETF test with Public
// Metadata example. It can be used for all test vectors returned by
// GetIetfRsaBlindSignatureWithPublicMetadataTestVectors.
absl::StatusOr<std::pair<RSAPublicKey, RSAPrivateKey>>
GetIetfRsaBlindSignatureWithPublicMetadataTestKeys();

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_TESTING_PROTO_UTILS_H_
