// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/blind_sign_auth.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"
#include "anonymous_tokens/cpp/client/anonymous_tokens_rsa_bssa_client.h"
#include "anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "anonymous_tokens/cpp/privacy_pass/rsa_bssa_public_metadata_client.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "anonymous_tokens/cpp/shared/proto_utils.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/blind_sign_auth/blind_sign_message_interface.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/common/quiche_random.h"

namespace quiche {
namespace {

template <typename T>
std::string OmitDefault(T value) {
  return value == 0 ? "" : absl::StrCat(value);
}

constexpr absl::string_view kIssuerHostname =
    "https://ipprotection-ppissuer.googleapis.com";

}  // namespace

void BlindSignAuth::GetTokens(std::optional<std::string> oauth_token,
                              int num_tokens, ProxyLayer proxy_layer,
                              SignedTokenCallback callback) {
  // Create GetInitialData RPC.
  privacy::ppn::GetInitialDataRequest request;
  request.set_use_attestation(false);
  request.set_service_type("chromeipblinding");
  request.set_location_granularity(
      privacy::ppn::GetInitialDataRequest_LocationGranularity_CITY_GEOS);
  // Validation version must be 2 to use ProxyLayer.
  request.set_validation_version(2);
  request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  // Call GetInitialData on the HttpFetcher.
  std::string body = request.SerializeAsString();
  BlindSignHttpCallback initial_data_callback = absl::bind_front(
      &BlindSignAuth::GetInitialDataCallback, this, oauth_token, num_tokens,
      proxy_layer, std::move(callback));
  http_fetcher_->DoRequest(BlindSignHttpRequestType::kGetInitialData,
                           oauth_token, body, std::move(initial_data_callback));
}

void BlindSignAuth::GetInitialDataCallback(
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, SignedTokenCallback callback,
    absl::StatusOr<BlindSignHttpResponse> response) {
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed: "
                        << response.status();
    std::move(callback)(response.status());
    return;
  }
  absl::StatusCode code = HttpCodeToStatusCode(response->status_code());
  if (code != absl::StatusCode::kOk) {
    std::string message =
        absl::StrCat("GetInitialDataRequest failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::Status(code, message));
    return;
  }
  // Parse GetInitialDataResponse.
  privacy::ppn::GetInitialDataResponse initial_data_response;
  if (!initial_data_response.ParseFromString(response->body())) {
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

  // Create token signing requests.
  bool use_privacy_pass_client =
      initial_data_response.has_privacy_pass_data() &&
      auth_options_.enable_privacy_pass();

  if (use_privacy_pass_client) {
    QUICHE_DVLOG(1) << "Using Privacy Pass client";
    GeneratePrivacyPassTokens(
        initial_data_response, *public_metadata_expiry_time,
        std::move(oauth_token), num_tokens, proxy_layer, std::move(callback));
  } else {
    QUICHE_DVLOG(1) << "Using public metadata client";
    GenerateRsaBssaTokens(initial_data_response, *public_metadata_expiry_time,
                          std::move(oauth_token), num_tokens, proxy_layer,
                          std::move(callback));
  }
}

void BlindSignAuth::GeneratePrivacyPassTokens(
    privacy::ppn::GetInitialDataResponse initial_data_response,
    absl::Time public_metadata_expiry_time,
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, SignedTokenCallback callback) {
  // Set up values used in the token generation loop.
  anonymous_tokens::RSAPublicKey public_key_proto;
  if (!public_key_proto.ParseFromString(
          initial_data_response.at_public_metadata_public_key()
              .serialized_public_key())) {
    std::move(callback)(
        absl::InvalidArgumentError("Failed to parse Privacy Pass public key"));
    return;
  }
  absl::StatusOr<bssl::UniquePtr<RSA>> bssl_rsa_key =
      anonymous_tokens::CreatePublicKeyRSA(
          public_key_proto.n(), public_key_proto.e());
  if (!bssl_rsa_key.ok()) {
    std::move(callback)(bssl_rsa_key.status());
    return;
  }
  absl::StatusOr<anonymous_tokens::Extensions> extensions =
      anonymous_tokens::DecodeExtensions(
          initial_data_response.privacy_pass_data()
              .public_metadata_extensions());
  if (!extensions.ok()) {
    QUICHE_LOG(WARNING) << "Failed to decode extensions: "
                        << extensions.status();
    std::move(callback)(extensions.status());
    return;
  }
  std::vector<uint16_t> kExpectedExtensionTypes = {
      /*ExpirationTimestamp=*/0x0001, /*GeoHint=*/0x0002,
      /*ServiceType=*/0xF001, /*DebugMode=*/0xF002, /*ProxyLayer=*/0xF003};
  absl::Status result =
      anonymous_tokens::ValidateExtensionsOrderAndValues(
          *extensions, absl::MakeSpan(kExpectedExtensionTypes), absl::Now());
  if (!result.ok()) {
    QUICHE_LOG(WARNING) << "Failed to validate extensions: " << result;
    std::move(callback)(result);
    return;
  }

  // Create token challenge.
  anonymous_tokens::TokenChallenge challenge;
  challenge.issuer_name = kIssuerHostname;
  absl::StatusOr<std::string> token_challenge =
      anonymous_tokens::MarshalTokenChallenge(challenge);
  if (!token_challenge.ok()) {
    QUICHE_LOG(WARNING) << "Failed to marshal token challenge: "
                        << token_challenge.status();
    std::move(callback)(token_challenge.status());
    return;
  }

  QuicheRandom* random = QuicheRandom::GetInstance();
  // Create vector of Privacy Pass clients, one for each token.
  std::vector<anonymous_tokens::ExtendedTokenRequest>
      extended_token_requests;
  std::vector<std::unique_ptr<anonymous_tokens::
                                  PrivacyPassRsaBssaPublicMetadataClient>>
      privacy_pass_clients;
  std::vector<std::string> privacy_pass_blinded_tokens;

  for (int i = 0; i < num_tokens; i++) {
    // Create client.
    auto client = anonymous_tokens::
        PrivacyPassRsaBssaPublicMetadataClient::Create(*bssl_rsa_key.value());
    if (!client.ok()) {
      QUICHE_LOG(WARNING) << "Failed to create Privacy Pass client: "
                          << client.status();
      std::move(callback)(client.status());
      return;
    }

    // Create nonce.
    std::string nonce_rand(32, '\0');
    random->RandBytes(nonce_rand.data(), nonce_rand.size());

    // Create token request.
    absl::StatusOr<anonymous_tokens::ExtendedTokenRequest>
        extended_token_request = client.value()->CreateTokenRequest(
            *token_challenge, nonce_rand,
            initial_data_response.privacy_pass_data().token_key_id(),
            *extensions);
    if (!extended_token_request.ok()) {
      QUICHE_LOG(WARNING) << "Failed to create ExtendedTokenRequest: "
                          << extended_token_request.status();
      std::move(callback)(extended_token_request.status());
      return;
    }
    privacy_pass_clients.push_back(*std::move(client));
    extended_token_requests.push_back(*extended_token_request);
    privacy_pass_blinded_tokens.push_back(absl::Base64Escape(
        extended_token_request->request.blinded_token_request));
  }

  privacy::ppn::AuthAndSignRequest sign_request;
  sign_request.set_service_type("chromeipblinding");
  sign_request.set_key_type(privacy::ppn::AT_PUBLIC_METADATA_KEY_TYPE);
  sign_request.set_key_version(
      initial_data_response.at_public_metadata_public_key().key_version());
  sign_request.mutable_blinded_token()->Assign(
      privacy_pass_blinded_tokens.begin(), privacy_pass_blinded_tokens.end());
  sign_request.mutable_public_metadata_extensions()->assign(
      initial_data_response.privacy_pass_data().public_metadata_extensions());
  // TODO(b/295924807): deprecate this option after AT server defaults to it
  sign_request.set_do_not_use_rsa_public_exponent(true);
  sign_request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  absl::StatusOr<anonymous_tokens::AnonymousTokensUseCase>
      use_case = anonymous_tokens::ParseUseCase(
          initial_data_response.at_public_metadata_public_key().use_case());
  if (!use_case.ok()) {
    QUICHE_LOG(WARNING) << "Failed to parse use case: " << use_case.status();
    std::move(callback)(use_case.status());
    return;
  }

  BlindSignHttpCallback auth_and_sign_callback =
      absl::bind_front(&BlindSignAuth::PrivacyPassAuthAndSignCallback, this,
                       std::move(initial_data_response.privacy_pass_data()
                                     .public_metadata_extensions()),
                       public_metadata_expiry_time, *use_case,
                       std::move(privacy_pass_clients), std::move(callback));
  // TODO(b/304811277): remove other usages of string.data()
  http_fetcher_->DoRequest(BlindSignHttpRequestType::kAuthAndSign, oauth_token,
                           sign_request.SerializeAsString(),
                           std::move(auth_and_sign_callback));
}

void BlindSignAuth::GenerateRsaBssaTokens(
    privacy::ppn::GetInitialDataResponse initial_data_response,
    absl::Time public_metadata_expiry_time,
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, SignedTokenCallback callback) {
  // Create public metadata client.
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
  QuicheRandom* random = QuicheRandom::GetInstance();
  std::vector<
      anonymous_tokens::PlaintextMessageWithPublicMetadata>
      plaintext_tokens;
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
  sign_request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  privacy::ppn::PublicMetadataInfo public_metadata_info =
      initial_data_response.public_metadata_info();
  BlindSignHttpCallback auth_and_sign_callback = absl::bind_front(
      &BlindSignAuth::AuthAndSignCallback, this, public_metadata_info,
      public_metadata_expiry_time, *at_sign_request, *std::move(bssa_client),
      std::move(callback));
  http_fetcher_->DoRequest(BlindSignHttpRequestType::kAuthAndSign, oauth_token,
                           sign_request.SerializeAsString(),
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
  absl::StatusCode code = HttpCodeToStatusCode(response->status_code());
  if (code != absl::StatusCode::kOk) {
    std::string message = absl::StrCat("AuthAndSign failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::Status(code, message));
    return;
  }
  // Decode AuthAndSignResponse.
  privacy::ppn::AuthAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response->body())) {
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

void BlindSignAuth::PrivacyPassAuthAndSignCallback(
    std::string encoded_extensions, absl::Time public_key_expiry_time,
    anonymous_tokens::AnonymousTokensUseCase use_case,
    std::vector<std::unique_ptr<anonymous_tokens::
                                    PrivacyPassRsaBssaPublicMetadataClient>>
        privacy_pass_clients,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignHttpResponse> response) {
  // Validate response.
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign failed: " << response.status();
    std::move(callback)(response.status());
    return;
  }
  absl::StatusCode code = HttpCodeToStatusCode(response->status_code());
  if (code != absl::StatusCode::kOk) {
    std::string message = absl::StrCat("AuthAndSign failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::Status(code, message));
    return;
  }

  // Decode AuthAndSignResponse.
  privacy::ppn::AuthAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response->body())) {
    QUICHE_LOG(WARNING) << "Failed to parse AuthAndSignResponse";
    std::move(callback)(
        absl::InternalError("Failed to parse AuthAndSignResponse"));
    return;
  }
  if (static_cast<size_t>(sign_response.blinded_token_signature_size()) !=
      privacy_pass_clients.size()) {
    QUICHE_LOG(WARNING) << "Number of signatures does not equal number of "
                           "Privacy Pass tokens sent";
    std::move(callback)(
        absl::InternalError("Number of signatures does not equal number of "
                            "Privacy Pass tokens sent"));
    return;
  }

  // Create tokens using blinded signatures.
  std::vector<BlindSignToken> tokens_vec;
  for (int i = 0; i < sign_response.blinded_token_signature_size(); i++) {
    std::string unescaped_blinded_sig;
    if (!absl::Base64Unescape(sign_response.blinded_token_signature()[i],
                              &unescaped_blinded_sig)) {
      QUICHE_LOG(WARNING) << "Failed to unescape blinded signature";
      std::move(callback)(
          absl::InternalError("Failed to unescape blinded signature"));
      return;
    }

    absl::StatusOr<anonymous_tokens::Token> token =
        privacy_pass_clients[i]->FinalizeToken(unescaped_blinded_sig);
    if (!token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to finalize token: " << token.status();
      std::move(callback)(token.status());
      return;
    }

    absl::StatusOr<std::string> marshaled_token =
        anonymous_tokens::MarshalToken(*token);
    if (!marshaled_token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to marshal token: "
                          << marshaled_token.status();
      std::move(callback)(marshaled_token.status());
      return;
    }

    privacy::ppn::PrivacyPassTokenData privacy_pass_token_data;
    privacy_pass_token_data.mutable_token()->assign(
        absl::WebSafeBase64Escape(*marshaled_token));
    privacy_pass_token_data.mutable_encoded_extensions()->assign(
        absl::WebSafeBase64Escape(encoded_extensions));
    privacy_pass_token_data.set_use_case_override(use_case);
    tokens_vec.push_back(BlindSignToken{
        privacy_pass_token_data.SerializeAsString(), public_key_expiry_time});
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

privacy::ppn::ProxyLayer BlindSignAuth::QuicheProxyLayerToPpnProxyLayer(
    quiche::ProxyLayer proxy_layer) {
  switch (proxy_layer) {
    case ProxyLayer::kProxyA: {
      return privacy::ppn::ProxyLayer::PROXY_A;
    }
    case ProxyLayer::kProxyB: {
      return privacy::ppn::ProxyLayer::PROXY_B;
    }
  }
}

}  // namespace quiche
