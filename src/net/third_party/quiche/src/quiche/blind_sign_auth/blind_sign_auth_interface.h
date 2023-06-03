// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_

#include <functional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// BlindSignAuth provides signed, unblinded tokens to callers.
class QUICHE_EXPORT BlindSignAuthInterface {
 public:
  virtual ~BlindSignAuthInterface() = default;

  // Returns signed unblinded tokens in a callback. Tokens are single-use.
  virtual void GetTokens(
      absl::string_view oauth_token, int num_tokens,
      std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
          callback) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_INTERFACE_H_
