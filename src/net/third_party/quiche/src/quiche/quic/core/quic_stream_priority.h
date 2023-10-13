// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_

#include <cstdint>
#include <string>
#include <tuple>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"

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

// Represents WebTransport priorities as defined by
// <https://w3c.github.io/webtransport/>.
struct QUICHE_EXPORT WebTransportStreamPriority {
  enum class StreamType : uint8_t {
    // WebTransport data streams.
    kData = 0,
    // Regular HTTP traffic. Since we're currently only supporting dedicated
    // HTTP/3 transport, this means that all HTTP traffic is control traffic,
    // and thus should always go first.
    kHttp = 1,
    // Streams that the QUIC stack declares as static.
    kStatic = 2,
  };

  // Allows prioritizing control streams over the data streams.
  StreamType stream_type = StreamType::kData;
  // https://w3c.github.io/webtransport/#dom-webtransportsendstreamoptions-sendorder
  int64_t send_order = 0;

  bool operator==(const WebTransportStreamPriority& other) const {
    return stream_type == other.stream_type && send_order == other.send_order;
  }
  bool operator!=(const WebTransportStreamPriority& other) const {
    return !(*this == other);
  }
};

// A class that wraps different types of priorities that can be used for
// scheduling QUIC streams.
class QUICHE_EXPORT QuicStreamPriority {
 public:
  explicit QuicStreamPriority(HttpStreamPriority priority) : value_(priority) {}
  explicit QuicStreamPriority(WebTransportStreamPriority priority)
      : value_(priority) {}

  static QuicStreamPriority Default(QuicPriorityType type) {
    switch (type) {
      case QuicPriorityType::kHttp:
        return QuicStreamPriority(HttpStreamPriority());
      case QuicPriorityType::kWebTransport:
        return QuicStreamPriority(WebTransportStreamPriority());
    }

    QUICHE_BUG(unhandled_quic_priority_type_518918225)
        << "Tried to create QuicStreamPriority for unknown QuicPriorityType "
        << type;
    return QuicStreamPriority(HttpStreamPriority());
  }

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
QUICHE_EXPORT absl::optional<HttpStreamPriority> ParsePriorityFieldValue(
    absl::string_view priority_field_value);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_PRIORITY_H_
