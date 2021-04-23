// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/masque/masque_utils.h"

namespace quic {

ParsedQuicVersionVector MasqueSupportedVersions() {
  QuicVersionInitializeSupportForIetfDraft();
  ParsedQuicVersion version = UnsupportedQuicVersion();
  for (const ParsedQuicVersion& vers : AllSupportedVersions()) {
    // Find the first version that supports IETF QUIC.
    if (vers.HasIetfQuicFrames() && vers.UsesTls()) {
      version = vers;
      break;
    }
  }
  QUICHE_CHECK(version.IsKnown());
  QuicEnableVersion(version);
  return {version};
}

QuicConfig MasqueEncapsulatedConfig() {
  QuicConfig config;
  config.SetMaxPacketSizeToSend(kMasqueMaxEncapsulatedPacketSize);
  return config;
}

}  // namespace quic
