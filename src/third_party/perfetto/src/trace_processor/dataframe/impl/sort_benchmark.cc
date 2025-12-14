/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/dataframe/impl/sort.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

// A simple POD object used for benchmarking LSD radix sort.
struct PodObject {
  uint64_t key;
  uint32_t value;
};

// A trivially copyable struct that points to string data. This is used for
// benchmarking MSD radix sort, which requires trivially copyable elements.
struct StringPtr {
  const char* data;
  size_t size;
};

// Generates a random alphanumeric string of a given length.
std::string RandomString(std::mt19937& gen, size_t len) {
  std::string str(len, 0);
  std::uniform_int_distribution<> dist(32, 126);  // printable ascii
  for (size_t i = 0; i < len; ++i) {
    str[i] = static_cast<char>(dist(gen));
  }
  return str;
}

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

// --- Sorter Implementations ---

struct RadixSortTag {};
struct StdSortTag {};
struct StdUnstableSortTag {};

template <typename Tag>
struct LsdSorter;

template <>
struct LsdSorter<RadixSortTag> {
  static void Sort(std::vector<PodObject>& data) {
    std::vector<PodObject> scratch(data.size());
    std::vector<uint32_t> counts(1 << 16);
    perfetto::base::RadixSort(
        data.data(), data.data() + data.size(), scratch.data(), counts.data(),
        sizeof(uint64_t), [](const PodObject& obj) {
          return reinterpret_cast<const uint8_t*>(&obj.key);
        });
  }
};

template <>
struct LsdSorter<StdSortTag> {
  static void Sort(std::vector<PodObject>& data) {
    std::stable_sort(
        data.begin(), data.end(),
        [](const PodObject& a, const PodObject& b) { return a.key < b.key; });
  }
};

template <>
struct LsdSorter<StdUnstableSortTag> {
  static void Sort(std::vector<PodObject>& data) {
    // Note: this is an unfair comparison as std::sort is not stable. This is
    // included to understand the performance cost of stability.
    std::sort(
        data.begin(), data.end(),
        [](const PodObject& a, const PodObject& b) { return a.key < b.key; });
  }
};

template <typename Tag>
struct MsdSorter;

template <>
struct MsdSorter<RadixSortTag> {
  static void Sort(std::vector<StringPtr>& data) {
    std::vector<StringPtr> scratch(data.size());
    perfetto::base::MsdRadixSort(
        data.data(), data.data() + data.size(), scratch.data(),
        [](const StringPtr& s) { return std::string_view(s.data, s.size); });
  }
};

template <>
struct MsdSorter<StdSortTag> {
  static void Sort(std::vector<StringPtr>& data) {
    std::sort(data.begin(), data.end(),
              [](const StringPtr& a, const StringPtr& b) {
                return std::string_view(a.data, a.size) <
                       std::string_view(b.data, b.size);
              });
  }
};

// --- Benchmarks for LSD Radix Sort ---

static void SortLsdArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Arg(16);
  } else {
    b->Arg(16);
    b->Arg(4096);
    b->Arg(16384);
    b->Arg(65536);
    b->Arg(4194304);
  }
}

template <typename SorterTag>
static void BM_DataframeSortLsd(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));

  std::vector<PodObject> data(n);
  std::mt19937_64 engine(0);
  std::uniform_int_distribution<uint64_t> dist;
  for (size_t i = 0; i < n; ++i) {
    data[i] = {dist(engine), static_cast<uint32_t>(i)};
  }

  for (auto _ : state) {
    std::vector<PodObject> working_copy = data;
    LsdSorter<SorterTag>::Sort(working_copy);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * n));
}
BENCHMARK_TEMPLATE(BM_DataframeSortLsd, RadixSortTag)
    ->Apply(SortLsdArgs)
    ->Name("BM_DataframeSortLsdRadix");
BENCHMARK_TEMPLATE(BM_DataframeSortLsd, StdSortTag)
    ->Apply(SortLsdArgs)
    ->Name("BM_DataframeSortLsdStd");
BENCHMARK_TEMPLATE(BM_DataframeSortLsd, StdUnstableSortTag)
    ->Apply(SortLsdArgs)
    ->Name("BM_DataframeSortLsdStdUnstable");

// --- Benchmarks for MSD Radix Sort ---

static void SortMsdArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Args({16, 8});
  } else {
    for (int i : {16, 64, 256, 1024, 262144}) {
      for (int j : {8, 64}) {
        b->Args({i, j});
      }
    }
  }
}

template <typename SorterTag>
static void BM_DataframeSortMsd(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  const auto str_len = static_cast<size_t>(state.range(1));

  std::vector<std::string> string_data(n);
  std::vector<StringPtr> data(n);
  std::mt19937 engine(0);
  for (size_t i = 0; i < n; ++i) {
    string_data[i] = RandomString(engine, str_len);
    data[i] = {string_data[i].data(), string_data[i].size()};
  }

  for (auto _ : state) {
    std::vector<StringPtr> working_copy = data;
    MsdSorter<SorterTag>::Sort(working_copy);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * n));
}
BENCHMARK_TEMPLATE(BM_DataframeSortMsd, RadixSortTag)
    ->Apply(SortMsdArgs)
    ->Name("BM_DataframeSortMsdRadix");
BENCHMARK_TEMPLATE(BM_DataframeSortMsd, StdSortTag)
    ->Apply(SortMsdArgs)
    ->Name("BM_DataframeSortMsdStd");

}  // namespace
