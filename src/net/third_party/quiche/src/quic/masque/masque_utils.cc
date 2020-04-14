// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_utils.h"

namespace quic {

ParsedQuicVersionVector MasqueSupportedVersions() {
  QuicVersionInitializeSupportForIetfDraft();
  ParsedQuicVersion version(PROTOCOL_TLS1_3, QUIC_VERSION_99);
  QuicEnableVersion(version);
  return {version};
}

QuicConfig MasqueEncapsulatedConfig() {
  QuicConfig config;
  config.SetMaxPacketSizeToSend(kMasqueMaxEncapsulatedPacketSize);
  return config;
}

}  // namespace quic
