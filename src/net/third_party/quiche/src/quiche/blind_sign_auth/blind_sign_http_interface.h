// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_INTERFACE_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_INTERFACE_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "quiche/blind_sign_auth/blind_sign_http_response.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Interface for async HTTP POST requests in BlindSignAuth.
// Implementers must send a request to a signer hostname, using the request's
// arguments, and call the provided callback when a request is complete.
class QUICHE_EXPORT BlindSignHttpInterface {
 public:
  virtual ~BlindSignHttpInterface() = default;
  // Non-HTTP errors (like failing to create a socket) must return an
  // absl::Status.
  // HTTP errors must set status_code and body in BlindSignHttpResponse.
  // DoRequest must be a HTTP POST request.
  // Requests do not need cookies and must follow redirects.
  // The implementer must set Content-Type and Accept headers to
  // "application/x-protobuf".
  // DoRequest is async. When the request completes, the implementer must call
  // the provided callback.
  virtual void DoRequest(
      const std::string& path_and_query,
      const std::string& authorization_header, const std::string& body,
      std::function<void(absl::StatusOr<BlindSignHttpResponse>)> callback) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_HTTP_INTERFACE_H_
