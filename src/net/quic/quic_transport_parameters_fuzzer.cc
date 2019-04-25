// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/test/fuzzed_data_provider.h"
#include "net/third_party/quic/core/crypto/transport_parameters.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  base::FuzzedDataProvider data_provider(data, size);
  auto perspective = data_provider.ConsumeBool() ? quic::Perspective::IS_CLIENT
                                                 : quic::Perspective::IS_SERVER;
  quic::TransportParameters transport_parameters;
  std::vector<uint8_t> remaining_bytes = data_provider.ConsumeRemainingBytes();
  quic::ParseTransportParameters(remaining_bytes.data(), remaining_bytes.size(),
                                 perspective, &transport_parameters);
  return 0;
}
