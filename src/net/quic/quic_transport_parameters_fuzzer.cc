// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <fuzzer/FuzzedDataProvider.h>

#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);
  auto perspective = data_provider.ConsumeBool() ? quic::Perspective::IS_CLIENT
                                                 : quic::Perspective::IS_SERVER;
  quic::TransportParameters transport_parameters;
  std::vector<uint8_t> remaining_bytes =
      data_provider.ConsumeRemainingBytes<uint8_t>();
  quic::ParsedQuicVersion version = quic::UnsupportedQuicVersion();
  for (const quic::ParsedQuicVersion& vers : quic::AllSupportedVersions()) {
    if (vers.handshake_protocol == quic::PROTOCOL_TLS1_3) {
      version = vers;
      break;
    }
  }
  CHECK_NE(version.transport_version, quic::QUIC_VERSION_UNSUPPORTED);
  std::string error_details;
  quic::ParseTransportParameters(version, perspective, remaining_bytes.data(),
                                 remaining_bytes.size(), &transport_parameters,
                                 &error_details);
  return 0;
}
