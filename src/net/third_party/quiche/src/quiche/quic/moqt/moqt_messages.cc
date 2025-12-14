// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_messages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

void KeyValuePairList::insert(uint64_t key, absl::string_view value) {
  if (key % 2 == 0) {
    QUICHE_BUG(key_value_pair_string_is_even) << "Key value pair of wrong type";
    return;
  }
  string_map_.emplace(key, value);
}

void KeyValuePairList::insert(uint64_t key, uint64_t value) {
  if (key % 2 == 1) {
    QUICHE_BUG(key_value_pair_int_is_odd) << "Key value pair of wrong type";
    return;
  }
  integer_map_.emplace(key, value);
}

size_t KeyValuePairList::count(uint64_t key) const {
  if (key % 2 == 0) {
    return integer_map_.count(key);
  }
  return string_map_.count(key);
}

bool KeyValuePairList::contains(uint64_t key) const {
  if (key % 2 == 0) {
    return integer_map_.contains(key);
  }
  return string_map_.contains(key);
}

std::vector<uint64_t> KeyValuePairList::GetIntegers(uint64_t key) const {
  if (key % 2 == 1) {
    QUICHE_BUG(key_value_pair_int_is_odd) << "Key value pair of wrong type";
    return {};
  }
  std::vector<uint64_t> result;
  auto [range_start, range_end] = integer_map_.equal_range(key);
  for (auto& it = range_start; it != range_end; ++it) {
    result.push_back(it->second);
  }
  return result;
}

std::vector<absl::string_view> KeyValuePairList::GetStrings(
    uint64_t key) const {
  if (key % 2 == 0) {
    QUICHE_BUG(key_value_pair_string_is_even) << "Key value pair of wrong type";
    return {};
  }
  std::vector<absl::string_view> result;
  auto [range_start, range_end] = string_map_.equal_range(key);
  for (auto& it = range_start; it != range_end; ++it) {
    result.push_back(it->second);
  }
  return result;
}

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer) {
  if (integer >=
      static_cast<uint64_t>(MoqtObjectStatus::kInvalidObjectStatus)) {
    return MoqtObjectStatus::kInvalidObjectStatus;
  }
  return static_cast<MoqtObjectStatus>(integer);
}

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
      return RequestErrorCode::kTrackDoesNotExist;
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
    case RequestErrorCode::kTrackDoesNotExist:
      // Equivalently, kUninterested and kNamespacePrefixUnknown.
      return absl::StatusCode::kNotFound;
    case RequestErrorCode::kInvalidRange:
      // Equivalently, kNamespacePrefixOverlap.
      return absl::StatusCode::kOutOfRange;
    case RequestErrorCode::kNoObjects:
      // Equivalently, kRetryTrackAlias.
      return absl::StatusCode::kNotFound;
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

MoqtError ValidateSetupParameters(const KeyValuePairList& parameters,
                                  bool webtrans,
                                  quic::Perspective perspective) {
  if (parameters.count(SetupParameter::kPath) > 1 ||
      parameters.count(SetupParameter::kMaxRequestId) > 1 ||
      parameters.count(SetupParameter::kMaxAuthTokenCacheSize) > 1 ||
      parameters.count(SetupParameter::kSupportObjectAcks) > 1) {
    return MoqtError::kKeyValueFormattingError;
  }
  if ((webtrans || perspective == quic::Perspective::IS_CLIENT) ==
      parameters.contains(SetupParameter::kPath)) {
    // Only non-webtrans servers should receive kPath.
    return MoqtError::kInvalidPath;
  }
  if ((webtrans || perspective == quic::Perspective::IS_CLIENT) &&
      parameters.contains(SetupParameter::kAuthority)) {
    // Only non-webtrans servers should receive kAuthority.
    return MoqtError::kInvalidAuthority;
  }
  if (!parameters.contains(SetupParameter::kSupportObjectAcks)) {
    return MoqtError::kNoError;
  }
  std::vector<uint64_t> support_object_acks =
      parameters.GetIntegers(SetupParameter::kSupportObjectAcks);
  QUICHE_DCHECK(support_object_acks.size() == 1);
  if (support_object_acks.front() > 1) {
    return MoqtError::kKeyValueFormattingError;
  }
  return MoqtError::kNoError;
}

