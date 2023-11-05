// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/blind_sign_auth.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "anonymous_tokens/cpp/shared/proto_utils.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/common/quiche_random.h"

namespace quiche {
namespace {

template <typename T>
std::string OmitDefault(T value) {
  return value == 0 ? "" : absl::StrCat(value);
}

}  // namespace

void BlindSignAuth::GetTokens(std::string oauth_token, int num_tokens,
                              SignedTokenCallback callback) {
  // Check whether Privacy Pass crypto is enabled.
  if (auth_options_.enable_privacy_pass()) {
    std::move(callback)(
        absl::UnimplementedError("Privacy Pass is not supported."));
    return;
  }
  // Create GetInitialData RPC.
  privacy::ppn::GetInitialDataRequest request;
  request.set_use_attestation(false);
  request.set_service_type("chromeipblinding");
  request.set_location_granularity(
      privacy::ppn::GetInitialDataRequest_LocationGranularity_CITY_GEOS);

  // Call GetInitialData on the HttpFetcher.
  std::string body = request.SerializeAsString();
  BlindSignHttpCallback initial_data_callback =
      absl::bind_front(&BlindSignAuth::GetInitialDataCallback, this,
                       oauth_token, num_tokens, std::move(callback));
  http_fetcher_->DoRequest(BlindSignHttpRequestType::kGetInitialData,
                           oauth_token, body, std::move(initial_data_callback));
}

void BlindSignAuth::GetInitialDataCallback(
    std::string oauth_token, int num_tokens, SignedTokenCallback callback,
    absl::StatusOr<BlindSignHttpResponse> response) {
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed: "
                        << response.status();
    std::move(callback)(response.status());
    return;
  }
  absl::StatusCode code = HttpCodeToStatusCode(response.value().status_code());
  if (code != absl::StatusCode::kOk) {
    std::string message =
        absl::StrCat("GetInitialDataRequest failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::Status(code, message));
    return;
  }
  // Parse GetInitialDataResponse.
  privacy::ppn::GetInitialDataResponse initial_data_response;
  if (!initial_data_response.ParseFromString(response.value().body())) {
    QUICHE_LOG(WARNING) << "Failed to parse GetInitialDataResponse";
    std::move(callback)(
        absl::InternalError("Failed to parse GetInitialDataResponse"));
    return;
  }
  absl::StatusOr<absl::Time> public_metadata_expiry_time =
      anonymous_tokens::TimeFromProto(
          initial_data_response.public_metadata_info()
              .public_metadata()
              .expiration());
  if (!public_metadata_expiry_time.ok()) {
    std::move(callback)(
        absl::InternalError("Failed to parse public metadata expiration time"));
    return;
  }

  // Create RSA BSSA client.
  auto bssa_client =
      anonymous_tokens::AnonymousTokensRsaBssaClient::
          Create(initial_data_response.at_public_metadata_public_key());
  if (!bssa_client.ok()) {
    QUICHE_LOG(WARNING) << "Failed to create AT BSSA client: "
                        << bssa_client.status();
    std::move(callback)(bssa_client.status());
    return;
  }

  // Create plaintext tokens.
  // Client blinds plaintext tokens (random 32-byte strings) in CreateRequest.
  std::vector<
      anonymous_tokens::PlaintextMessageWithPublicMetadata>
      plaintext_tokens;
  QuicheRandom* random = QuicheRandom::GetInstance();
  for (int i = 0; i < num_tokens; i++) {
    // Create random 32-byte string prefixed with "blind:".
    anonymous_tokens::PlaintextMessageWithPublicMetadata
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
      std::move(callback)(fingerprint_status);
      return;
    }
    uint64_t fingerprint_big_endian = QuicheEndian::HostToNet64(fingerprint);
    std::string key;
    key.resize(sizeof(fingerprint_big_endian));
    memcpy(key.data(), &fingerprint_big_endian, sizeof(fingerprint_big_endian));
    plaintext_message.set_public_metadata(key);
    plaintext_tokens.push_back(plaintext_message);
  }

