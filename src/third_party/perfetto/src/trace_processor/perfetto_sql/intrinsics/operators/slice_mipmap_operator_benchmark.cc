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

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/slice_mipmap_operator.h"

namespace perfetto::trace_processor {
namespace {

// Window queries from real UI trace, sorted by step (largest first).
// Each entry is {start, end, step} representing different zoom levels.
constexpr std::array<std::array<int64_t, 3>, 8> kWindowQueries{{
    {154618822656000, 309237645312000, 17179869184},  // Zoomed out
    {199715979264000, 219043332096000, 2147483648},
    {208574349312000, 210990268416000, 268435456},
    {209379655680000, 209983635456000, 67108864},
    {209631313920000, 209669062656000, 4194304},
    {209647042560000, 209656479744000, 1048576},
    {209649401856000, 209651761152000, 262144},
    {209650575360000, 209650593792000, 2048},  // Zoomed in
}};

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

// Load slice data from CSV file into State structure.
// Returns false if file cannot be loaded.
bool LoadSliceData(SliceMipmapOperator::State& state,
                   benchmark::State& bstate) {
  std::string contents;
  if (!base::ReadFile("test/data/slice_mipmap_benchmark.csv", &contents)) {
    bstate.SkipWithError(
        "Test data missing. Please ensure test/data/slice_mipmap_benchmark.csv "
        "exists.");
    return false;
  }

  std::vector<std::string> lines = base::SplitString(contents, "\n");
  // Skip header line, parse data lines: ts,dur,depth
  for (size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.empty()) {
      continue;
    }
    std::vector<std::string> parts = base::SplitString(line, ",");
    if (parts.size() < 3) {
      bstate.SkipWithError("Malformed CSV line");
      return false;
    }
    std::optional<int64_t> ts = base::StringToInt64(parts[0]);
    std::optional<int64_t> dur = base::StringToInt64(parts[1]);
    std::optional<uint32_t> depth = base::StringToUInt32(parts[2]);
    if (!ts || !dur || !depth) {
      bstate.SkipWithError("Malformed CSV values");
      return false;
    }
    if (*depth >= state.by_depth.size()) {
      state.by_depth.resize(*depth + 1);
    }
    auto& by_depth = state.by_depth[*depth];
    uint32_t id = by_depth.forest.size();
    by_depth.forest.Push(SliceMipmapOperator::Slice{*dur, 1, id});
    by_depth.timestamps.push_back(*ts);
    by_depth.ids.push_back(id);
  }
  return true;
}

void BM_SliceMipmapFilter(benchmark::State& bstate) {
  auto window_idx = static_cast<size_t>(bstate.range(0));
  const auto& w = kWindowQueries[window_idx];
  int64_t start = w[0];
  int64_t end = w[1];
  int64_t step = w[2];

  SliceMipmapOperator::State state;
  if (!LoadSliceData(state, bstate)) {
    return;
  }

  std::vector<int64_t> queries;
  std::vector<uint32_t> positions;
  std::vector<SliceMipmapOperator::Result> results;

  for (auto _ : bstate) {
    results.clear();
    SliceMipmapOperator::FilterImpl(state, start, end, step, queries, positions,
                                    results);
    benchmark::DoNotOptimize(results.data());
    benchmark::ClobberMemory();
  }

  bstate.counters["windows"] =
      benchmark::Counter(static_cast<double>((end - start) / step),
                         benchmark::Counter::kIsIterationInvariant);
  bstate.counters["results"] =
      benchmark::Counter(static_cast<double>(results.size()),
                         benchmark::Counter::kIsIterationInvariant);
}

void SliceMipmapBenchmarkArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Arg(0);
  } else {
    for (size_t i = 0; i < kWindowQueries.size(); ++i) {
      b->Arg(static_cast<int64_t>(i));
    }
  }
}

BENCHMARK(BM_SliceMipmapFilter)->Apply(SliceMipmapBenchmarkArgs);

}  // namespace
}  // namespace perfetto::trace_processor
