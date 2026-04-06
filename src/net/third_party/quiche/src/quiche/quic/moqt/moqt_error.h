// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_ERROR_H_
#define QUICHE_QUIC_MOQT_MOQT_ERROR_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// These are session errors sent in CONNECTION_CLOSE.
enum QUICHE_EXPORT MoqtError : uint64_t {
  kNoError = 0x0,
  kInternalError = 0x1,
  kUnauthorized = 0x2,
  kProtocolViolation = 0x3,
  kInvalidRequestId = 0x4,
  kDuplicateTrackAlias = 0x5,
  kKeyValueFormattingError = 0x6,
  kTooManyRequests = 0x7,
  kInvalidPath = 0x8,
  kMalformedPath = 0x9,
  kGoawayTimeout = 0x10,
  kControlMessageTimeout = 0x11,
  kDataStreamTimeout = 0x12,
  kAuthTokenCacheOverflow = 0x13,
  kDuplicateAuthTokenAlias = 0x14,
  kVersionNegotiationFailed = 0x15,
  kMalformedAuthToken = 0x16,
  kUnknownAuthTokenAlias = 0x17,
  kExpiredAuthToken = 0x18,
  kInvalidAuthority = 0x19,
  kMalformedAuthority = 0x1a,
};

// Error codes used by MoQT to reset streams.
inline constexpr webtransport::StreamErrorCode kResetCodeInternalError = 0x00;
inline constexpr webtransport::StreamErrorCode kResetCodeCancelled = 0x01;
inline constexpr webtransport::StreamErrorCode kResetCodeDeliveryTimeout = 0x02;
inline constexpr webtransport::StreamErrorCode kResetCodeSessionClosed = 0x03;
inline constexpr webtransport::StreamErrorCode kResetCodeUnknownObjectStatus =
    0x04;
inline constexpr webtransport::StreamErrorCode kResetCodeMalformedTrack = 0x12;
// Proposed in a PR post draft-16.
inline constexpr webtransport::StreamErrorCode kResetCodeTooFarBehind = 0x05;

// Used for SUBSCRIBE_ERROR, PUBLISH_NAMESPACE_ERROR, PUBLISH_NAMESPACE_CANCEL,
// SUBSCRIBE_NAMESPACE_ERROR, and FETCH_ERROR.
enum class QUICHE_EXPORT RequestErrorCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTimeout = 0x2,
  kNotSupported = 0x3,
  kMalformedAuthToken = 0x4,
  kExpiredAuthToken = 0x5,
  kDoesNotExist = 0x10,
  kInvalidRange = 0x11,
  kMalformedTrack = 0x12,
  kDuplicateSubscription = 0x19,
  kUninterested = 0x20,
  kNamespacePrefixUnknown = 0x21,
  kPrefixOverlap = 0x30,
  kInvalidJoiningRequestId = 0x32,
};

enum class QUICHE_EXPORT PublishDoneCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTrackEnded = 0x2,
  kSubscriptionEnded = 0x3,
  kGoingAway = 0x4,
  kExpired = 0x5,
  kTooFarBehind = 0x6,
  kUpdateFailed = 0x8,
  kMalformedTrack = 0x12,
};

struct MoqtRequestErrorInfo {
  RequestErrorCode error_code;
  std::optional<quic::QuicTimeDelta> retry_interval;
  std::string reason_phrase;
  bool operator==(const MoqtRequestErrorInfo& other) const = default;
};

RequestErrorCode StatusToRequestErrorCode(absl::Status status);
absl::StatusCode RequestErrorCodeToStatusCode(RequestErrorCode error_code);
absl::Status RequestErrorCodeToStatus(RequestErrorCode error_code,
                                      absl::string_view reason_phrase);

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_ERROR_H_
