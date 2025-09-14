// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "anonymous_tokens/cpp/privacy_pass/rsa_bssa_public_metadata_client.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/blind_sign_auth/blind_sign_message_interface.h"
#include "quiche/blind_sign_auth/blind_sign_message_response.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuth : public BlindSignAuthInterface {
 public:
  explicit BlindSignAuth(BlindSignMessageInterface* fetcher,
                         privacy::ppn::BlindSignAuthOptions auth_options)
      : fetcher_(fetcher), auth_options_(std::move(auth_options)) {}

  // Returns signed unblinded tokens, their expiration time, and their geo in a
  // callback.
  // Tokens are single-use.
  // The GetTokens callback will run on the same thread as the
  // BlindSignMessageInterface callbacks.
  // Callers can make multiple concurrent requests to GetTokens.
  void GetTokens(std::optional<std::string> oauth_token, int num_tokens,
                 ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
                 SignedTokenCallback callback) override;

  // Returns signed unblinded tokens and their expiration time in a
  // SignedTokenCallback. Errors will be returned in the SignedTokenCallback
  // only. Tokens are single-use and restricted to the PI use case.
  // GetAttestationTokens callback will run on the same thread as the
  // BlindSignMessageInterface callbacks.
  // Callers can make multiple concurrent requests to GetAttestationTokens.
  // In the AttestationDataCallback, the caller must call the
  // AttestAndSignCallback and provide AttestationData generated using Keystore
  // and the challenge returned in AttestationDataCallback. If a token challenge
  // is provided in the AttestAndSignCallback, it will be used in creating the
  // token. Otherwise a default challenge will be used containing the issuer
  // hostname.
  void GetAttestationTokens(int num_tokens, ProxyLayer layer,
                            AttestationDataCallback attestation_data_callback,
                            SignedTokenCallback token_callback) override;

 private:
  struct PrivacyPassContext {
    bssl::UniquePtr<RSA> rsa_public_key;
    anonymous_tokens::Extensions extensions;
    absl::Time public_metadata_expiry_time;
    anonymous_tokens::GeoHint geo_hint;
    anonymous_tokens::AnonymousTokensUseCase use_case;
    std::string token_key_id;
    uint32_t key_version = 0;
    std::string public_metadata_extensions_str;
  };

  struct GeneratedTokenRequests {
    std::vector<std::unique_ptr<anonymous_tokens::
                                    PrivacyPassRsaBssaPublicMetadataClient>>
        privacy_pass_clients;
    std::vector<std::string> privacy_pass_blinded_tokens_b64;
  };

  // Helper functions for GetTokens flow without device attestation.
  void GetInitialDataCallback(
      std::optional<std::string> oauth_token, int num_tokens,
      ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
      SignedTokenCallback callback,
      absl::StatusOr<BlindSignMessageResponse> response);

  void GeneratePrivacyPassTokens(
      privacy::ppn::GetInitialDataResponse initial_data_response,
      std::optional<std::string> oauth_token, int num_tokens,
      ProxyLayer proxy_layer, BlindSignAuthServiceType service_type,
      SignedTokenCallback callback);

  void PrivacyPassAuthAndSignCallback(
      const PrivacyPassContext& pp_context,
      std::vector<std::unique_ptr<anonymous_tokens::
                                      PrivacyPassRsaBssaPublicMetadataClient>>
          privacy_pass_clients,
      SignedTokenCallback callback,
      absl::StatusOr<BlindSignMessageResponse> response);

  // Helper functions for GetAttestationTokens flow.
  void GetAttestationTokensCallback(
      int num_tokens, AttestationDataCallback attestation_data_callback,
      SignedTokenCallback token_callback,
      absl::StatusOr<BlindSignMessageResponse> response);
  void AttestAndSign(int num_tokens,
                     privacy::ppn::GetInitialDataResponse initial_data_response,
                     SignedTokenCallback callback,
                     absl::StatusOr<std::string> attestation_data,
                     std::optional<std::string> token_challenge);
  void AttestAndSignCallback(
      PrivacyPassContext pp_context,
      const std::vector<std::unique_ptr<
          anonymous_tokens::
              PrivacyPassRsaBssaPublicMetadataClient>>& privacy_pass_clients,
      SignedTokenCallback callback,
      absl::StatusOr<BlindSignMessageResponse> response);

  absl::StatusOr<privacy::ppn::GetInitialDataResponse>
  ParseGetInitialDataResponseMessage(
      const absl::StatusOr<BlindSignMessageResponse>& response_statusor);

  absl::StatusOr<PrivacyPassContext> CreatePrivacyPassContext(
      const privacy::ppn::GetInitialDataResponse& initial_data_response);

  absl::StatusOr<GeneratedTokenRequests> GenerateBlindedTokenRequests(
      int num_tokens, const RSA& rsa_public_key,
      absl::string_view token_challenge_str, absl::string_view token_key_id,
      const anonymous_tokens::Extensions& extensions);

  privacy::ppn::ProxyLayer QuicheProxyLayerToPpnProxyLayer(
      quiche::ProxyLayer proxy_layer);
  // Replaces '+' and '/' with '-' and '_' in a Base64 string.
  std::string ConvertBase64ToWebSafeBase64(std::string base64_string);

  BlindSignMessageInterface* fetcher_ = nullptr;
  privacy::ppn::BlindSignAuthOptions auth_options_;
};

std::string BlindSignAuthServiceTypeToString(
    quiche::BlindSignAuthServiceType service_type);

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
