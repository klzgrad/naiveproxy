// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_RESPONSE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_RESPONSE_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Contains a response to a HTTP POST request issued by BlindSignAuth.
class QUICHE_EXPORT BlindSignHttpResponse {
 public:
  BlindSignHttpResponse(int status_code, std::string body)
      : status_code_(status_code), body_(std::move(body)) {}

  int status_code() const { return status_code_; }
  const std::string& body() const { return body_; }

 private:
  int status_code_;
  std::string body_;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_RESPONSE_H_
