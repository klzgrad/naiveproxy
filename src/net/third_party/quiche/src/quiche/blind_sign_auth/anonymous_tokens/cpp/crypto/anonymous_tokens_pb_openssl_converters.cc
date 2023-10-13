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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/anonymous_tokens_pb_openssl_converters.h"

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "openssl/base.h"
#include "openssl/digest.h"
#include "openssl/rand.h"

namespace private_membership {
namespace anonymous_tokens {

absl::StatusOr<std::string> GenerateMask(
    const RSABlindSignaturePublicKey& public_key) {
  std::string mask;
  if (public_key.message_mask_type() == AT_MESSAGE_MASK_CONCAT &&
      public_key.message_mask_size() >= kRsaMessageMaskSizeInBytes32) {
    mask = std::string(public_key.message_mask_size(), '\0');
    RAND_bytes(reinterpret_cast<uint8_t*>(mask.data()), mask.size());
  } else if (public_key.message_mask_type() == AT_MESSAGE_MASK_NO_MASK &&
             public_key.message_mask_size() == 0) {
    return "";
  } else {
    return absl::InvalidArgumentError(
        "Unsupported message mask type Or invalid message mask size "
        "requested.");
  }
  return mask;
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

absl::StatusOr<bssl::UniquePtr<RSA>> AnonymousTokensRSAPrivateKeyToRSA(
    const RSAPrivateKey& private_key) {
  return CreatePrivateKeyRSA(private_key.n(), private_key.e(), private_key.d(),
                             private_key.p(), private_key.q(), private_key.dp(),
                             private_key.dq(), private_key.crt());
}

absl::StatusOr<bssl::UniquePtr<RSA>> AnonymousTokensRSAPublicKeyToRSA(
    const RSAPublicKey& public_key) {
  return CreatePublicKeyRSA(public_key.n(), public_key.e());
}

}  // namespace anonymous_tokens
}  // namespace private_membership
