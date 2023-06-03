// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/blind_sign_auth.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "quiche/blind_sign_auth/proto/auth_and_sign.pb.h"
#include "quiche/blind_sign_auth/proto/get_initial_data.pb.h"
#include "quiche/blind_sign_auth/proto/key_services.pb.h"
#include "quiche/blind_sign_auth/proto/public_metadata.pb.h"
#include "quiche/blind_sign_auth/proto/spend_token_data.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/proto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_random.h"

namespace quiche {
namespace {

template <typename T>
std::string OmitDefault(T value) {
  return value == 0 ? "" : absl::StrCat(value);
}

}  // namespace

void BlindSignAuth::GetTokens(
    absl::string_view oauth_token, int num_tokens,
    std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
        callback) {
  // Create GetInitialData RPC.
  privacy::ppn::GetInitialDataRequest request;
  request.set_use_attestation(false);
  request.set_service_type("chromeipblinding");
  request.set_location_granularity(
      privacy::ppn::GetInitialDataRequest_LocationGranularity_CITY_GEOS);

  // Call GetInitialData on the HttpFetcher.
  std::string path_and_query = "/v1/getInitialData";
  std::string body = request.SerializeAsString();
  http_fetcher_->DoRequest(
      path_and_query, oauth_token.data(), body,
      [this, callback, oauth_token,
       num_tokens](absl::StatusOr<BlindSignHttpResponse> response) {
        GetInitialDataCallback(response, oauth_token, num_tokens, callback);
      });
}

void BlindSignAuth::GetInitialDataCallback(
    absl::StatusOr<BlindSignHttpResponse> response,
    absl::string_view oauth_token, int num_tokens,
    std::function<void(absl::StatusOr<absl::Span<std::string>>)> callback) {
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed: "
                        << response.status();
    callback(response.status());
    return;
  }
  int status_code = response.value().status_code();
  if (response.value().status_code() != 200) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed with code: "
                        << status_code;
    callback(response.status());
    return;
  }
  // Parse GetInitialDataResponse.
  privacy::ppn::GetInitialDataResponse initial_data_response;
  if (!initial_data_response.ParseFromString(response.value().body())) {
    QUICHE_LOG(WARNING) << "Failed to parse GetInitialDataResponse";
    callback(absl::InternalError("Failed to parse GetInitialDataResponse"));
    return;
  }

  // Create RSA BSSA client.
  auto bssa_client =
      private_membership::anonymous_tokens::AnonymousTokensRsaBssaClient::
          Create(initial_data_response.at_public_metadata_public_key());
  if (!bssa_client.ok()) {
    QUICHE_LOG(WARNING) << "Failed to create AT BSSA client: "
                        << bssa_client.status();
    callback(bssa_client.status());
    return;
  }

  // Create plaintext tokens.
  // Client blinds plaintext tokens (random 32-byte strings) in CreateRequest.
  std::vector<
      private_membership::anonymous_tokens::PlaintextMessageWithPublicMetadata>
      plaintext_tokens;
  QuicheRandom* random = QuicheRandom::GetInstance();
  for (int i = 0; i < num_tokens; i++) {
    // Create random 32-byte string prefixed with "blind:".
    private_membership::anonymous_tokens::PlaintextMessageWithPublicMetadata
        plaintext_message;
    std::string rand_bytes(32, '\0');
    random->RandBytes(rand_bytes.data(), rand_bytes.size());
    plaintext_message.set_plaintext_message(absl::StrCat("blind:", rand_bytes));
    uint64_t fingerprint = 0;
    absl::Status fingerprint_status = FingerprintPublicMetadata(
        initial_data_response.public_metadata_info().public_metadata(),
        &fingerprint);
    if (!fingerprint_status.ok()) {
      QUICHE_LOG(WARNING) << "Failed to fingerprint public metadata: "
                          << fingerprint_status;
      callback(fingerprint_status);
      return;
    }
    plaintext_message.set_public_metadata(absl::StrCat(fingerprint));
    plaintext_tokens.push_back(plaintext_message);
  }

  absl::StatusOr<
      private_membership::anonymous_tokens::AnonymousTokensSignRequest>
      at_sign_request = bssa_client.value()->CreateRequest(plaintext_tokens);
  if (!at_sign_request.ok()) {
    QUICHE_LOG(WARNING) << "Failed to create AT Sign Request: "
                        << at_sign_request.status();
    callback(at_sign_request.status());
    return;
  }

  // Create AuthAndSign RPC.
  privacy::ppn::AuthAndSignRequest sign_request;
  sign_request.set_oauth_token(std::string(oauth_token));
  sign_request.set_service_type("chromeipblinding");
  sign_request.set_key_type(privacy::ppn::AT_PUBLIC_METADATA_KEY_TYPE);
  sign_request.set_key_version(
      initial_data_response.at_public_metadata_public_key().key_version());
  *sign_request.mutable_public_metadata_info() =
      initial_data_response.public_metadata_info();
  for (int i = 0; i < at_sign_request->blinded_tokens_size(); i++) {
    sign_request.add_blinded_token(absl::Base64Escape(
        at_sign_request->blinded_tokens().at(i).serialized_token()));
  }

  privacy::ppn::PublicMetadataInfo public_metadata_info =
      initial_data_response.public_metadata_info();
  http_fetcher_->DoRequest(
      "/v1/authWithHeaderCreds", oauth_token.data(),
      sign_request.SerializeAsString(),
      [this, at_sign_request, public_metadata_info,
       bssa_client_ = bssa_client.value().get(),
       callback](absl::StatusOr<BlindSignHttpResponse> response) {
        AuthAndSignCallback(response, public_metadata_info, *at_sign_request,
                            bssa_client_, callback);
      });
}