const std::array<MoqtMessageType, 9> kAllowsAuthorization = {
    MoqtMessageType::kClientSetup,
    MoqtMessageType::kServerSetup,
    MoqtMessageType::kPublish,
    MoqtMessageType::kSubscribe,
    MoqtMessageType::kSubscribeUpdate,
    MoqtMessageType::kSubscribeNamespace,
    MoqtMessageType::kPublishNamespace,
    MoqtMessageType::kTrackStatus,
    MoqtMessageType::kFetch};
const std::array<MoqtMessageType, 7> kAllowsDeliveryTimeout = {
    MoqtMessageType::kTrackStatus,    MoqtMessageType::kTrackStatusOk,
    MoqtMessageType::kPublish,        MoqtMessageType::kPublishOk,
    MoqtMessageType::kSubscribe,      MoqtMessageType::kSubscribeOk,
    MoqtMessageType::kSubscribeUpdate};
const std::array<MoqtMessageType, 4> kAllowsMaxCacheDuration = {
    MoqtMessageType::kSubscribeOk, MoqtMessageType::kTrackStatusOk,
    MoqtMessageType::kFetchOk, MoqtMessageType::kPublish};
bool ValidateVersionSpecificParameters(const KeyValuePairList& parameters,
                                       MoqtMessageType message_type) {
  size_t authorization_token =
      parameters.count(VersionSpecificParameter::kAuthorizationToken);
  size_t delivery_timeout =
      parameters.count(VersionSpecificParameter::kDeliveryTimeout);
  size_t max_cache_duration =
      parameters.count(VersionSpecificParameter::kMaxCacheDuration);
  if (delivery_timeout > 1 || max_cache_duration > 1) {
    // Disallowed duplicate.
    return false;
  }
  if (authorization_token > 0 &&
      !absl::c_linear_search(kAllowsAuthorization, message_type)) {
    return false;
  }
  if (delivery_timeout > 0 &&
      !absl::c_linear_search(kAllowsDeliveryTimeout, message_type)) {
    return false;
  }
  if (max_cache_duration > 0 &&
      !absl::c_linear_search(kAllowsMaxCacheDuration, message_type)) {
    return false;
  }
  return true;
}

std::string MoqtMessageTypeToString(const MoqtMessageType message_type) {
  switch (message_type) {
    case MoqtMessageType::kClientSetup:
      return "CLIENT_SETUP";
    case MoqtMessageType::kServerSetup:
      return "SERVER_SETUP";
    case MoqtMessageType::kSubscribe:
      return "SUBSCRIBE";
    case MoqtMessageType::kSubscribeOk:
      return "SUBSCRIBE_OK";
    case MoqtMessageType::kSubscribeError:
      return "SUBSCRIBE_ERROR";
    case MoqtMessageType::kUnsubscribe:
      return "UNSUBSCRIBE";
    case MoqtMessageType::kPublishDone:
      return "SUBSCRIBE_DONE";
    case MoqtMessageType::kSubscribeUpdate:
      return "SUBSCRIBE_UPDATE";
    case MoqtMessageType::kPublishNamespaceCancel:
      return "PUBLISH_NAMESPACE_CANCEL";
    case MoqtMessageType::kTrackStatus:
      return "TRACK_STATUS";
    case MoqtMessageType::kTrackStatusOk:
      return "TRACK_STATUS_OK";
    case MoqtMessageType::kTrackStatusError:
      return "TRACK_STATUS_ERROR";
    case MoqtMessageType::kPublishNamespace:
      return "PUBLISH_NAMESPACE";
    case MoqtMessageType::kPublishNamespaceOk:
      return "PUBLISH_NAMESPACE_OK";
    case MoqtMessageType::kPublishNamespaceError:
      return "PUBLISH_NAMESPACE_ERROR";
    case MoqtMessageType::kPublishNamespaceDone:
      return "PUBLISH_NAMESPACE_DONE";
    case MoqtMessageType::kGoAway:
      return "GOAWAY";
    case MoqtMessageType::kSubscribeNamespace:
      return "SUBSCRIBE_NAMESPACE";
    case MoqtMessageType::kSubscribeNamespaceOk:
      return "SUBSCRIBE_NAMESPACE_OK";
    case MoqtMessageType::kSubscribeNamespaceError:
      return "SUBSCRIBE_NAMESPACE_ERROR";
    case MoqtMessageType::kUnsubscribeNamespace:
      return "UNSUBSCRIBE_NAMESPACE";
    case MoqtMessageType::kMaxRequestId:
      return "MAX_REQUEST_ID";
    case MoqtMessageType::kPublish:
      return "PUBLISH";
    case MoqtMessageType::kPublishOk:
      return "PUBLISH_OK";
    case MoqtMessageType::kPublishError:
      return "PUBLISH_ERROR";
    case MoqtMessageType::kFetch:
      return "FETCH";
    case MoqtMessageType::kFetchCancel:
      return "FETCH_CANCEL";
    case MoqtMessageType::kFetchOk:
      return "FETCH_OK";
    case MoqtMessageType::kFetchError:
      return "FETCH_ERROR";
    case MoqtMessageType::kRequestsBlocked:
      return "REQUESTS_BLOCKED";
    case MoqtMessageType::kObjectAck:
      return "OBJECT_ACK";
  }
  return "Unknown message " + std::to_string(static_cast<int>(message_type));
}

