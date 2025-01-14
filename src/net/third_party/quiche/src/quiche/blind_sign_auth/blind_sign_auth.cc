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

#include "absl/algorithm/container.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "anonymous_tokens/cpp/privacy_pass/rsa_bssa_public_metadata_client.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "anonymous_tokens/cpp/shared/proto_utils.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/blind_sign_auth/blind_sign_message_interface.h"
#include "quiche/blind_sign_auth/blind_sign_message_response.h"
#include "quiche/common/platform/api/quiche_logging.h"
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
                              BlindSignAuthServiceType service_type,
                              SignedTokenCallback callback) {
  // Create GetInitialData RPC.
  privacy::ppn::GetInitialDataRequest request;
  request.set_use_attestation(false);
  request.set_service_type(BlindSignAuthServiceTypeToString(service_type));
  request.set_location_granularity(
      privacy::ppn::GetInitialDataRequest_LocationGranularity_CITY_GEOS);
  // Validation version must be 2 to use ProxyLayer.
  request.set_validation_version(2);
  request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  // Call GetInitialData on the BlindSignMessageInterface Fetcher.
  std::string body = request.SerializeAsString();
  BlindSignMessageCallback initial_data_callback = absl::bind_front(
      &BlindSignAuth::GetInitialDataCallback, this, oauth_token, num_tokens,
      proxy_layer, service_type, std::move(callback));
  fetcher_->DoRequest(BlindSignMessageRequestType::kGetInitialData, oauth_token,
                      body, std::move(initial_data_callback));
}

void BlindSignAuth::GetInitialDataCallback(
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignMessageResponse> response) {
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed: "
                        << response.status();
    std::move(callback)(absl::InvalidArgumentError(
        "GetInitialDataRequest failed: invalid response"));
    return;
  }
  absl::StatusCode code = response->status_code();
  if (code != absl::StatusCode::kOk) {
    std::string message =
        absl::StrCat("GetInitialDataRequest failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(
        absl::InvalidArgumentError("GetInitialDataRequest failed"));
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

  // Create token signing requests.
  bool use_privacy_pass_client =
      initial_data_response.has_privacy_pass_data() &&
      auth_options_.enable_privacy_pass();

  if (use_privacy_pass_client) {
    QUICHE_DVLOG(1) << "Using Privacy Pass client";
    GeneratePrivacyPassTokens(initial_data_response, std::move(oauth_token),
                              num_tokens, proxy_layer, service_type,
                              std::move(callback));
  } else {
    QUICHE_LOG(ERROR) << "Non-Privacy Pass tokens are no longer supported";
    std::move(callback)(absl::UnimplementedError(
        "Non-Privacy Pass tokens are no longer supported"));
  }
}

void BlindSignAuth::GeneratePrivacyPassTokens(
    privacy::ppn::GetInitialDataResponse initial_data_response,
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
    SignedTokenCallback callback) {
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
    QUICHE_LOG(ERROR) << "Failed to create RSA public key: "
                      << bssl_rsa_key.status();
    std::move(callback)(absl::InternalError("Failed to create RSA public key"));
    return;
  }
  absl::StatusOr<anonymous_tokens::Extensions> extensions =
      anonymous_tokens::DecodeExtensions(
          initial_data_response.privacy_pass_data()
              .public_metadata_extensions());
  if (!extensions.ok()) {
    QUICHE_LOG(WARNING) << "Failed to decode extensions: "
                        << extensions.status();
    std::move(callback)(
        absl::InvalidArgumentError("Failed to decode extensions"));
    return;
  }
  std::vector<uint16_t> kExpectedExtensionTypes = {
      /*ExpirationTimestamp=*/0x0001, /*GeoHint=*/0x0002,
      /*ServiceType=*/0xF001, /*DebugMode=*/0xF002, /*ProxyLayer=*/0xF003};
  // TODO(b/345801768): Improve the API of
  // `anonymous_tokens::ValidateExtensionsOrderAndValues` to
  // avoid any possible TOCTOU problems.
  absl::Status result =
      anonymous_tokens::ValidateExtensionsOrderAndValues(
          *extensions, absl::MakeSpan(kExpectedExtensionTypes), absl::Now());
  if (!result.ok()) {
    QUICHE_LOG(WARNING) << "Failed to validate extensions: " << result;
    std::move(callback)(
        absl::InvalidArgumentError("Failed to validate extensions"));
    return;
  }
  absl::StatusOr<anonymous_tokens::ExpirationTimestamp>
      expiration_timestamp = anonymous_tokens::
          ExpirationTimestamp::FromExtension(extensions->extensions.at(0));
  if (!expiration_timestamp.ok()) {
    QUICHE_LOG(WARNING) << "Failed to parse expiration timestamp: "
                        << expiration_timestamp.status();
    std::move(callback)(
        absl::InvalidArgumentError("Failed to parse expiration timestamp"));
    return;
  }
  absl::Time public_metadata_expiry_time =
      absl::FromUnixSeconds(expiration_timestamp->timestamp);

  absl::StatusOr<anonymous_tokens::GeoHint> geo_hint =
      anonymous_tokens::GeoHint::FromExtension(
          extensions->extensions.at(1));
  QUICHE_CHECK(geo_hint.ok());

  // Create token challenge.
  anonymous_tokens::TokenChallenge challenge;
  challenge.issuer_name = kIssuerHostname;
  absl::StatusOr<std::string> token_challenge =
      anonymous_tokens::MarshalTokenChallenge(challenge);
  if (!token_challenge.ok()) {
    QUICHE_LOG(WARNING) << "Failed to marshal token challenge: "
                        << token_challenge.status();
    std::move(callback)(
        absl::InvalidArgumentError("Failed to marshal token challenge"));
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
      std::move(callback)(
          absl::InternalError("Failed to create Privacy Pass client"));
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
      std::move(callback)(
          absl::InternalError("Failed to create ExtendedTokenRequest"));
      return;
    }
    privacy_pass_clients.push_back(*std::move(client));
    extended_token_requests.push_back(*extended_token_request);
    privacy_pass_blinded_tokens.push_back(absl::Base64Escape(
        extended_token_request->request.blinded_token_request));
  }

  privacy::ppn::AuthAndSignRequest sign_request;
  sign_request.set_service_type(BlindSignAuthServiceTypeToString(service_type));
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
    std::move(callback)(absl::InvalidArgumentError("Failed to parse use case"));
    return;
  }

  BlindSignMessageCallback auth_and_sign_callback =
      absl::bind_front(&BlindSignAuth::PrivacyPassAuthAndSignCallback, this,
                       std::move(initial_data_response.privacy_pass_data()
                                     .public_metadata_extensions()),
                       public_metadata_expiry_time, *geo_hint, *use_case,
                       std::move(privacy_pass_clients), std::move(callback));
  // TODO(b/304811277): remove other usages of string.data()
  fetcher_->DoRequest(BlindSignMessageRequestType::kAuthAndSign, oauth_token,
                      sign_request.SerializeAsString(),
                      std::move(auth_and_sign_callback));
}