  absl::StatusOr<
      anonymous_tokens::AnonymousTokensSignRequest>
      at_sign_request = bssa_client.value()->CreateRequest(plaintext_tokens);
  if (!at_sign_request.ok()) {
    QUICHE_LOG(WARNING) << "Failed to create AT Sign Request: "
                        << at_sign_request.status();
    std::move(callback)(at_sign_request.status());
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
  // TODO(b/295924807): deprecate this option after AT server defaults to it
  sign_request.set_do_not_use_rsa_public_exponent(true);

  privacy::ppn::PublicMetadataInfo public_metadata_info =
      initial_data_response.public_metadata_info();
  BlindSignHttpCallback auth_and_sign_callback = absl::bind_front(
      &BlindSignAuth::AuthAndSignCallback, this, public_metadata_info,
      public_metadata_expiry_time.value(), *at_sign_request,
      *std::move(bssa_client), std::move(callback));
  http_fetcher_->DoRequest(BlindSignHttpRequestType::kAuthAndSign,
                           oauth_token.data(), sign_request.SerializeAsString(),
                           std::move(auth_and_sign_callback));
}

void BlindSignAuth::AuthAndSignCallback(
    privacy::ppn::PublicMetadataInfo public_metadata_info,
    absl::Time public_key_expiry_time,
    anonymous_tokens::AnonymousTokensSignRequest
        at_sign_request,
    std::unique_ptr<
        anonymous_tokens::AnonymousTokensRsaBssaClient>
        bssa_client,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignHttpResponse> response) {
  // Validate response.
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign failed: " << response.status();
    std::move(callback)(response.status());
    return;
  }
  absl::StatusCode code = HttpCodeToStatusCode(response.value().status_code());
  if (code != absl::StatusCode::kOk) {
    std::string message = absl::StrCat("AuthAndSign failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::Status(code, message));
    return;
  }

  // Decode AuthAndSignResponse.
  privacy::ppn::AuthAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response.value().body())) {
    QUICHE_LOG(WARNING) << "Failed to parse AuthAndSignResponse";
    std::move(callback)(
        absl::InternalError("Failed to parse AuthAndSignResponse"));
    return;
  }

  // Create vector of unblinded anonymous tokens.
  anonymous_tokens::AnonymousTokensSignResponse
      at_sign_response;

  if (sign_response.blinded_token_signature_size() !=
      at_sign_request.blinded_tokens_size()) {
    QUICHE_LOG(WARNING)
        << "Response signature size does not equal request tokens size";
    std::move(callback)(absl::InternalError(
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
      std::move(callback)(
          absl::InternalError("Failed to unescape blinded token signature"));
      return;
    }
    anonymous_tokens::AnonymousTokensSignResponse::
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
    anon_token_proto.set_do_not_use_rsa_public_exponent(true);
    at_sign_response.add_anonymous_tokens()->Swap(&anon_token_proto);
  }

  auto signed_tokens = bssa_client->ProcessResponse(at_sign_response);
  if (!signed_tokens.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign ProcessResponse failed: "
                        << signed_tokens.status();
    std::move(callback)(signed_tokens.status());
    return;
  }
  if (signed_tokens->size() !=
      static_cast<size_t>(at_sign_response.anonymous_tokens_size())) {
    QUICHE_LOG(WARNING)
        << "ProcessResponse did not output the right number of signed tokens";
    std::move(callback)(absl::InternalError(
        "ProcessResponse did not output the right number of signed tokens"));
    return;
  }

  // Output SpendTokenData with data for the redeemer to make a SpendToken RPC.
  std::vector<BlindSignToken> tokens_vec;
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
    auto use_case = anonymous_tokens::ParseUseCase(
        at_sign_response.anonymous_tokens(i).use_case());
    if (!use_case.ok()) {
      QUICHE_LOG(WARNING) << "Failed to parse use case: " << use_case.status();
      std::move(callback)(use_case.status());
      return;
    }
    spend_token_data.set_use_case(*use_case);
    spend_token_data.set_message_mask(
        signed_tokens->at(i).token().message_mask());
    tokens_vec.push_back(BlindSignToken{spend_token_data.SerializeAsString(),
                                        public_key_expiry_time});
  }

  std::move(callback)(absl::Span<BlindSignToken>(tokens_vec));
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
      OmitDefault(metadata.debug_mode()),
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

absl::StatusCode BlindSignAuth::HttpCodeToStatusCode(int http_code) {
  // copybara:strip_begin(golink)
  // This mapping is from go/http-canonical-mapping
  // copybara:strip_end
  if (http_code >= 200 && http_code < 300) {
    return absl::StatusCode::kOk;
  } else if (http_code >= 300 && http_code < 400) {
    return absl::StatusCode::kUnknown;
  } else if (http_code == 400) {
    return absl::StatusCode::kInvalidArgument;
  } else if (http_code == 401) {
    return absl::StatusCode::kUnauthenticated;
  } else if (http_code == 403) {
    return absl::StatusCode::kPermissionDenied;
  } else if (http_code == 404) {
    return absl::StatusCode::kNotFound;
  } else if (http_code == 409) {
    return absl::StatusCode::kAborted;
  } else if (http_code == 416) {
    return absl::StatusCode::kOutOfRange;
  } else if (http_code == 429) {
    return absl::StatusCode::kResourceExhausted;
  } else if (http_code == 499) {
    return absl::StatusCode::kCancelled;
  } else if (http_code >= 400 && http_code < 500) {
    return absl::StatusCode::kFailedPrecondition;
  } else if (http_code == 501) {
    return absl::StatusCode::kUnimplemented;
  } else if (http_code == 503) {
    return absl::StatusCode::kUnavailable;
  } else if (http_code == 504) {
    return absl::StatusCode::kDeadlineExceeded;
  } else if (http_code >= 500 && http_code < 600) {
    return absl::StatusCode::kInternal;
  }
  return absl::StatusCode::kUnknown;
}

}  // namespace quiche
