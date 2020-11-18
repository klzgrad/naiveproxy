// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// $ blaze run -c opt --dynamic_mode=off \
//     -- //net/third_party/quiche/src/http2/hpack/huffman:hpack_huffman_encoder_benchmark \
//     --benchmarks=all --benchmark_memory_usage --benchmark_repetitions=1
//
// Benchmark                  Time(ns)  CPU(ns) Allocs Iterations
// -----------------------------------------------------------------------------
// BM_EncodeSmallStrings           239       239    0 2456085   0.000B  peak-mem
// BM_EncodeLargeString/1k        4560      4561    5  153325   1.125kB peak-mem
// BM_EncodeLargeString/4k       18787     18788    7   38430   4.500kB peak-mem
// BM_EncodeLargeString/32k     147680    147657   10    4664  36.000kB peak-mem
// BM_EncodeLargeString/256k   1161688   1161511   13     601 288.000kB peak-mem
// BM_EncodeLargeString/2M    10042722  10036764   16      75   2.250MB peak-mem
// BM_EncodeLargeString/16M   76127338  76138839   19       9  18.000MB peak-mem
// BM_EncodeLargeString/128M 640008098 640154859   22       1 144.000MB peak-mem
//

#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
// This header has multiple DCHECK_* macros with signed-unsigned comparison.
#include "testing/base/public/benchmark.h"
#pragma clang diagnostic pop

#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_encoder.h"

namespace http2 {
namespace {

void BM_EncodeSmallStrings(benchmark::State& state) {
  const std::vector<const std::string> inputs{
      ":method", ":path", "cookie", "set-cookie", "vary", "accept-encoding"};
  for (auto s : state) {
    for (const auto& input : inputs) {
      std::string result;
      ExactHuffmanSize(input);
      HuffmanEncode(input, &result);
    }
  }
}

void BM_EncodeLargeString(benchmark::State& state) {
  const std::string input(state.range(0), 'a');
  for (auto s : state) {
    std::string result;
    ExactHuffmanSize(input);
    HuffmanEncode(input, &result);
  }
}

BENCHMARK(BM_EncodeSmallStrings);
BENCHMARK(BM_EncodeLargeString)->Range(1024, 128 * 1024 * 1024);

}  // namespace
}  // namespace http2