void BlindSignAuth::PrivacyPassAuthAndSignCallback(
    std::string encoded_extensions, absl::Time public_key_expiry_time,
    anonymous_tokens::GeoHint geo_hint,
    anonymous_tokens::AnonymousTokensUseCase use_case,
    std::vector<std::unique_ptr<anonymous_tokens::
                                    PrivacyPassRsaBssaPublicMetadataClient>>
        privacy_pass_clients,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignMessageResponse> response) {
  // Validate response.
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "AuthAndSign failed: " << response.status();
    std::move(callback)(
        absl::InvalidArgumentError("AuthAndSign failed: invalid response"));
    return;
  }
  absl::StatusCode code = response->status_code();
  if (code != absl::StatusCode::kOk) {
    std::string message = absl::StrCat("AuthAndSign failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::InvalidArgumentError("AuthAndSign failed"));
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
      std::move(callback)(absl::InternalError("Failed to finalize token"));
      return;
    }

    absl::StatusOr<std::string> marshaled_token =
        anonymous_tokens::MarshalToken(*token);
    if (!marshaled_token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to marshal token: "
                          << marshaled_token.status();
      std::move(callback)(absl::InternalError("Failed to marshal token"));
      return;
    }

    privacy::ppn::PrivacyPassTokenData privacy_pass_token_data;
    privacy_pass_token_data.mutable_token()->assign(
        ConvertBase64ToWebSafeBase64(absl::Base64Escape(*marshaled_token)));
    privacy_pass_token_data.mutable_encoded_extensions()->assign(
        ConvertBase64ToWebSafeBase64(absl::Base64Escape(encoded_extensions)));
    privacy_pass_token_data.set_use_case_override(use_case);
    tokens_vec.push_back(
        BlindSignToken{privacy_pass_token_data.SerializeAsString(),
                       public_key_expiry_time, geo_hint});
  }

  std::move(callback)(absl::Span<BlindSignToken>(tokens_vec));
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

std::string BlindSignAuth::ConvertBase64ToWebSafeBase64(
    std::string base64_string) {
  absl::c_replace(base64_string, /*old_value=*/'+', /*new_value=*/'-');
  absl::c_replace(base64_string, /*old_value=*/'/', /*new_value=*/'_');
  return base64_string;
}

std::string BlindSignAuthServiceTypeToString(
    quiche::BlindSignAuthServiceType service_type) {
  switch (service_type) {
    case BlindSignAuthServiceType::kChromeIpBlinding: {
      return "chromeipblinding";
    }
    case BlindSignAuthServiceType::kCronetIpBlinding: {
      return "cronetipblinding";
    }
    case BlindSignAuthServiceType::kWebviewIpBlinding: {
      // Currently WebView uses the same service type as Chrome.
      // TODO(b/280621504): Change this once we have a more specific service
      // type.
      return "chromeipblinding";
    }
  }
}

}  // namespace quiche
