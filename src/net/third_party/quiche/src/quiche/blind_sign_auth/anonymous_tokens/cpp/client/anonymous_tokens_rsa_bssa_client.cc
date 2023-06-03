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

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/client/anonymous_tokens_rsa_bssa_client.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/proto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"

namespace private_membership {
namespace anonymous_tokens {

namespace {

absl::Status ValidityChecksForClientCreation(
    const RSABlindSignaturePublicKey& public_key) {
  // Basic validity checks.
  if (!ParseUseCase(public_key.use_case()).ok()) {
    return absl::InvalidArgumentError("Invalid use case for public key.");
  } else if (public_key.key_version() <= 0) {
    return absl::InvalidArgumentError(
        "Key version cannot be zero or negative.");
  } else if (public_key.key_size() < 256) {
    return absl::InvalidArgumentError(
        "Key modulus size cannot be less than 256 bytes.");
  } else if (public_key.mask_gen_function() == AT_TEST_MGF ||
             public_key.mask_gen_function() == AT_MGF_UNDEFINED) {
    return absl::InvalidArgumentError("Unknown or unacceptable mgf1 hash.");
  } else if (public_key.sig_hash_type() == AT_TEST_HASH_TYPE ||
             public_key.sig_hash_type() == AT_HASH_TYPE_UNDEFINED) {
    return absl::InvalidArgumentError(
        "Unknown or unacceptable signature hash.");
  } else if (public_key.salt_length() <= 0) {
    return absl::InvalidArgumentError(
        "Non-positive salt length is not allowed.");
  } else if (public_key.mask_gen_function() == AT_TEST_MGF ||
             public_key.mask_gen_function() == AT_MGF_UNDEFINED) {
    return absl::InvalidArgumentError("Message mask type must be defined.");
  } else if (public_key.message_mask_size() <= 0) {
    return absl::InvalidArgumentError("Message mask size must be positive.");
  }

  RSAPublicKey rsa_public_key;
  if (!rsa_public_key.ParseFromString(public_key.serialized_public_key())) {
    return absl::InvalidArgumentError("Public key is malformed.");
  }
  if (rsa_public_key.n().size() != static_cast<size_t>(public_key.key_size())) {
    return absl::InvalidArgumentError(
        "Public key size does not match key size.");
  }
  return absl::OkStatus();
}

absl::Status CheckPublicKeyValidity(
    const RSABlindSignaturePublicKey& public_key) {
  absl::Time time_now = absl::Now();
  ANON_TOKENS_ASSIGN_OR_RETURN(
      absl::Time start_time,
      TimeFromProto(public_key.key_validity_start_time()));
  if (start_time > time_now) {
    return absl::FailedPreconditionError("Key is not valid yet.");
  }
  if (public_key.has_expiration_time()) {
    ANON_TOKENS_ASSIGN_OR_RETURN(absl::Time expiration_time,
                                 TimeFromProto(public_key.expiration_time()));
    if (expiration_time <= time_now) {
      return absl::FailedPreconditionError("Key is already expired.");
    }
  }
  return absl::OkStatus();
}

}  // namespace

AnonymousTokensRsaBssaClient::AnonymousTokensRsaBssaClient(
    const RSABlindSignaturePublicKey& public_key)
    : public_key_(public_key) {}

absl::StatusOr<std::unique_ptr<AnonymousTokensRsaBssaClient>>
AnonymousTokensRsaBssaClient::Create(
    const RSABlindSignaturePublicKey& public_key) {
  ANON_TOKENS_RETURN_IF_ERROR(ValidityChecksForClientCreation(public_key));
  return absl::WrapUnique(new AnonymousTokensRsaBssaClient(public_key));
}

// TODO(b/261866075): Offer an API to simply return bytes of blinded requests.
absl::StatusOr<AnonymousTokensSignRequest>
AnonymousTokensRsaBssaClient::CreateRequest(
    const std::vector<PlaintextMessageWithPublicMetadata>& inputs) {
  if (inputs.empty()) {
    return absl::InvalidArgumentError("Cannot create an empty request.");
  } else if (!blinding_info_map_.empty()) {
    return absl::FailedPreconditionError(
        "Blind signature request already created.");
  }

  ANON_TOKENS_RETURN_IF_ERROR(CheckPublicKeyValidity(public_key_));

  AnonymousTokensSignRequest request;
  for (const PlaintextMessageWithPublicMetadata& input : inputs) {
    // Generate nonce and masked message. For more details, see
    // https://datatracker.ietf.org/doc/draft-irtf-cfrg-rsa-blind-signatures/
    ANON_TOKENS_ASSIGN_OR_RETURN(std::string mask, GenerateMask(public_key_));
    std::string masked_message =
        MaskMessageConcat(mask, input.plaintext_message());

    std::optional<std::string> public_metadata = std::nullopt;
    if (public_key_.public_metadata_support()) {
      // Empty public metadata is a valid value.
      public_metadata = input.public_metadata();
    }
    // Generate RSA blinder.
    ANON_TOKENS_ASSIGN_OR_RETURN(auto rsa_bssa_blinder,
                                 RsaBlinder::New(public_key_, public_metadata));
    ANON_TOKENS_ASSIGN_OR_RETURN(const std::string blinded_message,
                                 rsa_bssa_blinder->Blind(masked_message));

    // Store randomness needed to unblind.
    BlindingInfo blinding_info = {
        input,
        mask,
        std::move(rsa_bssa_blinder),
    };

    // Create the blinded token.
    AnonymousTokensSignRequest_BlindedToken* blinded_token =
        request.add_blinded_tokens();
    blinded_token->set_use_case(public_key_.use_case());
    blinded_token->set_key_version(public_key_.key_version());
    blinded_token->set_serialized_token(blinded_message);
    blinded_token->set_public_metadata(input.public_metadata());
    blinding_info_map_[blinded_message] = std::move(blinding_info);
  }

  return request;
}

absl::StatusOr<std::vector<RSABlindSignatureTokenWithInput>>
AnonymousTokensRsaBssaClient::ProcessResponse(
    const AnonymousTokensSignResponse& response) {
  if (blinding_info_map_.empty()) {
    return absl::FailedPreconditionError(
        "A valid Blind signature request was not created before calling "
        "RetrieveAnonymousTokensFromSignResponse.");
  } else if (response.anonymous_tokens().empty()) {
    return absl::InvalidArgumentError("Cannot process an empty response.");
  } else if (static_cast<size_t>(response.anonymous_tokens().size()) !=
             blinding_info_map_.size()) {
    return absl::InvalidArgumentError(
        "Response is missing some requested tokens.");
  }

  // Vector to accumulate output tokens.
  std::vector<RSABlindSignatureTokenWithInput> tokens;

  // Temporary set structure to check for duplicate responses.
  absl::flat_hash_set<absl::string_view> blinded_messages;

  // Loop over all the anonymous tokens in the response.
  for (const AnonymousTokensSignResponse_AnonymousToken& anonymous_token :
       response.anonymous_tokens()) {
    // Basic validity checks on the response.
    if (anonymous_token.use_case() != public_key_.use_case()) {
      return absl::InvalidArgumentError("Use case does not match public key.");
    } else if (anonymous_token.key_version() != public_key_.key_version()) {
      return absl::InvalidArgumentError(
          "Key version does not match public key.");
    } else if (anonymous_token.serialized_blinded_message().empty()) {
      return absl::InvalidArgumentError(
          "Blinded message that was sent in request cannot be empty in "
          "response.");
    } else if (anonymous_token.serialized_token().empty()) {
      return absl::InvalidArgumentError(
          "Blinded anonymous token (serialized_token) in response cannot be "
          "empty.");
    }

    // Check for duplicate in responses.
    if (!blinded_messages.insert(anonymous_token.serialized_blinded_message())
             .second) {
      return absl::InvalidArgumentError(
          "Blinded message was repeated in the response.");
    }

    // Retrieve blinding info associated with blind response.
    auto it =
        blinding_info_map_.find(anonymous_token.serialized_blinded_message());
    if (it == blinding_info_map_.end()) {
      return absl::InvalidArgumentError(
          "Response has some tokens for some blinded messages that were not "
          "requested.");
    }
    const BlindingInfo& blinding_info = it->second;

    if (blinding_info.input.public_metadata() !=
        anonymous_token.public_metadata()) {
      return absl::InvalidArgumentError(
          "Response public metadata does not match input.");
    }

    // Unblind the blinded anonymous token to obtain the final anonymous token
    // (signature).
    ANON_TOKENS_ASSIGN_OR_RETURN(
        const std::string final_anonymous_token,
        blinding_info.rsa_blinder->Unblind(anonymous_token.serialized_token()));

    // Verify the signature for correctness.
    ANON_TOKENS_RETURN_IF_ERROR(blinding_info.rsa_blinder->Verify(
        final_anonymous_token,
        MaskMessageConcat(blinding_info.mask,
                          blinding_info.input.plaintext_message())));

    // Construct the final signature proto.
    RSABlindSignatureTokenWithInput final_token_proto;
    *final_token_proto.mutable_token()->mutable_token() = final_anonymous_token;
    *final_token_proto.mutable_token()->mutable_message_mask() =
        blinding_info.mask;
    *final_token_proto.mutable_input() = blinding_info.input;

    tokens.push_back(final_token_proto);
  }

  return tokens;
}

absl::Status AnonymousTokensRsaBssaClient::Verify(
    const RSABlindSignaturePublicKey& /*public_key*/,
    const RSABlindSignatureToken& /*token*/,
    const PlaintextMessageWithPublicMetadata& /*input*/) {
  return absl::UnimplementedError("Verify not implemented yet.");
}

}  // namespace anonymous_tokens
}  // namespace private_membership