void BlindSignAuth::AuthAndSignCallback(
    absl::StatusOr<BlindSignHttpResponse> response,
    privacy::ppn::PublicMetadataInfo public_metadata_info,
    private_membership::anonymous_tokens::AnonymousTokensSignRequest
        at_sign_request,
    private_membership::anonymous_tokens::AnonymousTokensRsaBssaClient*
        bssa_client,
    std::function<void(absl::StatusOr<absl::Span<std::string>>)> callback) {
  // Validate response.
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign failed: " << response.status();
    callback(response.status());
    return;
  }
  int status_code = response.value().status_code();
  if (response.value().status_code() != 200) {
    QUICHE_LOG(WARNING) << "AuthAndSign failed with code: " << status_code;
    callback(response.status());
    return;
  }

  // Decode AuthAndSignResponse.
  privacy::ppn::AuthAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response.value().body())) {
    QUICHE_LOG(WARNING) << "Failed to parse AuthAndSignResponse";
    callback(absl::InternalError("Failed to parse AuthAndSignResponse"));
    return;
  }

  // Create vector of unblinded anonymous tokens.
  private_membership::anonymous_tokens::AnonymousTokensSignResponse
      at_sign_response;

  if (sign_response.blinded_token_signature_size() !=
      at_sign_request.blinded_tokens_size()) {
    QUICHE_LOG(WARNING)
        << "Response signature size does not equal request tokens size";
    callback(absl::InternalError(
        "Response signature size does not equal request tokens size"));
    return;
  }
  // This depends on the signing server returning the signatures in the order
  // that the tokens were sent. Phosphor does guarantee this.
  for (int i = 0; i < sign_response.blinded_token_signature_size(); i++) {
    std::string blinded_token;
    if (!absl::Base64Unescape(sign_response.blinded_token_signature(i),
                              &blinded_token)) {
      QUICHE_LOG(WARNING) << "Failed to unescape blinded token signature";
      callback(
          absl::InternalError("Failed to unescape blinded token signature"));
      return;
    }
    private_membership::anonymous_tokens::AnonymousTokensSignResponse::
        AnonymousToken anon_token_proto;
    *anon_token_proto.mutable_use_case() =
        at_sign_request.blinded_tokens(i).use_case();
    anon_token_proto.set_key_version(
        at_sign_request.blinded_tokens(i).key_version());
    *anon_token_proto.mutable_public_metadata() =
        at_sign_request.blinded_tokens(i).public_metadata();
    *anon_token_proto.mutable_serialized_blinded_message() =
        at_sign_request.blinded_tokens(i).serialized_token();
    *anon_token_proto.mutable_serialized_token() = blinded_token;
    at_sign_response.add_anonymous_tokens()->Swap(&anon_token_proto);
  }

  auto signed_tokens = bssa_client->ProcessResponse(at_sign_response);
  if (!signed_tokens.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign ProcessResponse failed: "
                        << signed_tokens.status();
    callback(signed_tokens.status());
    return;
  }
  if (signed_tokens->size() !=
      static_cast<size_t>(at_sign_response.anonymous_tokens_size())) {
    QUICHE_LOG(WARNING)
        << "ProcessResponse did not output the right number of signed tokens";
    callback(absl::InternalError(
        "ProcessResponse did not output the right number of signed tokens"));
    return;
  }

  // Output SpendTokenData with data for the redeemer to make a SpendToken RPC.
  std::vector<std::string> tokens_vec;
  for (size_t i = 0; i < signed_tokens->size(); i++) {
    privacy::ppn::SpendTokenData spend_token_data;
    *spend_token_data.mutable_public_metadata() =
        public_metadata_info.public_metadata();
    *spend_token_data.mutable_unblinded_token() =
        signed_tokens->at(i).input().plaintext_message();
    *spend_token_data.mutable_unblinded_token_signature() =
        signed_tokens->at(i).token().token();
    spend_token_data.set_signing_key_version(
        at_sign_response.anonymous_tokens(i).key_version());
    auto use_case = private_membership::anonymous_tokens::ParseUseCase(
        at_sign_response.anonymous_tokens(i).use_case());
    if (!use_case.ok()) {
      QUICHE_LOG(WARNING) << "Failed to parse use case: " << use_case.status();
      callback(use_case.status());
      return;
    }
    spend_token_data.set_use_case(*use_case);
    spend_token_data.set_message_mask(
        signed_tokens->at(i).token().message_mask());
    tokens_vec.push_back(spend_token_data.SerializeAsString());
  }

  callback(absl::Span<std::string>(tokens_vec));
}

