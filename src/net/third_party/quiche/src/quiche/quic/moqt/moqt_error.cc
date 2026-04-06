// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_error.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

RequestErrorCode StatusToRequestErrorCode(absl::Status status) {
  QUICHE_DCHECK(!status.ok());
  switch (status.code()) {
    case absl::StatusCode::kPermissionDenied:
      return RequestErrorCode::kUnauthorized;
    case absl::StatusCode::kDeadlineExceeded:
      return RequestErrorCode::kTimeout;
    case absl::StatusCode::kUnimplemented:
      return RequestErrorCode::kNotSupported;
    case absl::StatusCode::kNotFound:
      return RequestErrorCode::kDoesNotExist;
    case absl::StatusCode::kOutOfRange:
      return RequestErrorCode::kInvalidRange;
    case absl::StatusCode::kInvalidArgument:
      return RequestErrorCode::kInvalidJoiningRequestId;
    case absl::StatusCode::kUnauthenticated:
      return RequestErrorCode::kExpiredAuthToken;
    default:
      return RequestErrorCode::kInternalError;
  }
}

absl::StatusCode RequestErrorCodeToStatusCode(RequestErrorCode error_code) {
  switch (error_code) {
    case RequestErrorCode::kInternalError:
      return absl::StatusCode::kInternal;
    case RequestErrorCode::kUnauthorized:
      return absl::StatusCode::kPermissionDenied;
    case RequestErrorCode::kTimeout:
      return absl::StatusCode::kDeadlineExceeded;
    case RequestErrorCode::kNotSupported:
      return absl::StatusCode::kUnimplemented;
    case RequestErrorCode::kDoesNotExist:
      return absl::StatusCode::kNotFound;
    case RequestErrorCode::kInvalidRange:
      return absl::StatusCode::kOutOfRange;
    case RequestErrorCode::kInvalidJoiningRequestId:
    case RequestErrorCode::kMalformedAuthToken:
      return absl::StatusCode::kInvalidArgument;
    case RequestErrorCode::kExpiredAuthToken:
      return absl::StatusCode::kUnauthenticated;
    default:
      return absl::StatusCode::kUnknown;
  }
}

absl::Status RequestErrorCodeToStatus(RequestErrorCode error_code,
                                      absl::string_view reason_phrase) {
  return absl::Status(RequestErrorCodeToStatusCode(error_code), reason_phrase);
};

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase) {
  switch (error_code) {
    case kResetCodeInternalError:
      return absl::InternalError(reason_phrase);
    case kResetCodeCancelled:
      return absl::CancelledError(reason_phrase);
    case kResetCodeDeliveryTimeout:
      return absl::DeadlineExceededError(reason_phrase);
    case kResetCodeSessionClosed:
      return absl::AbortedError(reason_phrase);
    case kResetCodeUnknownObjectStatus:
      return absl::FailedPreconditionError(reason_phrase);
    case kResetCodeTooFarBehind:
      return absl::DeadlineExceededError(reason_phrase);
    case kResetCodeMalformedTrack:
      return absl::InvalidArgumentError(reason_phrase);
    default:
      return absl::UnknownError(reason_phrase);
  }
}

}  // namespace moqt
