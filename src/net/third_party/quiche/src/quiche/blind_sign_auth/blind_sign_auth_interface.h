// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// A BlindSignToken is used to authenticate a request to a privacy proxy.
// The token string contains a serialized SpendTokenData proto.
// The token cannot be successfully redeemed after the expiration time.
struct QUICHE_EXPORT BlindSignToken {
  std::string token;
  absl::Time expiration;
};

using SignedTokenCallback =
    SingleUseCallback<void(absl::StatusOr<absl::Span<BlindSignToken>>)>;

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuthInterface {
 public:
  virtual ~BlindSignAuthInterface() = default;

  // Returns signed unblinded tokens in a callback. Tokens are single-use.
  virtual void GetTokens(std::string oauth_token, int num_tokens,
                         SignedTokenCallback callback) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
