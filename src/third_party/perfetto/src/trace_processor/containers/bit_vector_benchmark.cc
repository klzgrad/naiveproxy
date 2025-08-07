// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/bit_vector.h"

namespace {

using perfetto::trace_processor::BitVector;

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

void BitVectorArgs(benchmark::internal::Benchmark* b) {
  std::vector<int> set_percentages;
  if (IsBenchmarkFunctionalOnly()) {
    set_percentages = std::vector<int>{50};
  } else {
    set_percentages = std::vector<int>{0, 1, 5, 50, 95, 99, 100};
  }

  for (int percentage : set_percentages) {
    b->Args({64, percentage});

    if (!IsBenchmarkFunctionalOnly()) {
      b->Args({512, percentage});
      b->Args({8192, percentage});
      b->Args({123456, percentage});
      b->Args({1234567, percentage});
    }
  }
}

void UpdateSetBitsSelectBitsArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Args({64, 50, 50});
  } else {
    std::vector<int64_t> set_percentages{1, 5, 50, 95, 99};
    b->ArgsProduct({{1234567}, set_percentages, set_percentages});
  }
}

BitVector BvWithSizeAndSetPercentage(uint32_t size, uint32_t set_percentage) {
  static constexpr uint32_t kRandomSeed = 29;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  BitVector bv;
  for (uint32_t i = 0; i < size; ++i) {
    if (rnd_engine() % 100 < set_percentage) {
      bv.AppendTrue();
    } else {
      bv.AppendFalse();
    }
  }
  return bv;
}

}  // namespace

