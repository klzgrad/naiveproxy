// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/web_transport_headers.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/common/structured_headers.h"

namespace webtransport {

namespace {
using ::quiche::structured_headers::Dictionary;
using ::quiche::structured_headers::DictionaryMember;
using ::quiche::structured_headers::Item;
using ::quiche::structured_headers::ItemTypeToString;
using ::quiche::structured_headers::List;
using ::quiche::structured_headers::ParameterizedItem;
using ::quiche::structured_headers::ParameterizedMember;

absl::Status CheckItemType(const ParameterizedItem& item,
                           Item::ItemType expected_type) {
  if (item.item.Type() != expected_type) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Expected all members to be of type ", ItemTypeToString(expected_type),
        ", found ", ItemTypeToString(item.item.Type()), " instead"));
  }
  return absl::OkStatus();
}
absl::Status CheckMemberType(const ParameterizedMember& member,
                             Item::ItemType expected_type) {
  if (member.member_is_inner_list || member.member.size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Expected all members to be of type", ItemTypeToString(expected_type),
        ", found a nested list instead"));
  }
  return CheckItemType(member.member[0], expected_type);
}

ABSL_CONST_INIT std::array kInitHeaderFields{
    std::make_pair("u", &WebTransportInitHeader::initial_unidi_limit),
    std::make_pair("bl", &WebTransportInitHeader::initial_incoming_bidi_limit),
    std::make_pair("br", &WebTransportInitHeader::initial_outgoing_bidi_limit),
};
}  // namespace

absl::StatusOr<std::vector<std::string>> ParseSubprotocolRequestHeader(
    absl::string_view value) {
  std::optional<List> parsed = quiche::structured_headers::ParseList(value);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError(
        "Failed to parse the header as an sf-list");
  }

  std::vector<std::string> result;
  result.reserve(parsed->size());
  for (ParameterizedMember& member : *parsed) {
    QUICHE_RETURN_IF_ERROR(CheckMemberType(member, Item::kTokenType));
    result.push_back(std::move(member.member[0].item).TakeString());
  }
  return result;
}

absl::StatusOr<std::string> SerializeSubprotocolRequestHeader(
    absl::Span<const std::string> subprotocols) {
  // Serialize tokens manually via a simple StrJoin call; this lets us provide
  // better error messages, and is probably more efficient too.
  for (const std::string& token : subprotocols) {
    if (!quiche::structured_headers::IsValidToken(token)) {
      return absl::InvalidArgumentError(absl::StrCat("Invalid token: ", token));
    }
  }
  return absl::StrJoin(subprotocols, ", ");
}

absl::StatusOr<std::string> ParseSubprotocolResponseHeader(
    absl::string_view value) {
  std::optional<ParameterizedItem> parsed =
      quiche::structured_headers::ParseItem(value);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError("Failed to parse sf-item");
  }
  QUICHE_RETURN_IF_ERROR(CheckItemType(*parsed, Item::kTokenType));
  return std::move(parsed->item).TakeString();
}

absl::StatusOr<std::string> SerializeSubprotocolResponseHeader(
    absl::string_view subprotocol) {
  if (!quiche::structured_headers::IsValidToken(subprotocol)) {
    return absl::InvalidArgumentError("Invalid token value supplied");
  }
  return std::string(subprotocol);
}

absl::StatusOr<WebTransportInitHeader> ParseInitHeader(
    absl::string_view header) {
  std::optional<Dictionary> parsed =
      quiche::structured_headers::ParseDictionary(header);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError(
        "Failed to parse WebTransport-Init header as an sf-dictionary");
  }
  WebTransportInitHeader output;
  for (const auto& [field_name_a, field_value] : *parsed) {
    for (const auto& [field_name_b, field_accessor] : kInitHeaderFields) {
      if (field_name_a != field_name_b) {
        continue;
      }
      QUICHE_RETURN_IF_ERROR(CheckMemberType(field_value, Item::kIntegerType));
      int64_t value = field_value.member[0].item.GetInteger();
      if (value < 0) {
        return absl::InvalidArgumentError(
            absl::StrCat("Received negative value for ", field_name_a));
      }
      output.*field_accessor = value;
    }
  }
  return output;
}

absl::StatusOr<std::string> SerializeInitHeader(
    const WebTransportInitHeader& header) {
  std::vector<DictionaryMember> members;
  members.reserve(kInitHeaderFields.size());
  for (const auto& [field_name, field_accessor] : kInitHeaderFields) {
    Item item(static_cast<int64_t>(header.*field_accessor));
    members.push_back(std::make_pair(
        field_name, ParameterizedMember({ParameterizedItem(item, {})}, false,
                                        /*parameters=*/{})));
  }
  std::optional<std::string> result =
      quiche::structured_headers::SerializeDictionary(
          Dictionary(std::move(members)));
  if (!result.has_value()) {
    return absl::InternalError("Failed to serialize the dictionary");
  }
  return *std::move(result);
}

}  // namespace webtransport
