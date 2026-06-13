// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_messages.h"

#include <cstdint>
#include <string>

#include "absl/strings/str_cat.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace moqt {

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer) {
  if (integer >=
      static_cast<uint64_t>(MoqtObjectStatus::kInvalidObjectStatus)) {
    return MoqtObjectStatus::kInvalidObjectStatus;
  }
  return static_cast<MoqtObjectStatus>(integer);
}

MoqtError SetupParametersAllowedByMessage(const SetupParameters& parameters,
                                          MoqtMessageType message_type,
                                          bool webtrans) {
  bool should_have_path_and_authority =
      !webtrans && message_type == MoqtMessageType::kClientSetup;
  if (should_have_path_and_authority != parameters.path.has_value()) {
    return MoqtError::kInvalidPath;
  }
  if (should_have_path_and_authority != parameters.authority.has_value()) {
    return MoqtError::kInvalidAuthority;
  }
  return MoqtError::kNoError;
}

// Parameter types are not enforced by message in draft-16, but apparently this
// is coming back later.
#if 0
const std::array<MoqtMessageType, 9> kAllowsAuthorization = {
    MoqtMessageType::kClientSetup,
    MoqtMessageType::kServerSetup,
    MoqtMessageType::kPublish,
    MoqtMessageType::kSubscribe,
    MoqtMessageType::kRequestUpdate,
    MoqtMessageType::kSubscribeNamespace,
    MoqtMessageType::kPublishNamespace,
    MoqtMessageType::kTrackStatus,
    MoqtMessageType::kFetch};
const std::array<MoqtMessageType, 7> kAllowsDeliveryTimeout = {
    MoqtMessageType::kTrackStatus,  MoqtMessageType::kRequestOk,
    MoqtMessageType::kPublish,      MoqtMessageType::kPublishOk,
    MoqtMessageType::kSubscribe,    MoqtMessageType::kSubscribeOk,
    MoqtMessageType::kRequestUpdate};
bool MessageParametersAllowedByMessage(
    const MessageParameters& parameters, MoqtMessageType message_type) {
  if (!parameters.authorization_tokens.empty() &&
      !absl::c_linear_search(kAllowsAuthorization, message_type)) {
    return false;
  }
  if (parameters.delivery_timeout != quic::QuicTimeDelta::Infinite() &&
      !absl::c_linear_search(kAllowsDeliveryTimeout, message_type)) {
    return false;
  }
  return true;
}
#endif

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
    case MoqtMessageType::kRequestError:
      return "REQUEST_ERROR";
    case MoqtMessageType::kUnsubscribe:
      return "UNSUBSCRIBE";
    case MoqtMessageType::kPublishDone:
      return "PUBLISH_DONE";
    case MoqtMessageType::kRequestUpdate:
      return "REQUEST_UPDATE";
    case MoqtMessageType::kPublishNamespaceCancel:
      return "PUBLISH_NAMESPACE_CANCEL";
    case MoqtMessageType::kTrackStatus:
      return "TRACK_STATUS";
    case MoqtMessageType::kPublishNamespace:
      return "PUBLISH_NAMESPACE";
    case MoqtMessageType::kNamespace:
      return "NAMESPACE";
    case MoqtMessageType::kNamespaceDone:
      return "NAMESPACE_DONE";
    case MoqtMessageType::kRequestOk:
      return "REQUEST_OK";
    case MoqtMessageType::kPublishNamespaceDone:
      return "PUBLISH_NAMESPACE_DONE";
    case MoqtMessageType::kGoAway:
      return "GOAWAY";
    case MoqtMessageType::kSubscribeNamespace:
      return "SUBSCRIBE_NAMESPACE";
    case MoqtMessageType::kMaxRequestId:
      return "MAX_REQUEST_ID";
    case MoqtMessageType::kPublish:
      return "PUBLISH";
    case MoqtMessageType::kPublishOk:
      return "PUBLISH_OK";
    case MoqtMessageType::kFetch:
      return "FETCH";
    case MoqtMessageType::kFetchCancel:
      return "FETCH_CANCEL";
    case MoqtMessageType::kFetchOk:
      return "FETCH_OK";
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

std::string MoqtFetchSerializationToString(MoqtFetchSerialization type) {
  return absl::StrCat("FETCH_SERIALIZATION_", type.value());
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

}  // namespace moqt
