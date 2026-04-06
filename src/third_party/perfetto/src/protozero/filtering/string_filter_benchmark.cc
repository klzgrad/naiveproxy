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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

using SemanticTypeMask = protozero::StringFilter::SemanticTypeMask;

void Benchmark(
    benchmark::State& state,
    const std::vector<
        std::tuple<Policy, const char*, const char*, SemanticTypeMask>>& rules,
    uint32_t semantic_type = 0) {
  protozero::StringFilter filter;
  for (int64_t i = 0; i < state.range(0); ++i) {
    for (const auto& [policy, regex, atrace, mask] : rules) {
      filter.AddRule(policy, regex, atrace, "", mask);
    }
  }

  std::vector<char> storage;
  auto strs = LoadTraceStrings(state, storage);
  uint32_t match = 0;
  for (auto _ : state) {
    match = 0;
    std::vector<char> local = storage;
    for (auto& str : strs) {
      match += filter.MaybeFilter(local.data() + str.first, str.second,
                                  semantic_type);
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

// Benchmarks use UNSPECIFIED (semantic_type=0) by default, so we use
// Unspecified() mask to match.
static void BM_ProtozeroStringRewriterRedactMissing(benchmark::State& state) {
  Benchmark(state, {{Policy::kMatchRedactGroups,
                     R"(S\|[^|]+\|\*job\*\/.*\/.*\/(.*)\n)", "",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterRedactMissing)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactMissing(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceMatchRedactGroups,
                     R"(S\|[^|]+\|\*job\*\/.*\/.*\/(.*)\n)", "*job*",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactMissing)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterRedactRare(benchmark::State& state) {
  Benchmark(state,
            {{Policy::kMatchRedactGroups, R"(B\|[^|]+\|VerifyClass (.*)\n)", "",
              SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactRare(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceMatchRedactGroups,
                     R"(B\|[^|]+\|VerifyClass (.*)\n)", "VerifyClass",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceSearchSingleRedactRare(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceRepeatedSearchRedactGroups,
                     R"(VerifyClass (.*)\n)", "VerifyClass",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceSearchSingleRedactRare)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterRedactCommon(benchmark::State& state) {
  Benchmark(state, {{Policy::kMatchRedactGroups,
                     R"(B\|[^|]+\|Lock contention on a monitor lock (.*)\n)",
                     "", SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterRedactCommon)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactCommon(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceMatchRedactGroups,
                     R"(B\|[^|]+\|Lock contention on a monitor lock (.*)\n)",
                     "Lock contention on a monitor lock",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactCommon)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceRedactSpammy(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceMatchRedactGroups,
                     R"(C\|[^|]+\|Heap size \(KB\)\|(\d+)\n)", "Heap size (KB)",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceRedactSpammy)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringRewriterAtraceSearchSingleRedactSpammy(
    benchmark::State& state) {
  Benchmark(state, {{Policy::kAtraceRepeatedSearchRedactGroups,
                     R"(Heap size \(KB\)\|(\d+))", "Heap size (KB)",
                     SemanticTypeMask::Unspecified()}});
}
BENCHMARK(BM_ProtozeroStringRewriterAtraceSearchSingleRedactSpammy)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringFilterSemanticTypeMatching(
    benchmark::State& state) {
  Benchmark(state,
            {{Policy::kAtraceMatchRedactGroups,
              R"(B\|\d+\|Lock contention on a monitor lock (.*))",
              "Lock contention on a monitor lock",
              SemanticTypeMask::FromWords(1ULL << 1, 0)},
             {Policy::kAtraceMatchRedactGroups, R"(B\|\d+\|foo (.*))", "foo",
              SemanticTypeMask::FromWords(1ULL << 2, 0)}},
            1);  // Filter with semantic type 1
}
BENCHMARK(BM_ProtozeroStringFilterSemanticTypeMatching)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringFilterSemanticTypeNoMatch(
    benchmark::State& state) {
  Benchmark(state,
            {{Policy::kAtraceMatchRedactGroups,
              R"(B\|\d+\|Lock contention on a monitor lock (.*))",
              "Lock contention on a monitor lock",
              SemanticTypeMask::FromWords(1ULL << 1, 0)},
             {Policy::kAtraceMatchRedactGroups, R"(B\|\d+\|foo (.*))", "foo",
              SemanticTypeMask::FromWords(1ULL << 1, 0)}},
            2);  // Filter with semantic type 2 (no rules match)
}
BENCHMARK(BM_ProtozeroStringFilterSemanticTypeNoMatch)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);

static void BM_ProtozeroStringFilterSemanticTypeBothMatch(
    benchmark::State& state) {
  // Both rules explicitly include semantic type 1
  auto mask_type1 = SemanticTypeMask::FromWords(1ULL << 1, 0);
  Benchmark(state,
            {{Policy::kAtraceMatchRedactGroups,
              R"(B\|\d+\|Lock contention on a monitor lock (.*))",
              "Lock contention on a monitor lock", mask_type1},
             {Policy::kAtraceMatchRedactGroups, R"(B\|\d+\|foo (.*))", "foo",
              mask_type1}},
            1);  // Filter with semantic type 1 (all rules match)
}
BENCHMARK(BM_ProtozeroStringFilterSemanticTypeBothMatch)
    ->Unit(benchmark::kMillisecond)
    ->Arg(10);
