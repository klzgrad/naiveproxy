// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_PROTOCOL_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_PROTOCOL_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

// The ALPN used by QuicTransport.
QUIC_EXPORT_PRIVATE inline const char* QuicTransportAlpn() {
  return "wq-vvv-01";
}

// The stream ID on which the client indication is sent.
QUIC_EXPORT_PRIVATE constexpr QuicStreamId ClientIndicationStream() {
  return 2;
}

// The maximum allowed size of the client indication.
QUIC_EXPORT_PRIVATE constexpr QuicByteCount ClientIndicationMaxSize() {
  return 65536;
}

// The keys of the fields in the client indication.
enum class QuicTransportClientIndicationKeys : uint16_t {
  kOrigin = 0x0000,
  kPath = 0x0001,
};

// Returns if the specified QUIC version can be used by QuicTransport.
QUIC_EXPORT_PRIVATE constexpr bool IsVersionValidForQuicTransport(
    const ParsedQuicVersion& version) {
  return VersionSupportsMessageFrames(version.transport_version) &&
         VersionHasIetfQuicFrames(version.transport_version) &&
         version.handshake_protocol == PROTOCOL_TLS1_3;
}

// Returns default QUIC version used for QuicTransport.
QUIC_EXPORT_PRIVATE inline ParsedQuicVersion DefaultVersionForQuicTransport() {
  constexpr ParsedQuicVersion version(PROTOCOL_TLS1_3,
                                      QUIC_VERSION_IETF_DRAFT_25);
  static_assert(IsVersionValidForQuicTransport(version),
                "Default QUIC version used by QuicTransport is invalid");
  return version;
}

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_PROTOCOL_H_
