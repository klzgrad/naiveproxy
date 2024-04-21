// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "anonymous_tokens/cpp/client/anonymous_tokens_rsa_bssa_client.h"
#include "anonymous_tokens/cpp/privacy_pass/rsa_bssa_public_metadata_client.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/blind_sign_auth/blind_sign_message_interface.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuth : public BlindSignAuthInterface {
 public:
  explicit BlindSignAuth(BlindSignMessageInterface* http_fetcher,
                         privacy::ppn::BlindSignAuthOptions auth_options)
      : http_fetcher_(http_fetcher), auth_options_(std::move(auth_options)) {}

  // Returns signed unblinded tokens and their expiration time in a callback.
  // Tokens are single-use.
  // GetTokens starts asynchronous HTTP POST requests to a signer hostname
  // specified by the caller, with path and query params given in the request.
  // The GetTokens callback will run on the same thread as the
  // BlindSignMessageInterface callbacks.
  // Callers can make multiple concurrent requests to GetTokens.
  void GetTokens(std::optional<std::string> oauth_token, int num_tokens,
                 ProxyLayer proxy_layer, SignedTokenCallback callback) override;

 private:
  void GetInitialDataCallback(std::optional<std::string> oauth_token,
                              int num_tokens, ProxyLayer proxy_layer,
                              SignedTokenCallback callback,
                              absl::StatusOr<BlindSignHttpResponse> response);
  void GeneratePrivacyPassTokens(
      privacy::ppn::GetInitialDataResponse initial_data_response,
      absl::Time public_metadata_expiry_time,
      std::optional<std::string> oauth_token, int num_tokens,
      ProxyLayer proxy_layer, SignedTokenCallback callback);
  void GenerateRsaBssaTokens(
      privacy::ppn::GetInitialDataResponse initial_data_response,
      absl::Time public_metadata_expiry_time,
      std::optional<std::string> oauth_token, int num_tokens,
      ProxyLayer proxy_layer, SignedTokenCallback callback);
  void AuthAndSignCallback(
      privacy::ppn::PublicMetadataInfo public_metadata_info,
      absl::Time public_key_expiry_time,
      anonymous_tokens::AnonymousTokensSignRequest
          at_sign_request,
      std::unique_ptr<
          anonymous_tokens::AnonymousTokensRsaBssaClient>
          bssa_client,
      SignedTokenCallback callback,
      absl::StatusOr<BlindSignHttpResponse> response);
  void PrivacyPassAuthAndSignCallback(
      std::string encoded_extensions, absl::Time public_key_expiry_time,
      anonymous_tokens::AnonymousTokensUseCase use_case,
      std::vector<std::unique_ptr<anonymous_tokens::
                                      PrivacyPassRsaBssaPublicMetadataClient>>
          privacy_pass_clients,
      SignedTokenCallback callback,
      absl::StatusOr<BlindSignHttpResponse> response);
  absl::Status FingerprintPublicMetadata(
      const privacy::ppn::PublicMetadata& metadata, uint64_t* fingerprint);
  absl::StatusCode HttpCodeToStatusCode(int http_code);
  privacy::ppn::ProxyLayer QuicheProxyLayerToPpnProxyLayer(
      quiche::ProxyLayer proxy_layer);

  BlindSignMessageInterface* http_fetcher_ = nullptr;
  privacy::ppn::BlindSignAuthOptions auth_options_;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
