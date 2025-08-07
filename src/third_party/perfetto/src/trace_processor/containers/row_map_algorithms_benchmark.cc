/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <random>

#include <benchmark/benchmark.h>

#include "src/trace_processor/containers/row_map_algorithms.h"

using perfetto::trace_processor::BitVector;

namespace {

using namespace perfetto::trace_processor::row_map_algorithms;

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

BitVector BvWithSetBits(uint32_t bv_set_bits) {
  static constexpr uint32_t kRandomSeed = 29;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  BitVector bv;
  for (uint32_t i = 0; i < bv_set_bits;) {
    if (rnd_engine() % 2 == 0) {
      bv.AppendTrue();
      ++i;
    } else {
      bv.AppendFalse();
    }
  }
  return bv;
}

std::vector<uint32_t> IvWithSizeAndMaxIndex(
    uint32_t bv_set_bits,
    uint32_t set_bit_to_selector_ratio) {
  static constexpr uint32_t kRandomSeed = 78;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = bv_set_bits / set_bit_to_selector_ratio;
  std::vector<uint32_t> indices(size);
  for (uint32_t i = 0; i < size; ++i) {
    indices[i] = rnd_engine() % bv_set_bits;
  }
  return indices;
}

void BvWithIvArgs(benchmark::internal::Benchmark* b) {
  std::vector<int> set_bit_to_selector_ratios;
  if (IsBenchmarkFunctionalOnly()) {
    set_bit_to_selector_ratios = std::vector<int>{2};
  } else {
    set_bit_to_selector_ratios = std::vector<int>{2, 4, 6, 8, 10, 12, 16, 32};
  }

  std::vector<int> bv_set_bits;
  if (IsBenchmarkFunctionalOnly()) {
    bv_set_bits = std::vector<int>{1024};
  } else {
    bv_set_bits = std::vector<int>{1024, 4096, 1024 * 1024};
  }

  for (int bv_set_bit : bv_set_bits) {
    for (int set_bit_to_selector_ratio : set_bit_to_selector_ratios) {
      b->Args({bv_set_bit, set_bit_to_selector_ratio});
    }
  }
}

// |BM_SelectBvWithIvByConvertToIv| and |BM_SelectBvWithIvByIndexOfNthSet| are
// used together to find the ratio between the selector size and the number of
// set bits in the BitVector where |SelectBvWithIvByIndexOfNthSet| is faster
// than |SelectBvWithIvByConvertToIv|.
//
// See the comment in SelectBvWithIv in row_map.cc for more information on this.

static void BM_SelectBvWithIvByConvertToIv(benchmark::State& state) {
  uint32_t bv_set_bit = static_cast<uint32_t>(state.range(0));
  uint32_t set_bit_to_selector_ratio = static_cast<uint32_t>(state.range(1));

  BitVector bv = BvWithSetBits(bv_set_bit);
  std::vector<uint32_t> iv =
      IvWithSizeAndMaxIndex(bv_set_bit, set_bit_to_selector_ratio);

  for (auto _ : state) {
    benchmark::DoNotOptimize(SelectBvWithIvByConvertToIv(bv, iv));
  }
}
BENCHMARK(BM_SelectBvWithIvByConvertToIv)->Apply(BvWithIvArgs);

static void BM_SelectBvWithIvByIndexOfNthSet(benchmark::State& state) {
  uint32_t bv_set_bit = static_cast<uint32_t>(state.range(0));
  uint32_t set_bit_to_selector_ratio = static_cast<uint32_t>(state.range(1));

  BitVector bv = BvWithSetBits(bv_set_bit);
  std::vector<uint32_t> iv =
      IvWithSizeAndMaxIndex(bv_set_bit, set_bit_to_selector_ratio);

  for (auto _ : state) {
    benchmark::DoNotOptimize(SelectBvWithIvByIndexOfNthSet(bv, iv));
  }
}
BENCHMARK(BM_SelectBvWithIvByIndexOfNthSet)->Apply(BvWithIvArgs);

}  // namespace