absl::Status BlindSignAuth::FingerprintPublicMetadata(
    const privacy::ppn::PublicMetadata& metadata, uint64_t* fingerprint) {
  const EVP_MD* hasher = EVP_sha256();
  std::string digest;
  digest.resize(EVP_MAX_MD_SIZE);

  uint32_t digest_length = 0;
  // Concatenate fields in tag number order, omitting fields whose values match
  // the default. This enables new fields to be added without changing the
  // resulting encoding. The signer needs to ensure that | is not allowed in any
  // metadata value so intentional collisions cannot be created.
  const std::vector<std::string> parts = {
      metadata.exit_location().country(),
      metadata.exit_location().city_geo_id(),
      metadata.service_type(),
      OmitDefault(metadata.expiration().seconds()),
      OmitDefault(metadata.expiration().nanos()),
  };
  const std::string input = absl::StrJoin(parts, "|");
  if (EVP_Digest(input.data(), input.length(),
                 reinterpret_cast<uint8_t*>(&digest[0]), &digest_length, hasher,
                 nullptr) != 1) {
    return absl::InternalError("EVP_Digest failed");
  }
  // Return the first uint64_t of the SHA-256 hash.
  memcpy(fingerprint, digest.data(), sizeof(*fingerprint));
  return absl::OkStatus();
}

}  // namespace quiche
