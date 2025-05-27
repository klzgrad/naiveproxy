// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

using ::anonymous_tokens::GeoHint;

// ProxyLayer indicates which proxy layer that tokens will be used with.
enum class ProxyLayer : int {
  kProxyA,
  kProxyB,
};

// BlindSignAuthServiceType indicates which service that tokens will be
// authenticated with.
enum class BlindSignAuthServiceType {
  kChromeIpBlinding,
  kCronetIpBlinding,
  kWebviewIpBlinding,
};

// A BlindSignToken is used to authenticate a request to a privacy proxy.
// The token string contains a serialized SpendTokenData proto.
// The token cannot be successfully redeemed after the expiration time.
struct QUICHE_EXPORT BlindSignToken {
  std::string token;
  absl::Time expiration;
  GeoHint geo_hint;
};

using SignedTokenCallback =
    SingleUseCallback<void(absl::StatusOr<absl::Span<BlindSignToken>>)>;

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuthInterface {
 public:
  virtual ~BlindSignAuthInterface() = default;

  // Returns signed unblinded tokens in a callback. Tokens are single-use.
  virtual void GetTokens(std::optional<std::string> oauth_token, int num_tokens,
                         ProxyLayer proxy_layer,
                         BlindSignAuthServiceType service_type,
                         SignedTokenCallback callback) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
