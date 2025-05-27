#include "quiche/blind_sign_auth/blind_sign_message_response.h"

namespace quiche {

absl::StatusCode BlindSignMessageResponse::HttpCodeToStatusCode(int http_code) {
  // copybara:strip_begin(golink)
  // This mapping is from go/http-canonical-mapping
  // copybara:strip_end
  if (http_code >= 200 && http_code < 300) {
    return absl::StatusCode::kOk;
  } else if (http_code >= 300 && http_code < 400) {
    return absl::StatusCode::kUnknown;
  } else if (http_code == 400) {
    return absl::StatusCode::kInvalidArgument;
  } else if (http_code == 401) {
    return absl::StatusCode::kUnauthenticated;
  } else if (http_code == 403) {
    return absl::StatusCode::kPermissionDenied;
  } else if (http_code == 404) {
    return absl::StatusCode::kNotFound;
  } else if (http_code == 409) {
    return absl::StatusCode::kAborted;
  } else if (http_code == 416) {
    return absl::StatusCode::kOutOfRange;
  } else if (http_code == 429) {
    return absl::StatusCode::kResourceExhausted;
  } else if (http_code == 499) {
    return absl::StatusCode::kCancelled;
  } else if (http_code >= 400 && http_code < 500) {
    return absl::StatusCode::kFailedPrecondition;
  } else if (http_code == 501) {
    return absl::StatusCode::kUnimplemented;
  } else if (http_code == 503) {
    return absl::StatusCode::kUnavailable;
  } else if (http_code == 504) {
    return absl::StatusCode::kDeadlineExceeded;
  } else if (http_code >= 500 && http_code < 600) {
    return absl::StatusCode::kInternal;
  }
  return absl::StatusCode::kUnknown;
}

}  // namespace quiche
