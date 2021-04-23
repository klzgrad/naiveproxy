// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_

#include <cstdint>
#include <string>

#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// Unidirectional stream types.

// https://quicwg.org/base-drafts/draft-ietf-quic-http.html#unidirectional-streams
const uint64_t kControlStream = 0x00;
const uint64_t kServerPushStream = 0x01;
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#enc-dec-stream-def
const uint64_t kQpackEncoderStream = 0x02;
const uint64_t kQpackDecoderStream = 0x03;

// This includes control stream, QPACK encoder stream, and QPACK decoder stream.
const QuicStreamCount kHttp3StaticUnidirectionalStreamCount = 3;

// HTTP/3 and QPACK settings identifiers.
// https://quicwg.org/base-drafts/draft-ietf-quic-http.html#settings-parameters
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#configuration
enum Http3AndQpackSettingsIdentifiers : uint64_t {
  // Same value as spdy::SETTINGS_HEADER_TABLE_SIZE.
  SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x01,
  // Same value as spdy::SETTINGS_MAX_HEADER_LIST_SIZE.
  SETTINGS_MAX_FIELD_SECTION_SIZE = 0x06,
  SETTINGS_QPACK_BLOCKED_STREAMS = 0x07,
};

// Returns HTTP/3 SETTINGS identifier as a string.
QUIC_EXPORT std::string H3SettingsToString(
    Http3AndQpackSettingsIdentifiers identifier);

// Default maximum dynamic table capacity, communicated via
// SETTINGS_QPACK_MAX_TABLE_CAPACITY.
const QuicByteCount kDefaultQpackMaxDynamicTableCapacity = 64 * 1024;  // 64 KB

// Default limit on the size of uncompressed headers,
// communicated via SETTINGS_MAX_HEADER_LIST_SIZE.
const QuicByteCount kDefaultMaxUncompressedHeaderSize = 16 * 1024;  // 16 KB

// Default limit on number of blocked streams, communicated via
// SETTINGS_QPACK_BLOCKED_STREAMS.
const uint64_t kDefaultMaximumBlockedStreams = 100;

const char kUserAgentHeaderName[] = "user-agent";

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
