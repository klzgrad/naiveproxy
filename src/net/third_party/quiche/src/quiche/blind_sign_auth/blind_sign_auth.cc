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

constexpr absl::string_view kAttestationProtoTypeUrl =
    "type.googleapis.com/privacy.ppn.AndroidAttestationData";
constexpr absl::string_view kIssuerHostname =
    "https://ipprotection-ppissuer.googleapis.com";
constexpr size_t kExpectedExtensionTypesSize = 5;
constexpr std::array<const uint16_t, kExpectedExtensionTypesSize>
    kExpectedExtensionTypes = {0x0001, 0x0002, 0xF001, 0xF002, 0xF003};

using quiche::protobuf::Any;
using privacy::ppn::AndroidAttestationData;
using privacy::ppn::AttestAndSignRequest;
using privacy::ppn::AttestAndSignResponse;
using privacy::ppn::AttestationData;
using privacy::ppn::AuthAndSignRequest;
using privacy::ppn::AuthAndSignResponse;
using privacy::ppn::GetInitialDataRequest;
using privacy::ppn::GetInitialDataResponse;
using privacy::ppn::PrivacyPassTokenData;
using anonymous_tokens::AnonymousTokensUseCase;
using anonymous_tokens::CreatePublicKeyRSA;
using anonymous_tokens::DecodeExtensions;
using anonymous_tokens::ExpirationTimestamp;
using anonymous_tokens::ExtendedTokenRequest;
using anonymous_tokens::Extensions;
using anonymous_tokens::GeoHint;
using anonymous_tokens::MarshalTokenChallenge;
using anonymous_tokens::ParseUseCase;
using anonymous_tokens::
    PrivacyPassRsaBssaPublicMetadataClient;
using anonymous_tokens::RSAPublicKey;
using anonymous_tokens::Token;
using anonymous_tokens::TokenChallenge;
using anonymous_tokens::ValidateExtensionsOrderAndValues;

}  // namespace

void BlindSignAuth::GetTokens(std::optional<std::string> oauth_token,
                              int num_tokens, ProxyLayer proxy_layer,
                              BlindSignAuthServiceType service_type,
                              SignedTokenCallback callback) {
  // Create GetInitialData RPC.
  GetInitialDataRequest request;
  request.set_use_attestation(false);
  request.set_service_type(BlindSignAuthServiceTypeToString(service_type));
  request.set_location_granularity(
      privacy::ppn::GetInitialDataRequest_LocationGranularity_CITY_GEOS);
  // Validation version must be 2 to use ProxyLayer.
  request.set_validation_version(2);
  request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  // Call GetInitialData on the BlindSignMessageInterface Fetcher.
  std::string body_bytes = request.SerializeAsString();
  BlindSignMessageCallback initial_data_callback = absl::bind_front(
      &BlindSignAuth::GetInitialDataCallback, this, oauth_token, num_tokens,
      proxy_layer, service_type, std::move(callback));
  fetcher_->DoRequest(BlindSignMessageRequestType::kGetInitialData, oauth_token,
                      body_bytes, std::move(initial_data_callback));
}

void BlindSignAuth::GetInitialDataCallback(
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignMessageResponse> response) {
  absl::StatusOr<GetInitialDataResponse> initial_data_response =
      ParseGetInitialDataResponseMessage(response);
  if (!initial_data_response.ok()) {
    std::move(callback)(initial_data_response.status());
    return;
  }

  // Create token signing requests.
  const bool use_privacy_pass_client =
      initial_data_response->has_privacy_pass_data() &&
      auth_options_.enable_privacy_pass();

  if (use_privacy_pass_client) {
    QUICHE_DVLOG(1) << "Using Privacy Pass client";
    GeneratePrivacyPassTokens(*initial_data_response, std::move(oauth_token),
                              num_tokens, proxy_layer, service_type,
                              std::move(callback));
  } else {
    QUICHE_LOG(ERROR) << "Non-Privacy Pass tokens are no longer supported";
    std::move(callback)(absl::UnimplementedError(
        "Non-Privacy Pass tokens are no longer supported"));
    return;
  }
}

