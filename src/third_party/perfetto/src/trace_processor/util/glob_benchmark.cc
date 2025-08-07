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

#include "src/trace_processor/util/glob.h"

#include <benchmark/benchmark.h>
#include <sqlite3.h>
#include <cstdio>
#include <string>
#include <vector>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_view.h"

namespace {

using benchmark::Counter;
using perfetto::trace_processor::util::GlobMatcher;

const char kAndroidGlob[] = "*android*";
const char kLaunchingGlob[] = "launching: *";
const char kChoreographerGlob[] = "Choreographer#doFrame*";
const char kQuestionMarkGlob[] = "Choreo?rapher#doFrame*";
const char kCharClassGlob[] = "Choreo[a-z]rapher#doFrame*";

std::vector<std::string> LoadTraceStrings(benchmark::State& state) {
  std::vector<std::string> strs;
  // This requires that the user has downloaded the file
  // go/perfetto-benchmark-slice-strings into /tmp/trace_strings. The file is
  // too big (220 MB after uncompression) and it's not worth adding it to the
  // //test/data. Also it contains data from a team member's phone and cannot
  // be public.
  perfetto::base::ScopedFstream f(fopen("/tmp/slice_strings", "re"));
  if (!f) {
    state.SkipWithError(
        "Test strings missing. Googlers: download "
        "go/perfetto-benchmark-slice-strings and save into /tmp/slice_strings");
    return strs;
  }
  char line[4096];
  while (fgets(line, sizeof(line), *f)) {
    strs.emplace_back(perfetto::base::StringView(line).ToStdString());
  }
  return strs;
}

template <class... Args>
void BM_Glob(benchmark::State& state, Args&&... args) {
  auto args_tuple = std::make_tuple(std::move(args)...);

  std::vector<std::string> strs = LoadTraceStrings(state);
  GlobMatcher glob = GlobMatcher::FromPattern(std::get<0>(args_tuple));
  for (auto _ : state) {
    for (const std::string& str : strs) {
      benchmark::DoNotOptimize(glob.Matches(perfetto::base::StringView(str)));
    }
    benchmark::ClobberMemory();
  }
  state.counters["str/s"] = Counter(static_cast<double>(strs.size()),
                                    Counter::kIsIterationInvariantRate);
  state.counters["s/str"] =
      Counter(static_cast<double>(strs.size()),
              Counter::kIsIterationInvariantRate | Counter::kInvert);
}

BENCHMARK_CAPTURE(BM_Glob, android, kAndroidGlob);
BENCHMARK_CAPTURE(BM_Glob, launching, kLaunchingGlob);
BENCHMARK_CAPTURE(BM_Glob, choreographer, kChoreographerGlob);
BENCHMARK_CAPTURE(BM_Glob, question_mark, kQuestionMarkGlob);
BENCHMARK_CAPTURE(BM_Glob, char_class, kCharClassGlob);

template <class... Args>
void BM_SqliteGlob(benchmark::State& state, Args&&... args) {
  auto args_tuple = std::make_tuple(std::move(args)...);
  const char* glob = std::get<0>(args_tuple);

  std::vector<std::string> strs = LoadTraceStrings(state);
  for (auto _ : state) {
    for (const std::string& str : strs)
      benchmark::DoNotOptimize(sqlite3_strglob(glob, str.c_str()));
    benchmark::ClobberMemory();
  }
  state.counters["str/s"] = Counter(static_cast<double>(strs.size()),
                                    Counter::kIsIterationInvariantRate);
  state.counters["s/str"] =
      Counter(static_cast<double>(strs.size()),
              Counter::kIsIterationInvariantRate | Counter::kInvert);
}

BENCHMARK_CAPTURE(BM_SqliteGlob, android, kAndroidGlob);
BENCHMARK_CAPTURE(BM_SqliteGlob, launching, kLaunchingGlob);
BENCHMARK_CAPTURE(BM_SqliteGlob, slice, kChoreographerGlob);
BENCHMARK_CAPTURE(BM_SqliteGlob, question_mark, kQuestionMarkGlob);
BENCHMARK_CAPTURE(BM_SqliteGlob, char_class, kCharClassGlob);

}  // namespace
