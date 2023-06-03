// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "quiche/blind_sign_auth/proto/public_metadata.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/client/anonymous_tokens_rsa_bssa_client.h"
#include "quiche/blind_sign_auth/anonymous_tokens/proto/anonymous_tokens.pb.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/blind_sign_auth/blind_sign_http_interface.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuth : public BlindSignAuthInterface {
 public:
  explicit BlindSignAuth(BlindSignHttpInterface* http_fetcher)
      : http_fetcher_(http_fetcher) {}

  // Returns signed unblinded tokens in a callback. Tokens are single-use.
  // GetTokens starts asynchronous HTTP POST requests to a signer hostname
  // specified by the caller, with path and query params given in the request.
  // The GetTokens callback will run on the same thread as the
  // BlindSignHttpInterface callbacks.
  // Callers can make multiple concurrent requests to GetTokens.
  void GetTokens(
      absl::string_view oauth_token, int num_tokens,
      std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
          callback) override;

 private:
  void GetInitialDataCallback(
      absl::StatusOr<BlindSignHttpResponse> response,
      absl::string_view oauth_token, int num_tokens,
      std::function<void(absl::StatusOr<absl::Span<std::string>>)> callback);
  void AuthAndSignCallback(
      absl::StatusOr<BlindSignHttpResponse> response,
      privacy::ppn::PublicMetadataInfo public_metadata_info,
      private_membership::anonymous_tokens::AnonymousTokensSignRequest
          at_sign_request,
      private_membership::anonymous_tokens::AnonymousTokensRsaBssaClient*
          bssa_client,
      std::function<void(absl::StatusOr<absl::Span<std::string>>)> callback);
  absl::Status FingerprintPublicMetadata(
      const privacy::ppn::PublicMetadata& metadata, uint64_t* fingerprint);

  BlindSignHttpInterface* http_fetcher_ = nullptr;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_H_