void BlindSignAuth::GeneratePrivacyPassTokens(
    privacy::ppn::GetInitialDataResponse initial_data_response,
    std::optional<std::string> oauth_token, int num_tokens,
    ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
    SignedTokenCallback callback) {
  absl::StatusOr<PrivacyPassContext> pp_context =
      CreatePrivacyPassContext(initial_data_response);
  if (!pp_context.ok()) {
    std::move(callback)(pp_context.status());
    return;
  }

  // Create token challenge.
  TokenChallenge challenge;
  challenge.issuer_name = kIssuerHostname;
  absl::StatusOr<std::string> token_challenge =
      MarshalTokenChallenge(challenge);
  if (!token_challenge.ok()) {
    QUICHE_LOG(WARNING) << "Failed to marshal token challenge: "
                        << token_challenge.status();
    std::move(callback)(
        absl::InvalidArgumentError("Failed to marshal token challenge"));
    return;
  }

  absl::StatusOr<GeneratedTokenRequests> token_requests_data =
      GenerateBlindedTokenRequests(num_tokens, *pp_context->rsa_public_key,
                                   *token_challenge, pp_context->token_key_id,
                                   pp_context->extensions);
  if (!token_requests_data.ok()) {
    std::move(callback)(token_requests_data.status());
    return;
  }

  AuthAndSignRequest sign_request;
  sign_request.set_service_type(BlindSignAuthServiceTypeToString(service_type));
  sign_request.set_key_type(privacy::ppn::AT_PUBLIC_METADATA_KEY_TYPE);
  sign_request.set_key_version(
      initial_data_response.at_public_metadata_public_key().key_version());
  *sign_request.mutable_blinded_token() = {
      token_requests_data->privacy_pass_blinded_tokens_b64.begin(),
      token_requests_data->privacy_pass_blinded_tokens_b64.end()};
  sign_request.mutable_public_metadata_extensions()->assign(
      initial_data_response.privacy_pass_data().public_metadata_extensions());
  // TODO(b/295924807): deprecate this option after AT server defaults to it
  sign_request.set_do_not_use_rsa_public_exponent(true);
  sign_request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(proxy_layer));

  BlindSignMessageCallback auth_and_sign_callback =
      absl::bind_front(&BlindSignAuth::PrivacyPassAuthAndSignCallback, this,
                       *std::move(pp_context),
                       std::move(token_requests_data->privacy_pass_clients),
                       std::move(callback));
  // TODO(b/304811277): remove other usages of string.data()
  fetcher_->DoRequest(BlindSignMessageRequestType::kAuthAndSign, oauth_token,
                      sign_request.SerializeAsString(),
                      std::move(auth_and_sign_callback));
}

