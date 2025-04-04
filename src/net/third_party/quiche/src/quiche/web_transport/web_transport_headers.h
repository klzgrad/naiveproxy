// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_HEADERS_H_
#define QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_HEADERS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace webtransport {

inline constexpr absl::string_view kSubprotocolRequestHeader =
    "WT-Available-Protocols";
inline constexpr absl::string_view kSubprotocolResponseHeader = "WT-Protocol";

QUICHE_EXPORT absl::StatusOr<std::vector<std::string>>
ParseSubprotocolRequestHeader(absl::string_view value);
QUICHE_EXPORT absl::StatusOr<std::string> SerializeSubprotocolRequestHeader(
    absl::Span<const std::string> subprotocols);
QUICHE_EXPORT absl::StatusOr<std::string> ParseSubprotocolResponseHeader(
    absl::string_view value);
QUICHE_EXPORT absl::StatusOr<std::string> SerializeSubprotocolResponseHeader(
    absl::string_view subprotocol);

inline constexpr absl::string_view kInitHeader = "WebTransport-Init";

// A deserialized representation of WebTransport-Init header that is used to
// indicate the initial stream flow control windows in WebTransport over HTTP/2.
// Specification:
// https://www.ietf.org/archive/id/draft-ietf-webtrans-http2-07.html#name-flow-control-header-field
struct QUICHE_EXPORT WebTransportInitHeader {
  // Initial flow control window for unidirectional streams opened by the
  // header's recipient.
  uint64_t initial_unidi_limit = 0;
  // Initial flow control window for bidirectional streams opened by the
  // header's recipient.
  uint64_t initial_incoming_bidi_limit = 0;
  // Initial flow control window for bidirectional streams opened by the
  // header's sender.
  uint64_t initial_outgoing_bidi_limit = 0;

  bool operator==(const WebTransportInitHeader& other) const {
    return initial_unidi_limit == other.initial_unidi_limit &&
           initial_incoming_bidi_limit == other.initial_incoming_bidi_limit &&
           initial_outgoing_bidi_limit == other.initial_outgoing_bidi_limit;
  }
};

QUICHE_EXPORT absl::StatusOr<WebTransportInitHeader> ParseInitHeader(
    absl::string_view header);
QUICHE_EXPORT absl::StatusOr<std::string> SerializeInitHeader(
    const WebTransportInitHeader& header);

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_HEADERS_H_