static void BM_BitVectorAppendTrue(benchmark::State& state) {
  BitVector bv;
  for (auto _ : state) {
    bv.AppendTrue();
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BitVectorAppendTrue);

static void BM_BitVectorAppendFalse(benchmark::State& state) {
  BitVector bv;
  for (auto _ : state) {
    bv.AppendFalse();
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BitVectorAppendFalse);

static void BM_BitVectorIsSet(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  BitVector bv = BvWithSizeAndSetPercentage(8192, 50);

  static constexpr uint32_t kPoolSize = 1024 * 1024;
  std::vector<bool> bit_pool(kPoolSize);
  std::vector<uint32_t> row_pool(kPoolSize);
  for (uint32_t i = 0; i < kPoolSize; ++i) {
    bit_pool[i] = rnd_engine() % 2;
    row_pool[i] = rnd_engine() % 8192;
  }

  uint32_t pool_idx = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(bv.IsSet(row_pool[pool_idx]));
    pool_idx = (pool_idx + 1) % kPoolSize;
  }
}
BENCHMARK(BM_BitVectorIsSet);

static void BM_BitVectorSet(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = static_cast<uint32_t>(state.range(0));
  uint32_t set_percentage = static_cast<uint32_t>(state.range(1));

  BitVector bv = BvWithSizeAndSetPercentage(size, set_percentage);

  static constexpr uint32_t kPoolSize = 1024 * 1024;
  std::vector<bool> bit_pool(kPoolSize);
  std::vector<uint32_t> row_pool(kPoolSize);
  for (uint32_t i = 0; i < kPoolSize; ++i) {
    bit_pool[i] = rnd_engine() % 2;
    row_pool[i] = rnd_engine() % size;
  }

  uint32_t pool_idx = 0;
  for (auto _ : state) {
    bv.Set(row_pool[pool_idx]);
    pool_idx = (pool_idx + 1) % kPoolSize;
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BitVectorSet)->Apply(BitVectorArgs);

static void BM_BitVectorClear(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = static_cast<uint32_t>(state.range(0));
  uint32_t set_percentage = static_cast<uint32_t>(state.range(1));

  BitVector bv = BvWithSizeAndSetPercentage(size, set_percentage);

  static constexpr uint32_t kPoolSize = 1024 * 1024;
  std::vector<uint32_t> row_pool(kPoolSize);
  for (uint32_t i = 0; i < kPoolSize; ++i) {
    row_pool[i] = rnd_engine() % size;
  }

  uint32_t pool_idx = 0;
  for (auto _ : state) {
    bv.Clear(row_pool[pool_idx]);
    pool_idx = (pool_idx + 1) % kPoolSize;
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BitVectorClear)->Apply(BitVectorArgs);

static void BM_BitVectorIndexOfNthSet(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = static_cast<uint32_t>(state.range(0));
  uint32_t set_percentage = static_cast<uint32_t>(state.range(1));

  BitVector bv = BvWithSizeAndSetPercentage(size, set_percentage);
  static constexpr uint32_t kPoolSize = 1024 * 1024;
  std::vector<uint32_t> row_pool(kPoolSize);
  uint32_t set_bit_count = bv.CountSetBits();
  if (set_bit_count == 0) {
    state.SkipWithError("Cannot find set bit in all zeros bitvector");
    return;
  }

  for (uint32_t i = 0; i < kPoolSize; ++i) {
    row_pool[i] = rnd_engine() % set_bit_count;
  }

  uint32_t pool_idx = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(bv.IndexOfNthSet(row_pool[pool_idx]));
    pool_idx = (pool_idx + 1) % kPoolSize;
  }
}
BENCHMARK(BM_BitVectorIndexOfNthSet)->Apply(BitVectorArgs);

static void BM_BitVectorCountSetBits(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = static_cast<uint32_t>(state.range(0));
  uint32_t set_percentage = static_cast<uint32_t>(state.range(1));

  uint32_t count = 0;
  BitVector bv;
  for (uint32_t i = 0; i < size; ++i) {
    bool value = rnd_engine() % 100 < set_percentage;
    if (value) {
      bv.AppendTrue();
    } else {
      bv.AppendFalse();
    }

    if (value)
      count++;
  }

  uint32_t res = count;
  for (auto _ : state) {
    benchmark::DoNotOptimize(res &= bv.CountSetBits());
  }
  PERFETTO_CHECK(res == count);
}
BENCHMARK(BM_BitVectorCountSetBits)->Apply(BitVectorArgs);

static void BM_BitVectorGetSetBitIndices(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  auto size = static_cast<uint32_t>(state.range(0));
  auto set_percentage = static_cast<uint32_t>(state.range(1));

  BitVector bv;
  for (uint32_t i = 0; i < size; ++i) {
    bool value = rnd_engine() % 100 < set_percentage;
    if (value) {
      bv.AppendTrue();
    } else {
      bv.AppendFalse();
    }
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(bv.GetSetBitIndices());
  }
}
BENCHMARK(BM_BitVectorGetSetBitIndices)->Apply(BitVectorArgs);

static void BM_BitVectorResize(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  static constexpr uint32_t kPoolSize = 1024 * 1024;
  static constexpr uint32_t kMaxSize = 1234567;

  std::vector<bool> resize_fill_pool(kPoolSize);
  std::vector<uint32_t> resize_count_pool(kPoolSize);
  for (uint32_t i = 0; i < kPoolSize; ++i) {
    resize_fill_pool[i] = rnd_engine() % 2;
    resize_count_pool[i] = rnd_engine() % kMaxSize;
  }

  uint32_t pool_idx = 0;
  BitVector bv;
  for (auto _ : state) {
    bv.Resize(resize_count_pool[pool_idx], resize_fill_pool[pool_idx]);
    pool_idx = (pool_idx + 1) % kPoolSize;
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BitVectorResize);

static void BM_BitVectorUpdateSetBits(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  uint32_t size = static_cast<uint32_t>(state.range(0));
  uint32_t set_percentage = static_cast<uint32_t>(state.range(1));
  uint32_t picker_set_percentage = static_cast<uint32_t>(state.range(2));

  BitVector bv;
  BitVector picker;
  for (uint32_t i = 0; i < size; ++i) {
    bool value = rnd_engine() % 100 < set_percentage;
    if (value) {
      bv.AppendTrue();

      bool picker_value = rnd_engine() % 100 < picker_set_percentage;
      if (picker_value) {
        picker.AppendTrue();
      } else {
        picker.AppendFalse();
      }
    } else {
      bv.AppendFalse();
    }
  }

  uint32_t set_bit_count = bv.CountSetBits();
  uint32_t picker_set_bit_count = picker.CountSetBits();

  for (auto _ : state) {
    BitVector copy = bv.Copy();
    copy.UpdateSetBits(picker);
    benchmark::DoNotOptimize(copy);
  }

  state.counters["s/set bit"] = benchmark::Counter(
      set_bit_count, benchmark::Counter::kIsIterationInvariantRate |
                         benchmark::Counter::kInvert);
  state.counters["s/set picker bit"] = benchmark::Counter(
      picker_set_bit_count, benchmark::Counter::kIsIterationInvariantRate |
                                benchmark::Counter::kInvert);
}
BENCHMARK(BM_BitVectorUpdateSetBits)->Apply(UpdateSetBitsSelectBitsArgs);

static void BM_BitVectorSelectBits(benchmark::State& state) {
  static constexpr uint32_t kRandomSeed = 42;
  std::minstd_rand0 rnd_engine(kRandomSeed);

  auto size = static_cast<uint32_t>(state.range(0));
  auto set_percentage = static_cast<uint32_t>(state.range(1));
  auto mask_set_percentage = static_cast<uint32_t>(state.range(2));

  BitVector bv;
  BitVector mask;
  for (uint32_t i = 0; i < size; ++i) {
    bool value = rnd_engine() % 100 < set_percentage;
    if (value) {
      bv.AppendTrue();
    } else {
      bv.AppendFalse();
    }
    bool mask_value = rnd_engine() % 100 < mask_set_percentage;
    if (mask_value) {
      mask.AppendTrue();
    } else {
      mask.AppendFalse();
    }
  }

  uint32_t set_bit_count = bv.CountSetBits();
  uint32_t mask_set_bit_count = mask.CountSetBits();

  for (auto _ : state) {
    BitVector copy = bv.Copy();
    copy.SelectBits(mask);
    benchmark::DoNotOptimize(copy);
  }

  state.counters["s/set bit"] = benchmark::Counter(
      set_bit_count, benchmark::Counter::kIsIterationInvariantRate |
                         benchmark::Counter::kInvert);
  state.counters["s/mask bit"] = benchmark::Counter(
      mask_set_bit_count, benchmark::Counter::kIsIterationInvariantRate |
                              benchmark::Counter::kInvert);
}
BENCHMARK(BM_BitVectorSelectBits)->Apply(UpdateSetBitsSelectBitsArgs);

static void BM_BitVectorFromIndexVector(benchmark::State& state) {
  std::vector<int64_t> indices;
  for (int64_t i = 0; i < 1024l * 1024l; i++) {
    indices.push_back(i);
  }

  indices.push_back(std::numeric_limits<uint32_t>::max() >> 5);

  for (auto _ : state) {
    benchmark::DoNotOptimize(BitVector::FromSortedIndexVector(indices));
  }
}
BENCHMARK(BM_BitVectorFromIndexVector);