std::string MoqtDataStreamTypeToString(MoqtDataStreamType type) {
  if (type.IsPadding()) {
    return "PADDING";
  } else if (type.IsFetch()) {
    return "STREAM_HEADER_FETCH";
  }
  return absl::StrCat("STREAM_HEADER_SUBGROUP_", type.value());
}

std::string MoqtDatagramTypeToString(MoqtDatagramType type) {
  return absl::StrCat("DATAGRAM", type.has_status() ? "_STATUS" : "",
                      type.has_extension() ? "_EXTENSION" : "");
}

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference) {
  switch (preference) {
    case MoqtForwardingPreference::kDatagram:
      return "DATAGRAM";
    case MoqtForwardingPreference::kSubgroup:
      return "SUBGROUP";
  }
  QUIC_BUG(quic_bug_bad_moqt_message_type_01)
      << "Unknown preference " << std::to_string(static_cast<int>(preference));
  return "Unknown preference " + std::to_string(static_cast<int>(preference));
}

TrackNamespace::TrackNamespace(absl::Span<const absl::string_view> elements)
    : tuple_(elements.begin(), elements.end()) {
  if (std::size(elements) > kMaxNamespaceElements) {
    tuple_.clear();
    QUICHE_BUG(Moqt_namespace_too_large_01)
        << "Constructing a namespace that is too large.";
    return;
  }
  for (auto it : elements) {
    length_ += it.size();
    if (length_ > kMaxFullTrackNameSize) {
      tuple_.clear();
      QUICHE_BUG(Moqt_namespace_too_large_02)
          << "Constructing a namespace that is too large.";
      return;
    }
  }
}

bool TrackNamespace::InNamespace(const TrackNamespace& other) const {
  if (tuple_.size() < other.tuple_.size()) {
    return false;
  }
  for (int i = 0; i < other.tuple_.size(); ++i) {
    if (tuple_[i] != other.tuple_[i]) {
      return false;
    }
  }
  return true;
}

void TrackNamespace::AddElement(absl::string_view element) {
  if (!CanAddElement(element)) {
    QUICHE_BUG(Moqt_namespace_too_large_03)
        << "Constructing a namespace that is too large.";
    return;
  }
  length_ += element.length();
  tuple_.push_back(std::string(element));
}

std::string TrackNamespace::ToString() const {
  std::vector<std::string> bits;
  bits.reserve(tuple_.size());
  for (absl::string_view raw_bit : tuple_) {
    bits.push_back(absl::StrCat("\"", absl::CHexEscape(raw_bit), "\""));
  }
  return absl::StrCat("{", absl::StrJoin(bits, "::"), "}");
}

void FullTrackName::set_name(absl::string_view name) {
  QUIC_BUG_IF(Moqt_name_too_large_03, !CanAddName(name))
      << "Setting a name that is too large.";
  name_ = name;
}

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase) {
  switch (error_code) {
    case kResetCodeCanceled:
      return absl::CancelledError(reason_phrase);
    case kResetCodeDeliveryTimeout:
      return absl::DeadlineExceededError(reason_phrase);
    case kResetCodeSessionClosed:
      return absl::AbortedError(reason_phrase);
    case kResetCodeMalformedTrack:
      return absl::InvalidArgumentError(reason_phrase);
    default:
      return absl::UnknownError(reason_phrase);
  }
}

}  // namespace moqt
