// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_utils.h"

namespace quic {

ParsedQuicVersionVector MasqueSupportedVersions() {
  QuicVersionInitializeSupportForIetfDraft();
  ParsedQuicVersion version = UnsupportedQuicVersion();
  for (const ParsedQuicVersion& vers : AllSupportedVersions()) {
    // Find the first version that supports IETF QUIC.
    if (vers.HasIetfQuicFrames() &&
        vers.handshake_protocol == quic::PROTOCOL_TLS1_3) {
      version = vers;
      break;
    }
  }
  CHECK_NE(version.transport_version, QUIC_VERSION_UNSUPPORTED);
  QuicEnableVersion(version);
  return {version};
}

QuicConfig MasqueEncapsulatedConfig() {
  QuicConfig config;
  config.SetMaxPacketSizeToSend(kMasqueMaxEncapsulatedPacketSize);
  return config;
}

}  // namespace quic
