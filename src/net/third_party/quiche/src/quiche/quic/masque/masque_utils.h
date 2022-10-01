// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_

#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"

namespace quic {

// List of QUIC versions that support MASQUE. Currently restricted to IETF QUIC.
QUIC_NO_EXPORT ParsedQuicVersionVector MasqueSupportedVersions();

// Default QuicConfig for use with MASQUE. Sets a custom max_packet_size.
QUIC_NO_EXPORT QuicConfig MasqueEncapsulatedConfig();

// Maximum packet size for encapsulated connections.
enum : QuicByteCount {
  kMasqueMaxEncapsulatedPacketSize = 1300,
  kMasqueMaxOuterPacketSize = 1350,
};

// Mode that MASQUE is operating in.
enum class MasqueMode : uint8_t {
  kInvalid = 0,  // Should never be used.
  kOpen = 2,  // Open mode uses the MASQUE HTTP CONNECT-UDP method as documented
  // in <https://tools.ietf.org/html/draft-ietf-masque-connect-udp>. This mode
  // allows unauthenticated clients (a more restricted mode will be added to
  // this enum at a later date).
};

QUIC_NO_EXPORT std::string MasqueModeToString(MasqueMode masque_mode);
QUIC_NO_EXPORT std::ostream& operator<<(std::ostream& os,
                                        const MasqueMode& masque_mode);

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
