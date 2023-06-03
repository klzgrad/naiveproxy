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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CLIENT_ANONYMOUS_TOKENS_RSA_BSSA_CLIENT_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CLIENT_ANONYMOUS_TOKENS_RSA_BSSA_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/rsa_blinder.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/common/platform/api/quiche_export.h"
// copybara:strip_begin(internal comment)
// The QUICHE_EXPORT annotation is necessary for some classes and functions
// to link correctly on Windows. Please do not remove them!
// copybara:strip_end

namespace private_membership {
namespace anonymous_tokens {

// This class generates AnonymousTokens RSA blind signatures,
// (https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/)
// blind message signing request and processes the response.
//
// Each execution of the Anonymous Tokens RSA blind signatures protocol requires
// a new instance of the AnonymousTokensRsaBssaClient.
//
// This class is not thread-safe.
class QUICHE_EXPORT AnonymousTokensRsaBssaClient {
 public:
  // AnonymousTokensRsaBssaClient is neither copyable nor copy assignable.
  AnonymousTokensRsaBssaClient(const AnonymousTokensRsaBssaClient&) = delete;
  AnonymousTokensRsaBssaClient& operator=(const AnonymousTokensRsaBssaClient&) =
      delete;

  // Create client with the specified public key which can be used to send a
  // sign request and process a response.
  //
  // This method is to be used to create a client as its constructor is private.
  // It takes as input RSABlindSignaturePublicKey which contains the public key
  // and relevant parameters.
  static absl::StatusOr<std::unique_ptr<AnonymousTokensRsaBssaClient>> Create(
      const RSABlindSignaturePublicKey& public_key);

  // Class method that creates the signature requests by taking a vector where
  // each element in the vector is the plaintext message along with its
  // respective public metadata (if the metadata exists).
  //
  // The library will also fail if the key has expired.
  //
  // It only puts the blinded version of the messages in the request.
  absl::StatusOr<AnonymousTokensSignRequest> CreateRequest(
      const std::vector<PlaintextMessageWithPublicMetadata>& inputs);

  // Class method that processes the signature response from the server.
  //
  // It outputs a vector of a protos where each element contains an input
  // plaintext message and associated public metadata (if it exists) along with
  // its final (unblinded) anonymous token resulting from the RSA blind
  // signatures protocol.
  absl::StatusOr<std::vector<RSABlindSignatureTokenWithInput>> ProcessResponse(
      const AnonymousTokensSignResponse& response);

  // Method to verify whether an anonymous token is valid or not.
  //
  // Returns OK on a valid token and non-OK otherwise.
  absl::Status Verify(const RSABlindSignaturePublicKey& public_key,
                      const RSABlindSignatureToken& token,
                      const PlaintextMessageWithPublicMetadata& input);

 private:
  struct BlindingInfo {
    PlaintextMessageWithPublicMetadata input;
    std::string mask;
    std::unique_ptr<RsaBlinder> rsa_blinder;
  };

  explicit AnonymousTokensRsaBssaClient(
      const RSABlindSignaturePublicKey& public_key);

  const RSABlindSignaturePublicKey public_key_;
  absl::flat_hash_map<std::string, BlindingInfo> blinding_info_map_;
};

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CLIENT_ANONYMOUS_TOKENS_RSA_BSSA_CLIENT_H_