void BlindSignAuth::PrivacyPassAuthAndSignCallback(
    const PrivacyPassContext& pp_context,
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
    std::move(callback)(absl::InvalidArgumentError(message));
    return;
  }

  // Decode AuthAndSignResponse.
  AuthAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response->body())) {
    QUICHE_LOG(WARNING) << "Failed to parse AuthAndSignResponse";
    std::move(callback)(
        absl::InternalError("Failed to parse AuthAndSignResponse"));
    return;
  }
  if (static_cast<size_t>(sign_response.blinded_token_signature_size()) >
      privacy_pass_clients.size()) {
    QUICHE_LOG(WARNING) << "Number of signatures is greater than the number of "
                           "Privacy Pass tokens sent";
    std::move(callback)(absl::InternalError(
        "Number of signatures is greater than the number of "
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

    absl::StatusOr<Token> token =
        privacy_pass_clients[i]->FinalizeToken(unescaped_blinded_sig);
    if (!token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to finalize token: " << token.status();
      std::move(callback)(absl::InternalError("Failed to finalize token"));
      return;
    }

    absl::StatusOr<std::string> marshaled_token = MarshalToken(*token);
    if (!marshaled_token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to marshal token: "
                          << marshaled_token.status();
      std::move(callback)(absl::InternalError("Failed to marshal token"));
      return;
    }

    PrivacyPassTokenData privacy_pass_token_data;
    privacy_pass_token_data.mutable_token()->assign(
        ConvertBase64ToWebSafeBase64(absl::Base64Escape(*marshaled_token)));
    privacy_pass_token_data.mutable_encoded_extensions()->assign(
        ConvertBase64ToWebSafeBase64(
            absl::Base64Escape(pp_context.public_metadata_extensions_str)));
    privacy_pass_token_data.set_use_case_override(pp_context.use_case);
    tokens_vec.push_back(BlindSignToken{
        privacy_pass_token_data.SerializeAsString(),
        pp_context.public_metadata_expiry_time, pp_context.geo_hint});
  }

  std::move(callback)(absl::Span<BlindSignToken>(tokens_vec));
}

void BlindSignAuth::GetAttestationTokens(
    int num_tokens, ProxyLayer layer,
    AttestationDataCallback attestation_data_callback,
    SignedTokenCallback token_callback) {
  GetInitialDataRequest request;
  request.set_service_type(BlindSignAuthServiceTypeToString(
      BlindSignAuthServiceType::kPrivateAratea));
  // Validation version must be 2 to use ProxyLayer.
  request.set_validation_version(2);
  request.set_proxy_layer(QuicheProxyLayerToPpnProxyLayer(layer));
  request.set_use_attestation(true);

  // Send GetAttestationData RPC on the BlindSignMessageInterface.
  std::string body_bytes = request.SerializeAsString();
  BlindSignMessageCallback initial_data_callback = absl::bind_front(
      &BlindSignAuth::GetAttestationTokensCallback, this, num_tokens,
      std::move(attestation_data_callback), std::move(token_callback));
  fetcher_->DoRequest(BlindSignMessageRequestType::kGetInitialData,
                      /*authorization_header=*/std::nullopt, body_bytes,
                      std::move(initial_data_callback));
}

void BlindSignAuth::GetAttestationTokensCallback(
    int num_tokens, AttestationDataCallback attestation_data_callback,
    SignedTokenCallback token_callback,
    absl::StatusOr<BlindSignMessageResponse> response) {
  absl::StatusOr<GetInitialDataResponse> initial_data_response =
      ParseGetInitialDataResponseMessage(response);
  if (!initial_data_response.ok()) {
    std::move(token_callback)(initial_data_response.status());
    return;
  }

  const bool use_privacy_pass_client =
      auth_options_.enable_privacy_pass() &&
      initial_data_response->has_privacy_pass_data();
  if (use_privacy_pass_client) {
    QUICHE_DVLOG(1) << "Using Privacy Pass client for GetAttestationTokens";
  } else {
    QUICHE_LOG(ERROR) << "Non-Privacy Pass tokens are no longer supported";
    std::move(token_callback)(absl::UnimplementedError(
        "Non-Privacy Pass tokens are no longer supported"));
    return;
  }

  // Return attestation nonce, caller will use it as the attestation challenge.
  if (!initial_data_response->has_attestation()) {
    QUICHE_LOG(WARNING)
        << "GetInitialDataResponse does not have attestation data";
    std::move(token_callback)(absl::InternalError(
        "GetInitialDataResponse does not have attestation data"));
    return;
  }
  absl::string_view attestation_nonce =
      initial_data_response->attestation().attestation_nonce();
  quiche::AttestAndSignCallback attest_and_sign_callback = absl::bind_front(
      &BlindSignAuth::AttestAndSign, this, num_tokens,
      *std::move(initial_data_response), std::move(token_callback));
  std::move(attestation_data_callback)(attestation_nonce,
                                       std::move(attest_and_sign_callback));
}

void BlindSignAuth::AttestAndSign(
    int num_tokens, privacy::ppn::GetInitialDataResponse initial_data_response,
    SignedTokenCallback callback, absl::StatusOr<std::string> attestation_data,
    std::optional<std::string> token_challenge) {
  absl::StatusOr<PrivacyPassContext> pp_context =
      CreatePrivacyPassContext(initial_data_response);
  if (!pp_context.ok()) {
    std::move(callback)(pp_context.status());
    return;
  }

  // Create token challenge if not provided.
  std::string token_challenge_str;
  if (!token_challenge.has_value()) {
    TokenChallenge challenge;
    challenge.issuer_name = kIssuerHostname;
    if (absl::StatusOr<std::string> constant_challenge =
            MarshalTokenChallenge(challenge);
        constant_challenge.ok()) {
      token_challenge_str = *constant_challenge;
    } else {
      QUICHE_LOG(WARNING) << "Failed to marshal token challenge: "
                          << constant_challenge.status();
      std::move(callback)(
          absl::InvalidArgumentError("Failed to marshal token challenge"));
      return;
    }
  } else {
    token_challenge_str = *token_challenge;
  }

  absl::StatusOr<GeneratedTokenRequests> token_requests_data =
      GenerateBlindedTokenRequests(
          num_tokens, *pp_context->rsa_public_key, token_challenge_str,
          pp_context->token_key_id, pp_context->extensions);
  if (!token_requests_data.ok()) {
    std::move(callback)(token_requests_data.status());
    return;
  }

  // Create AndroidAttestationData.
  AndroidAttestationData android_attestation_data;
  if (!attestation_data.ok()) {
    std::move(callback)(attestation_data.status());
    return;
  }
  android_attestation_data.add_hardware_backed_certs(*attestation_data);

  Any attestation_data_proto_any;
  attestation_data_proto_any.set_type_url(kAttestationProtoTypeUrl);
  attestation_data_proto_any.set_value(
      android_attestation_data.SerializeAsString());

  AttestationData attestation_data_proto;
  *attestation_data_proto.mutable_attestation_data() =
      attestation_data_proto_any;

  // Create AttestAndSignRequest.
  AttestAndSignRequest sign_request;
  sign_request.set_service_type(BlindSignAuthServiceTypeToString(
      BlindSignAuthServiceType::kPrivateAratea));
  sign_request.mutable_blinded_tokens()->Assign(
      token_requests_data->privacy_pass_blinded_tokens_b64.begin(),
      token_requests_data->privacy_pass_blinded_tokens_b64.end());
  sign_request.set_key_version(
      initial_data_response.at_public_metadata_public_key().key_version());
  sign_request.mutable_public_metadata_extensions()->assign(
      initial_data_response.privacy_pass_data().public_metadata_extensions());
  *sign_request.mutable_attestation() = attestation_data_proto;

  BlindSignMessageCallback attestation_data_callback = absl::bind_front(
      &BlindSignAuth::AttestAndSignCallback, this, *std::move(pp_context),
      std::move(token_requests_data->privacy_pass_clients),
      std::move(callback));

  fetcher_->DoRequest(BlindSignMessageRequestType::kAttestAndSign,
                      /*authorization_header=*/std::nullopt,
                      sign_request.SerializeAsString(),
                      std::move(attestation_data_callback));
}

void BlindSignAuth::AttestAndSignCallback(
    PrivacyPassContext pp_context,
    const std::vector<std::unique_ptr<
        anonymous_tokens::
            PrivacyPassRsaBssaPublicMetadataClient>>& privacy_pass_clients,
    SignedTokenCallback callback,
    absl::StatusOr<BlindSignMessageResponse> response) {
  // Validate response.
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "AttestAndSign failed: " << response.status();
    std::move(callback)(
        absl::InvalidArgumentError("AttestAndSign failed: invalid response"));
    return;
  }
  absl::StatusCode code = response->status_code();
  if (code != absl::StatusCode::kOk) {
    std::string message =
        absl::StrCat("AttestAndSign failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    std::move(callback)(absl::InvalidArgumentError(message));
    return;
  }

  // Decode AttestAndSignResponse.
  AttestAndSignResponse sign_response;
  if (!sign_response.ParseFromString(response->body())) {
    QUICHE_LOG(WARNING) << "Failed to parse AttestAndSignResponse";
    std::move(callback)(
        absl::InternalError("Failed to parse AttestAndSignResponse"));
    return;
  }
  if (static_cast<size_t>(sign_response.blinded_token_signatures_size()) >
      privacy_pass_clients.size()) {
    QUICHE_LOG(WARNING) << "Number of signatures is greater than the number of "
                           "Privacy Pass tokens sent";
    std::move(callback)(absl::InternalError(
        "Number of signatures is greater than the number of "
        "Privacy Pass tokens sent"));
    return;
  }

  // Create tokens using blinded signatures.
  std::vector<BlindSignToken> tokens_vec;
  for (int i = 0; i < sign_response.blinded_token_signatures_size(); i++) {
    std::string unescaped_blinded_sig;
    if (!absl::Base64Unescape(sign_response.blinded_token_signatures()[i],
                              &unescaped_blinded_sig)) {
      QUICHE_LOG(WARNING) << "Failed to unescape blinded signature";
      std::move(callback)(
          absl::InternalError("Failed to unescape blinded signature"));
      return;
    }

    absl::StatusOr<Token> token =
        privacy_pass_clients[i]->FinalizeToken(unescaped_blinded_sig);
    if (!token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to finalize token: " << token.status();
      std::move(callback)(absl::InternalError("Failed to finalize token"));
      return;
    }

    absl::StatusOr<std::string> marshaled_token = MarshalToken(*token);
    if (!marshaled_token.ok()) {
      QUICHE_LOG(WARNING) << "Failed to marshal token: "
                          << marshaled_token.status();
      std::move(callback)(absl::InternalError("Failed to marshal token"));
      return;
    }

    PrivacyPassTokenData privacy_pass_token_data;
    privacy_pass_token_data.mutable_token()->assign(
        ConvertBase64ToWebSafeBase64(absl::Base64Escape(*marshaled_token)));
    privacy_pass_token_data.mutable_encoded_extensions()->assign(
        ConvertBase64ToWebSafeBase64(
            absl::Base64Escape(pp_context.public_metadata_extensions_str)));
    privacy_pass_token_data.set_use_case_override(pp_context.use_case);
    tokens_vec.push_back(BlindSignToken{
        privacy_pass_token_data.SerializeAsString(),
        pp_context.public_metadata_expiry_time, pp_context.geo_hint});
  }

  std::move(callback)(absl::Span<BlindSignToken>(tokens_vec));
}

absl::StatusOr<privacy::ppn::GetInitialDataResponse>
BlindSignAuth::ParseGetInitialDataResponseMessage(
    const absl::StatusOr<BlindSignMessageResponse>& response) {
  if (!response.ok()) {
    QUICHE_LOG(WARNING) << "GetInitialDataRequest failed: "
                        << response.status();
    return absl::InvalidArgumentError(
        "GetInitialDataRequest failed: invalid response");
  }
  if (absl::StatusCode code = response->status_code();
      code != absl::StatusCode::kOk) {
    std::string message =
        absl::StrCat("GetInitialDataRequest failed with code: ", code);
    QUICHE_LOG(WARNING) << message;
    return absl::InvalidArgumentError(message);
  }
  // Parse GetInitialDataResponse.
  GetInitialDataResponse initial_data_response;
  if (!initial_data_response.ParseFromString(response->body())) {
    QUICHE_LOG(WARNING) << "Failed to parse GetInitialDataResponse";
    return absl::InternalError("Failed to parse GetInitialDataResponse");
  }
  return initial_data_response;
}

absl::StatusOr<BlindSignAuth::PrivacyPassContext>
BlindSignAuth::CreatePrivacyPassContext(
    const privacy::ppn::GetInitialDataResponse& initial_data_response) {
  RSAPublicKey public_key_proto;
  if (!public_key_proto.ParseFromString(
          initial_data_response.at_public_metadata_public_key()
              .serialized_public_key())) {
    return absl::InvalidArgumentError(
        "Failed to parse Privacy Pass public key");
  }
  absl::StatusOr<bssl::UniquePtr<RSA>> bssl_rsa_key =
      CreatePublicKeyRSA(public_key_proto.n(), public_key_proto.e());
  if (!bssl_rsa_key.ok()) {
    return absl::InternalError(absl::StrCat("Failed to create RSA public key: ",
                                            bssl_rsa_key.status().ToString()));
  }

  PrivacyPassContext context;
  context.rsa_public_key = *std::move(bssl_rsa_key);
  context.key_version =
      initial_data_response.at_public_metadata_public_key().key_version();
  context.token_key_id =
      initial_data_response.privacy_pass_data().token_key_id();
  context.public_metadata_extensions_str =
      initial_data_response.privacy_pass_data().public_metadata_extensions();

  absl::StatusOr<Extensions> extensions =
      DecodeExtensions(context.public_metadata_extensions_str);
  if (!extensions.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to decode extensions: ", extensions.status().ToString()));
  }

  if (absl::Status validation_result = ValidateExtensionsOrderAndValues(
          *extensions, absl::MakeSpan(kExpectedExtensionTypes), absl::Now());
      validation_result.ok()) {
    context.extensions = *std::move(extensions);
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to validate extensions: ", validation_result.ToString()));
  }

  if (absl::StatusOr<ExpirationTimestamp> expiration_timestamp =
          ExpirationTimestamp::FromExtension(
              context.extensions.extensions.at(0));
      expiration_timestamp.ok()) {
    context.public_metadata_expiry_time =
        absl::FromUnixSeconds(expiration_timestamp->timestamp);
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse expiration timestamp: ",
                     expiration_timestamp.status().ToString()));
  }

  if (absl::StatusOr<GeoHint> geo_hint =
          GeoHint::FromExtension(context.extensions.extensions.at(1));
      geo_hint.ok()) {
    context.geo_hint = *std::move(geo_hint);
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse geo hint: ", geo_hint.status().ToString()));
  }

  if (absl::StatusOr<AnonymousTokensUseCase> use_case = ParseUseCase(
          initial_data_response.at_public_metadata_public_key().use_case());
      use_case.ok()) {
    context.use_case = *std::move(use_case);
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse use case: ", use_case.status().ToString()));
  }

  return context;
}

