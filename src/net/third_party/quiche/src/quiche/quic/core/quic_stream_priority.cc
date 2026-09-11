// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream_priority.h"

#include <optional>
#include <string>
#include <vector>

#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/structured_headers.h"

namespace quic {

std::string SerializePriorityFieldValue(HttpStreamPriority priority) {
  quiche::structured_headers::Dictionary dictionary;

  if (priority.urgency != HttpStreamPriority::kDefaultUrgency &&
      priority.urgency >= HttpStreamPriority::kMinimumUrgency &&
      priority.urgency <= HttpStreamPriority::kMaximumUrgency) {
    dictionary[HttpStreamPriority::kUrgencyKey] =
        quiche::structured_headers::ParameterizedMember(
            quiche::structured_headers::Item(
                static_cast<int64_t>(priority.urgency)),
            {});
  }

  if (priority.incremental != HttpStreamPriority::kDefaultIncremental) {
    dictionary[HttpStreamPriority::kIncrementalKey] =
        quiche::structured_headers::ParameterizedMember(
            quiche::structured_headers::Item(priority.incremental), {});
  }

  std::optional<std::string> priority_field_value =
      quiche::structured_headers::SerializeDictionary(dictionary);
  if (!priority_field_value.has_value()) {
    QUICHE_BUG(priority_field_value_serialization_failed);
    return "";
  }

  return *std::move(priority_field_value);
}

std::optional<HttpStreamPriority> ParsePriorityFieldValue(
    absl::string_view priority_field_value) {
  std::optional<quiche::structured_headers::Dictionary> parsed_dictionary =
      quiche::structured_headers::ParseDictionary(priority_field_value);
  if (!parsed_dictionary.has_value()) {
    return std::nullopt;
  }

  uint8_t urgency = HttpStreamPriority::kDefaultUrgency;
  bool incremental = HttpStreamPriority::kDefaultIncremental;

  for (const auto& [name, value] : *parsed_dictionary) {
    const std::optional<
        std::pair<const quiche::structured_headers::Item&,
                  const quiche::structured_headers::Parameters&>>
        item_and_params = value.GetWithParamsIfItem();
    if (!item_and_params.has_value()) {
      continue;
    }

    const quiche::structured_headers::Item& item = item_and_params->first;
    if (name == HttpStreamPriority::kUrgencyKey) {
      const int64_t* parsed_urgency = item.GetIfInteger();
      // Ignore out-of-range values.
      if (parsed_urgency &&
          *parsed_urgency >= HttpStreamPriority::kMinimumUrgency &&
          *parsed_urgency <= HttpStreamPriority::kMaximumUrgency) {
        urgency = *parsed_urgency;
      }
    } else if (name == HttpStreamPriority::kIncrementalKey) {
      if (const bool* parsed_incremental = item.GetIfBoolean()) {
        incremental = *parsed_incremental;
      }
    }
  }

  return HttpStreamPriority{urgency, incremental};
}

}  // namespace quic
