/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include <cstdint>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/scoped_file.h"
#include "src/base/test/utils.h"
#include "src/protozero/filtering/string_filter.h"

namespace {

using Policy = protozero::StringFilter::Policy;

std::vector<std::pair<size_t, size_t>> LoadTraceStrings(
    benchmark::State& state,
    std::vector<char>& storage) {
  storage.clear();

  std::vector<std::pair<size_t, size_t>> strs;
  std::string path = perfetto::base::GetTestDataPath(
      "test/data/example_android_trace_30s_atrace_strings.txt");
  perfetto::base::ScopedFstream f(fopen(path.c_str(), "re"));
  if (!f) {
    state.SkipWithError("Strings does not exist");
    return {};
  }
  char line[4096];
  while (fgets(line, sizeof(line), *f)) {
    size_t len = strlen(line);
    size_t pos = storage.size();
    storage.insert(storage.end(), line, line + len);
    strs.push_back(std::make_pair(pos, len));
  }
  return strs;
}

void Benchmark(benchmark::State& state,
               Policy policy,
               const char* regex,
               const char* atrace) {
  protozero::StringFilter rewriter;
  for (int64_t i = 0; i < state.range(0); ++i) {
    rewriter.AddRule(policy, regex, atrace);
  }

  std::vector<char> storage;
  auto strs = LoadTraceStrings(state, storage);
  uint32_t match = 0;
  for (auto _ : state) {
    match = 0;
    std::vector<char> local = storage;
    for (auto& str : strs) {
      match += rewriter.MaybeFilter(local.data() + str.first, str.second);
    }
    benchmark::DoNotOptimize(match);
  }
  state.counters["time/string"] =
      benchmark::Counter(static_cast<double>(strs.size()),
                         benchmark::Counter::kIsIterationInvariantRate |
                             benchmark::Counter::kInvert);
  state.counters["time/redaction"] =
      benchmark::Counter(static_cast<double>(match),
                         benchmark::Counter::kIsIterationInvariantRate |
                             benchmark::Counter::kInvert);
  state.counters["redactions"] = benchmark::Counter(
      static_cast<double>(match), benchmark::Counter::kDefaults);
}

}  // namespace

static void BM_ProtozeroStringRewriterRedactMissing(benchmark::State& state) {
  Benchmark(state, Policy::kMatchRedactGroups,
            R"(S\|[^|]+\|\*job\*\/.*\/.*\/(.*)\n)", "");
}
BENCHMARK(BM_ProtozeroStringRewriterRedactMissing)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactMissing(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceMatchRedactGroups,
            R"(S\|[^|]+\|\*job\*\/.*\/.*\/(.*)\n)", "*job*");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactMissing)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterRedactRare(benchmark::State& state) {
  Benchmark(state, Policy::kMatchRedactGroups,
            R"(B\|[^|]+\|VerifyClass (.*)\n)", "");
}
BENCHMARK(BM_ProtozeroStringRewriterRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactRare(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceMatchRedactGroups,
            R"(B\|[^|]+\|VerifyClass (.*)\n)", "VerifyClass");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceSearchSingleRedactRare(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceRepeatedSearchRedactGroups,
            R"(VerifyClass (.*)\n)", "VerifyClass");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceSearchSingleRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterRedactCommon(benchmark::State& state) {
  Benchmark(state, Policy::kMatchRedactGroups,
            R"(B\|[^|]+\|Lock contention on a monitor lock (.*)\n)", "");
}
BENCHMARK(BM_ProtozeroStringRewriterRedactCommon)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactCommon(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceMatchRedactGroups,
            R"(B\|[^|]+\|Lock contention on a monitor lock (.*)\n)",
            "Lock contention on a monitor lock");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactCommon)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactSpammy(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceMatchRedactGroups,
            R"(C\|[^|]+\|Heap size \(KB\)\|(\d+)\n)", "Heap size (KB)");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactSpammy)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceSearchSingleRedactSpammy(
    benchmark::State& state) {
  Benchmark(state, Policy::kAtraceRepeatedSearchRedactGroups,
            R"(Heap size \(KB\)\|(\d+))", "Heap size (KB)");
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceSearchSingleRedactSpammy)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);
