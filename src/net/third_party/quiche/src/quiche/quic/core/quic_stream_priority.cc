// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream_priority.h"

#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/structured_headers.h"

namespace quic {

std::string SerializePriorityFieldValue(QuicStreamPriority priority) {
  quiche::structured_headers::Dictionary dictionary;

  // TODO(b/266722347): Never send `urgency` if value equals default value.
  if ((priority.urgency != QuicStreamPriority::kDefaultUrgency ||
       priority.incremental != QuicStreamPriority::kDefaultIncremental) &&
      priority.urgency >= QuicStreamPriority::kMinimumUrgency &&
      priority.urgency <= QuicStreamPriority::kMaximumUrgency) {
    dictionary[QuicStreamPriority::kUrgencyKey] =
        quiche::structured_headers::ParameterizedMember(
            quiche::structured_headers::Item(
                static_cast<int64_t>(priority.urgency)),
            {});
  }

  if (priority.incremental != QuicStreamPriority::kDefaultIncremental) {
    dictionary[QuicStreamPriority::kIncrementalKey] =
        quiche::structured_headers::ParameterizedMember(
            quiche::structured_headers::Item(priority.incremental), {});
  }

  absl::optional<std::string> priority_field_value =
      quiche::structured_headers::SerializeDictionary(dictionary);
  if (!priority_field_value.has_value()) {
    QUICHE_BUG(priority_field_value_serialization_failed);
    return "";
  }

  return *priority_field_value;
}

absl::optional<QuicStreamPriority> ParsePriorityFieldValue(
    absl::string_view priority_field_value) {
  absl::optional<quiche::structured_headers::Dictionary> parsed_dictionary =
      quiche::structured_headers::ParseDictionary(priority_field_value);
  if (!parsed_dictionary.has_value()) {
    return absl::nullopt;
  }

  uint8_t urgency = QuicStreamPriority::kDefaultUrgency;
  bool incremental = QuicStreamPriority::kDefaultIncremental;

  for (const auto& [name, value] : *parsed_dictionary) {
    if (value.member_is_inner_list) {
      continue;
    }

    const std::vector<quiche::structured_headers::ParameterizedItem>& member =
        value.member;
    if (member.size() != 1) {
      // If `member_is_inner_list` is false above,
      // then `member` should have exactly one element.
      QUICHE_BUG(priority_field_value_parsing_internal_error);
      continue;
    }

    const quiche::structured_headers::Item item = member[0].item;
    if (name == QuicStreamPriority::kUrgencyKey && item.is_integer()) {
      int parsed_urgency = item.GetInteger();
      // Ignore out-of-range values.
      if (parsed_urgency >= QuicStreamPriority::kMinimumUrgency &&
          parsed_urgency <= QuicStreamPriority::kMaximumUrgency) {
        urgency = parsed_urgency;
      }
    } else if (name == QuicStreamPriority::kIncrementalKey &&
               item.is_boolean()) {
      incremental = item.GetBoolean();
    }
  }

  return QuicStreamPriority{urgency, incremental};
}

}  // namespace quic
