// Copyright (C) 2026 The Android Open Source Project
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

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/regex.h"
#include "src/base/regex/regex_std.h"

#if PERFETTO_BUILDFLAG(PERFETTO_RE2)
#include "src/base/regex/regex_re2.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_PCRE2)
#include "src/base/regex/regex_pcre2.h"
#endif

namespace perfetto {
namespace base {
namespace {

// --- Backend-parameterized benchmarks ---

template <typename Backend>
static void BM_Regex_PartialMatch_Simple(benchmark::State& state) {
  auto re = Backend::Create("abc", false);
  PERFETTO_CHECK(re.ok());
  std::string_view input = "xxabcyy";
  for (auto _ : state) {
    benchmark::DoNotOptimize(re->PartialMatch(input));
  }
}

template <typename Backend>
static void BM_Regex_FullMatch_Simple(benchmark::State& state) {
  auto re = Backend::Create("abc", false);
  PERFETTO_CHECK(re.ok());
  std::string_view input = "abc";
  for (auto _ : state) {
    benchmark::DoNotOptimize(re->FullMatch(input));
  }
}

template <typename Backend>
static void BM_Regex_PartialMatchWithGroups(benchmark::State& state) {
  auto re = Backend::Create("([a-z]+)-([0-9]+)", false);
  PERFETTO_CHECK(re.ok());
  std::string_view input = "hello-12345";
  std::vector<std::string_view> groups;
  for (auto _ : state) {
    re->PartialMatchWithGroups(input, groups);
    benchmark::DoNotOptimize(groups.data());
  }
}

template <typename Backend>
static void BM_Regex_GlobalReplace(benchmark::State& state) {
  auto re = Backend::Create("[0-9]+", false);
  PERFETTO_CHECK(re.ok());
  std::string_view input = "abc123def456ghi789";
  for (auto _ : state) {
    auto result = re->GlobalReplace(input, "X");
    benchmark::DoNotOptimize(result.data());
  }
}

template <typename Backend>
static void BM_Regex_PartialMatch_LongInput(benchmark::State& state) {
  auto re = Backend::Create(R"(\d{4}-\d{2}-\d{2})", false);
  PERFETTO_CHECK(re.ok());
  // 1000-char string with a date near the end.
  std::string input(990, 'x');
  input += "2026-03-29";
  for (auto _ : state) {
    benchmark::DoNotOptimize(re->PartialMatch(input));
  }
}

template <typename Backend>
static void BM_Regex_Compile(benchmark::State& state) {
  for (auto _ : state) {
    auto re = Backend::Create(R"(([a-zA-Z]+)(\d+)\.(\w+))", false);
    benchmark::DoNotOptimize(re.ok());
  }
}

// Register benchmarks for std::regex backend.
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_Simple, RegexStd);
BENCHMARK_TEMPLATE(BM_Regex_FullMatch_Simple, RegexStd);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatchWithGroups, RegexStd);
BENCHMARK_TEMPLATE(BM_Regex_GlobalReplace, RegexStd);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_LongInput, RegexStd);
BENCHMARK_TEMPLATE(BM_Regex_Compile, RegexStd);

#if PERFETTO_BUILDFLAG(PERFETTO_RE2)
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_Simple, RegexRe2);
BENCHMARK_TEMPLATE(BM_Regex_FullMatch_Simple, RegexRe2);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatchWithGroups, RegexRe2);
BENCHMARK_TEMPLATE(BM_Regex_GlobalReplace, RegexRe2);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_LongInput, RegexRe2);
BENCHMARK_TEMPLATE(BM_Regex_Compile, RegexRe2);
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_PCRE2)
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_Simple, RegexPcre2);
BENCHMARK_TEMPLATE(BM_Regex_FullMatch_Simple, RegexPcre2);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatchWithGroups, RegexPcre2);
BENCHMARK_TEMPLATE(BM_Regex_GlobalReplace, RegexPcre2);
BENCHMARK_TEMPLATE(BM_Regex_PartialMatch_LongInput, RegexPcre2);
BENCHMARK_TEMPLATE(BM_Regex_Compile, RegexPcre2);
#endif

}  // namespace
}  // namespace base
}  // namespace perfetto
