// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Represents HTTP priorities as defined by RFC 9218.
struct QUICHE_EXPORT HttpStreamPriority {
  static constexpr int kMinimumUrgency = 0;
  static constexpr int kMaximumUrgency = 7;
  static constexpr int kDefaultUrgency = 3;
  static constexpr bool kDefaultIncremental = false;

  // Parameter names for Priority Field Value.
  static constexpr absl::string_view kUrgencyKey = "u";
  static constexpr absl::string_view kIncrementalKey = "i";

  int urgency = kDefaultUrgency;
  bool incremental = kDefaultIncremental;

  bool operator==(const HttpStreamPriority& other) const {
    return std::tie(urgency, incremental) ==
           std::tie(other.urgency, other.incremental);
  }

  bool operator!=(const HttpStreamPriority& other) const {
    return !(*this == other);
  }
};

// Represents the priorities of WebTransport nested data streams as defined in
// <https://w3c.github.io/webtransport/>.
struct QUICHE_EXPORT WebTransportStreamPriority {
  // The stream ID of the control stream for the WebTransport session to which
  // this data stream belongs.
  QuicStreamId session_id = 0;
  // Number of the send group with which the stream is associated; see
  // https://w3c.github.io/webtransport/#dom-webtransportsendstreamoptions-sendgroup
  uint64_t send_group_number = 0;
  // https://w3c.github.io/webtransport/#dom-webtransportsendstreamoptions-sendorder
  webtransport::SendOrder send_order = 0;

  bool operator==(const WebTransportStreamPriority& other) const {
    return session_id == other.session_id &&
           send_group_number == other.send_group_number &&
           send_order == other.send_order;
  }
  bool operator!=(const WebTransportStreamPriority& other) const {
    return !(*this == other);
  }
};

// A class that wraps different types of priorities that can be used for
// scheduling QUIC streams.
class QUICHE_EXPORT QuicStreamPriority {
 public:
  QuicStreamPriority() : value_(HttpStreamPriority()) {}
  explicit QuicStreamPriority(HttpStreamPriority priority) : value_(priority) {}
  explicit QuicStreamPriority(WebTransportStreamPriority priority)
      : value_(priority) {}

  QuicPriorityType type() const { return absl::visit(TypeExtractor(), value_); }

  HttpStreamPriority http() const {
    if (absl::holds_alternative<HttpStreamPriority>(value_)) {
      return absl::get<HttpStreamPriority>(value_);
    }
    QUICHE_BUG(invalid_priority_type_http)
        << "Tried to access HTTP priority for a priority type" << type();
    return HttpStreamPriority();
  }
  WebTransportStreamPriority web_transport() const {
    if (absl::holds_alternative<WebTransportStreamPriority>(value_)) {
      return absl::get<WebTransportStreamPriority>(value_);
    }
    QUICHE_BUG(invalid_priority_type_wt)
        << "Tried to access WebTransport priority for a priority type"
        << type();
    return WebTransportStreamPriority();
  }

  bool operator==(const QuicStreamPriority& other) const {
    return value_ == other.value_;
  }

 private:
  struct TypeExtractor {
    QuicPriorityType operator()(const HttpStreamPriority&) {
      return QuicPriorityType::kHttp;
    }
    QuicPriorityType operator()(const WebTransportStreamPriority&) {
      return QuicPriorityType::kWebTransport;
    }
  };

  absl::variant<HttpStreamPriority, WebTransportStreamPriority> value_;
};

// Serializes the Priority Field Value for a PRIORITY_UPDATE frame.
QUICHE_EXPORT std::string SerializePriorityFieldValue(
    HttpStreamPriority priority);

// Parses the Priority Field Value field of a PRIORITY_UPDATE frame.
// Returns nullopt on failure.
QUICHE_EXPORT std::optional<HttpStreamPriority> ParsePriorityFieldValue(
    absl::string_view priority_field_value);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_
