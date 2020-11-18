// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_

#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"

namespace quic {

// List of QUIC versions that support MASQUE. Currently restricted to IETF QUIC.
QUIC_NO_EXPORT ParsedQuicVersionVector MasqueSupportedVersions();

// Default QuicConfig for use with MASQUE. Sets a custom max_packet_size.
QUIC_NO_EXPORT QuicConfig MasqueEncapsulatedConfig();

// Maximum packet size for encapsulated connections.
const QuicByteCount kMasqueMaxEncapsulatedPacketSize = 1300;

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
