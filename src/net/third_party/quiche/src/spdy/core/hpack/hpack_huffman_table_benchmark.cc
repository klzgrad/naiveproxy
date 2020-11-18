// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// $ blaze run -c opt --dynamic_mode=off \
//     -- //net/third_party/quiche/src/spdy/core/hpack:hpack_huffman_table_benchmark \
//     --benchmarks=all --benchmark_memory_usage --benchmark_repetitions=1
//
// Benchmark                   Time(ns)  CPU(ns) Allocs Iterations
// -----------------------------------------------------------------------------
// BM_EncodeSmallStrings            463        441  0  100000   0.000B  peak-mem
// BM_EncodeLargeString/1k         9003       9069  5    4861   1.125kB peak-mem
// BM_EncodeLargeString/4k        34808      35157  7    1597   4.500kB peak-mem
// BM_EncodeLargeString/32k      275973     270741 10     207  36.000kB peak-mem
// BM_EncodeLargeString/256k    2234748    2236850 13      29 288.000kB peak-mem
// BM_EncodeLargeString/2M     18248449   18717995 16       3   2.250MB peak-mem
// BM_EncodeLargeString/16M   144944895  144415061 19       1  18.000MB peak-mem
// BM_EncodeLargeString/128M 1200907841 1207238809 86       1 144.009MB peak-mem
//

#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
// This header has multiple DCHECK_* macros with signed-unsigned comparison.
#include "testing/base/public/benchmark.h"
#pragma clang diagnostic pop

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_huffman_table.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"

namespace spdy {
namespace {

void BM_EncodeSmallStrings(benchmark::State& state) {
  const HpackHuffmanTable& table = ObtainHpackHuffmanTable();
  const std::vector<const std::string> inputs{
      ":method", ":path", "cookie", "set-cookie", "vary", "accept-encoding"};
  for (auto s : state) {
    for (const auto& input : inputs) {
      HpackOutputStream output_stream;
      table.EncodedSize(input);
      table.EncodeString(input, &output_stream);
    }
  }
}

void BM_EncodeLargeString(benchmark::State& state) {
  const HpackHuffmanTable& table = ObtainHpackHuffmanTable();
  const std::string input(state.range(0), 'a');
  for (auto s : state) {
    HpackOutputStream output_stream;
    table.EncodedSize(input);
    table.EncodeString(input, &output_stream);
  }
}

BENCHMARK(BM_EncodeSmallStrings);
BENCHMARK(BM_EncodeLargeString)->Range(1024, 128 * 1024 * 1024);

}  // namespace
}  // namespace spdy
