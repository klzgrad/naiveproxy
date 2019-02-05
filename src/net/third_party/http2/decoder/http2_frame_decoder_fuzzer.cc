// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/test/fuzzed_data_provider.h"
#include "net/third_party/http2/decoder/http2_frame_decoder.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  base::FuzzedDataProvider fuzzed_data_provider(data, size);
  http2::Http2FrameDecoder decoder;
  while (fuzzed_data_provider.remaining_bytes() > 0) {
    size_t chunk_size = fuzzed_data_provider.ConsumeIntegralInRange(1, 32);
    std::vector<char> chunk =
        fuzzed_data_provider.ConsumeBytes<char>(chunk_size);

    // http2::DecodeBuffer constructor does not accept nullptr buffer.
    if (chunk.data() == nullptr)
      continue;

    http2::DecodeBuffer frame_data(chunk.data(), chunk.size());
    decoder.DecodeFrame(&frame_data);
  }
  return 0;
}
