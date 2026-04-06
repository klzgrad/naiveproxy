// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Unidirectional stream types.
enum : uint64_t {
  // https://quicwg.org/base-drafts/draft-ietf-quic-http.html#unidirectional-streams
  kControlStream = 0x00,
  kServerPushStream = 0x01,
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#enc-dec-stream-def
  kQpackEncoderStream = 0x02,
  kQpackDecoderStream = 0x03,
  // https://ietf-wg-webtrans.github.io/draft-ietf-webtrans-http3/draft-ietf-webtrans-http3.html#name-unidirectional-streams
  kWebTransportUnidirectionalStream = 0x54,
};

// This includes control stream, QPACK encoder stream, and QPACK decoder stream.
enum : QuicStreamCount { kHttp3StaticUnidirectionalStreamCount = 3 };

// HTTP/3 and QPACK settings identifiers.
// https://quicwg.org/base-drafts/draft-ietf-quic-http.html#settings-parameters
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#configuration
enum Http3AndQpackSettingsIdentifiers : uint64_t {
  // Same value as spdy::SETTINGS_HEADER_TABLE_SIZE.
  SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x01,
  // Same value as spdy::SETTINGS_MAX_HEADER_LIST_SIZE.
  SETTINGS_MAX_FIELD_SECTION_SIZE = 0x06,
  SETTINGS_QPACK_BLOCKED_STREAMS = 0x07,
  // draft-ietf-masque-h3-datagram-04.
  SETTINGS_H3_DATAGRAM_DRAFT04 = 0xffd277,
  // RFC 9297.
  SETTINGS_H3_DATAGRAM = 0x33,
  // draft-ietf-webtrans-http3
  SETTINGS_WEBTRANS_DRAFT00 = 0x2b603742,
  SETTINGS_WEBTRANS_MAX_SESSIONS_DRAFT07 = 0xc671706a,
  // draft-ietf-httpbis-h3-websockets
  SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x08,
  SETTINGS_ENABLE_METADATA = 0x4d44,
};

// Returns HTTP/3 SETTINGS identifier as a string.
QUICHE_EXPORT std::string H3SettingsToString(
    Http3AndQpackSettingsIdentifiers identifier);

// Default maximum dynamic table capacity, communicated via
// SETTINGS_QPACK_MAX_TABLE_CAPACITY.
enum : QuicByteCount {
  kDefaultQpackMaxDynamicTableCapacity = 64 * 1024  // 64 KB
};

// Default limit on the size of uncompressed headers,
// communicated via SETTINGS_MAX_HEADER_LIST_SIZE.
enum : QuicByteCount {
  kDefaultMaxUncompressedHeaderSize = 16 * 1024  // 16 KB
};

// Default limit on number of blocked streams, communicated via
// SETTINGS_QPACK_BLOCKED_STREAMS.
enum : uint64_t { kDefaultMaximumBlockedStreams = 100 };

ABSL_CONST_INIT QUICHE_EXPORT extern const absl::string_view
    kUserAgentHeaderName;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
