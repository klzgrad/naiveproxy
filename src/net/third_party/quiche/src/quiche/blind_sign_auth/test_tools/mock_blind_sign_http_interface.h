// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_HTTP_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_HTTP_INTERFACE_H_

#include <string>

#include "quiche/blind_sign_auth/blind_sign_http_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace quiche::test {

class QUICHE_NO_EXPORT MockBlindSignHttpInterface
    : public BlindSignHttpInterface {
 public:
  MOCK_METHOD(void, DoRequest,
              (BlindSignHttpRequestType request_type,
               const std::string& authorization_header, const std::string& body,
               BlindSignHttpCallback callback),
              (override));
};

}  // namespace quiche::test

#endif  // QUICHE_BLIND_SIGN_AUTH_TEST_TOOLS_MOCK_BLIND_SIGN_HTTP_INTERFACE_H_
