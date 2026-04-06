// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/http/status_code_mapping.h"

#include "absl/status/status.h"

namespace quiche {

int StatusCodeAbslToHttp(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kOk:
      return 200;  // OK
    case absl::StatusCode::kUnauthenticated:
      return 401;  // Unauthorized
    case absl::StatusCode::kPermissionDenied:
      return 403;  // Forbidden
    case absl::StatusCode::kNotFound:
      return 404;  // Not Found
    case absl::StatusCode::kResourceExhausted:
      return 429;  // Too Many Requests
    case absl::StatusCode::kUnavailable:
      return 503;  // Service Unavailable

    case absl::StatusCode::kOutOfRange:
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kAlreadyExists:
      return 400;  // Bad Request

    default:
      return 500;  // Internal Server Error
  }
}

int StatusToHttpStatusCode(const absl::Status& status) {
  return StatusCodeAbslToHttp(status.code());
}

}  // namespace quiche
