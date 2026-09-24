// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_MESSAGE_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_MESSAGE_INTERFACE_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/blind_sign_message_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace quiche::test {

class QUICHE_NO_EXPORT MockBlindSignMessageInterface
    : public BlindSignMessageInterface {
 public:
  MOCK_METHOD(void, DoRequest,
              (BlindSignMessageRequestType request_type,
               std::optional<absl::string_view> authorization_header,
               const std::string& body, BlindSignMessageCallback callback),
              (override));
};

}  // namespace quiche::test

#endif  // QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_MESSAGE_INTERFACE_H_