absl::StatusOr<BlindSignAuth::GeneratedTokenRequests>
BlindSignAuth::GenerateBlindedTokenRequests(
    int num_tokens, const RSA& rsa_public_key,
    absl::string_view token_challenge_str, absl::string_view token_key_id,
    const anonymous_tokens::Extensions& extensions) {
  GeneratedTokenRequests result;
  result.privacy_pass_clients.reserve(num_tokens);
  result.privacy_pass_blinded_tokens_b64.reserve(num_tokens);
  QuicheRandom* random = QuicheRandom::GetInstance();

  for (int i = 0; i < num_tokens; i++) {
    absl::StatusOr<std::unique_ptr<PrivacyPassRsaBssaPublicMetadataClient>>
        client = PrivacyPassRsaBssaPublicMetadataClient::Create(rsa_public_key);
    if (!client.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to create Privacy Pass client: ",
                       client.status().ToString()));
    }

    std::string nonce_rand(32, '\0');
    random->RandBytes(nonce_rand.data(), nonce_rand.size());

    absl::StatusOr<ExtendedTokenRequest> extended_token_request =
        (*client)->CreateTokenRequest(token_challenge_str, nonce_rand,
                                      token_key_id, extensions);
    if (!extended_token_request.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to create ExtendedTokenRequest: ",
                       extended_token_request.status().ToString()));
    }
    result.privacy_pass_clients.push_back(*std::move(client));
    result.privacy_pass_blinded_tokens_b64.push_back(absl::Base64Escape(
        extended_token_request->request.blinded_token_request));
  }
  return result;
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
    case ProxyLayer::kTerminalLayer: {
      return privacy::ppn::ProxyLayer::TERMINAL_LAYER;
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
    case BlindSignAuthServiceType::kPrivateAratea: {
      return "privatearatea";
    }
  }
}

}  // namespace quiche
