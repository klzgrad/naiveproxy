// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_messages.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer) {
  if (integer >=
      static_cast<uint64_t>(MoqtObjectStatus::kInvalidObjectStatus)) {
    return MoqtObjectStatus::kInvalidObjectStatus;
  }
  return static_cast<MoqtObjectStatus>(integer);
}

MoqtFilterType GetFilterType(const MoqtSubscribe& message) {
  if (message.start.has_value()) {
    if (message.end_group.has_value()) {
      if (*message.end_group < message.start->group) {
        return MoqtFilterType::kNone;
      }
      return MoqtFilterType::kAbsoluteRange;
    }
    return MoqtFilterType::kAbsoluteStart;
  }
  if (message.end_group.has_value()) {
    return MoqtFilterType::kNone;  // End group without start is invalid.
  }
  return MoqtFilterType::kLatestObject;
}

std::string MoqtMessageTypeToString(const MoqtMessageType message_type) {
  switch (message_type) {
    case MoqtMessageType::kClientSetup:
      return "CLIENT_SETUP";
    case MoqtMessageType::kServerSetup:
      return "SERVER_SETUP";
    case MoqtMessageType::kSubscribe:
      return "SUBSCRIBE_REQUEST";
    case MoqtMessageType::kSubscribeOk:
      return "SUBSCRIBE_OK";
    case MoqtMessageType::kSubscribeError:
      return "SUBSCRIBE_ERROR";
    case MoqtMessageType::kUnsubscribe:
      return "UNSUBSCRIBE";
    case MoqtMessageType::kSubscribeDone:
      return "SUBSCRIBE_DONE";
    case MoqtMessageType::kSubscribeUpdate:
      return "SUBSCRIBE_UPDATE";
    case MoqtMessageType::kAnnounceCancel:
      return "ANNOUNCE_CANCEL";
    case MoqtMessageType::kTrackStatusRequest:
      return "TRACK_STATUS_REQUEST";
    case MoqtMessageType::kTrackStatus:
      return "TRACK_STATUS";
    case MoqtMessageType::kAnnounce:
      return "ANNOUNCE";
    case MoqtMessageType::kAnnounceOk:
      return "ANNOUNCE_OK";
    case MoqtMessageType::kAnnounceError:
      return "ANNOUNCE_ERROR";
    case MoqtMessageType::kUnannounce:
      return "UNANNOUNCE";
    case MoqtMessageType::kGoAway:
      return "GOAWAY";
    case MoqtMessageType::kSubscribeAnnounces:
      return "SUBSCRIBE_NAMESPACE";
    case MoqtMessageType::kSubscribeAnnouncesOk:
      return "SUBSCRIBE_NAMESPACE_OK";
    case MoqtMessageType::kSubscribeAnnouncesError:
      return "SUBSCRIBE_NAMESPACE_ERROR";
    case MoqtMessageType::kUnsubscribeAnnounces:
      return "UNSUBSCRIBE_NAMESPACE";
    case MoqtMessageType::kMaxSubscribeId:
      return "MAX_SUBSCRIBE_ID";
    case MoqtMessageType::kFetch:
      return "FETCH";
    case MoqtMessageType::kFetchCancel:
      return "FETCH_CANCEL";
    case MoqtMessageType::kFetchOk:
      return "FETCH_OK";
    case MoqtMessageType::kFetchError:
      return "FETCH_ERROR";
    case MoqtMessageType::kSubscribesBlocked:
      return "SUBSCRIBES_BLOCKED";
    case MoqtMessageType::kObjectAck:
      return "OBJECT_ACK";
  }
  return "Unknown message " + std::to_string(static_cast<int>(message_type));
}

std::string MoqtDataStreamTypeToString(MoqtDataStreamType type) {
  switch (type) {
    case MoqtDataStreamType::kStreamHeaderSubgroup:
      return "STREAM_HEADER_SUBGROUP";
    case MoqtDataStreamType::kStreamHeaderFetch:
      return "STREAM_HEADER_FETCH";
    case MoqtDataStreamType::kPadding:
      return "PADDING";
  }
  return "Unknown stream type " + absl::StrCat(static_cast<int>(type));
}

std::string MoqtDatagramTypeToString(MoqtDatagramType type) {
  switch (type) {
    case MoqtDatagramType::kObject:
      return "OBJECT_DATAGRAM";
    case MoqtDatagramType::kObjectStatus:
      return "OBJECT_STATUS_DATAGRAM";
  }
  return "Unknown datagram type " + absl::StrCat(static_cast<int>(type));
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

std::string FullTrackName::ToString() const {
  std::vector<std::string> bits;
  bits.reserve(tuple_.size());
  for (absl::string_view raw_bit : tuple_) {
    bits.push_back(absl::StrCat("\"", absl::CHexEscape(raw_bit), "\""));
  }
  return absl::StrCat("{", absl::StrJoin(bits, ", "), "}");
}

bool FullTrackName::operator==(const FullTrackName& other) const {
  if (tuple_.size() != other.tuple_.size()) {
    return false;
  }
  return absl::c_equal(tuple_, other.tuple_);
}
bool FullTrackName::operator<(const FullTrackName& other) const {
  return absl::c_lexicographical_compare(tuple_, other.tuple_);
}
FullTrackName::FullTrackName(absl::Span<const absl::string_view> elements)
    : tuple_(elements.begin(), elements.end()) {
  QUICHE_BUG_IF(Moqt_namespace_too_large_03,
                std::size(elements) > (kMaxNamespaceElements + 1))
      << "Constructing a namespace that is too large.";
}

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase) {
  switch (error_code) {
    case kResetCodeSubscriptionGone:
      return absl::NotFoundError(reason_phrase);
    case kResetCodeTimedOut:
      return absl::DeadlineExceededError(reason_phrase);
    default:
      return absl::UnknownError(reason_phrase);
  }
}

}  // namespace moqt
