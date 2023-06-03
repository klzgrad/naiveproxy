// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_AUTH_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_AUTH_INTERFACE_H_

#include <functional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/blind_sign_auth/blind_sign_auth_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace quiche::test {

class QUICHE_NO_EXPORT MockBlindSignAuthInterface
    : public BlindSignAuthInterface {
 public:
  MOCK_METHOD(
      void, GetTokens,
      (absl::string_view oauth_token, int num_tokens,
       std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
           callback),
      (override));
};

}  // namespace quiche::test

#endif  // QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_AUTH_INTERFACE_H_
