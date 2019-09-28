// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_

#include <cstdint>

namespace quic {

// Unidirectional stream types.

// https://quicwg.org/base-drafts/draft-ietf-quic-http.html#unidirectional-streams
const uint64_t kControlStream = 0x00;
const uint64_t kServerPushStream = 0x01;
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#enc-dec-stream-def
const uint64_t kQpackEncoderStream = 0x02;
const uint64_t kQpackDecoderStream = 0x03;

// Settings identifiers.

// https://quicwg.org/base-drafts/draft-ietf-quic-http.html#settings-parameters
const uint64_t kSettingsMaxHeaderListSize = 0x06;
const uint64_t kSettingsNumPlaceholders = 0x09;
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#configuration
const uint64_t kSettingsQpackMaxTableCapacity = 0x01;
const uint64_t kSettingsQpackBlockedStream = 0x07;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_CONSTANTS_H_
