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

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"

namespace {

std::vector<std::string> LoadTraceStringRaw(FILE* f) {
  char line[4096];
  std::vector<std::string> strs;
  strs.reserve(1024ul * 1024);
  while (auto* end = fgets(line, sizeof(line), f)) {
    strs.emplace_back(line, static_cast<size_t>(end - line));
  }
  return strs;
}

const std::vector<std::string>& LoadTraceStrings(benchmark::State& state) {
  // This requires that the user has downloaded the file
  // go/perfetto-benchmark-trace-strings into /tmp/trace_strings. The file is
  // too big (2.3 GB after uncompression) and it's not worth adding it to the
  // //test/data. Also it contains data from a team member's phone and cannot
  // be public.
  perfetto::base::ScopedFstream f(fopen("/tmp/trace_strings", "re"));
  if (!f) {
    static perfetto::base::NoDestructor<std::vector<std::string>> raw;
    state.SkipWithError(
        "Test strings missing. Googlers: download "
        "go/perfetto-benchmark-trace-strings and save into /tmp/trace_strings");
    return raw.ref();
  }
  static perfetto::base::NoDestructor<std::vector<std::string>> raw(
      LoadTraceStringRaw(f.get()));
  if (raw.ref().empty()) {
    state.SkipWithError(
        "/tmp/trace_strings exists, but contains no data. Googlers: download "
        "go/perfetto-benchmark-trace-strings and save into /tmp/trace_strings");
  }
  return raw.ref();
}

void BM_StringPoolIntern(benchmark::State& state) {
  const std::vector<std::string>& strings = LoadTraceStrings(state);
  perfetto::trace_processor::StringPool pool;
  pool.set_locking(state.range());
  uint32_t i = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pool.InternString(
        perfetto::base::StringView(strings[i++ % strings.size()])));
  }
}
BENCHMARK(BM_StringPoolIntern)->ArgsProduct({{false, true}});

void BM_StringPoolInternAlreadyExist(benchmark::State& state) {
  const std::vector<std::string>& strings = LoadTraceStrings(state);
  perfetto::trace_processor::StringPool pool;
  pool.set_locking(state.range());
  for (const auto& str : strings) {
    pool.InternString(perfetto::base::StringView(str));
  }
  uint32_t i = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pool.InternString(
        perfetto::base::StringView(strings[i++ % strings.size()])));
  }
}
BENCHMARK(BM_StringPoolInternAlreadyExist)->ArgsProduct({{false, true}});

void BM_StringPoolGet(benchmark::State& state) {
  const std::vector<std::string>& strings = LoadTraceStrings(state);
  perfetto::trace_processor::StringPool pool;
  std::vector<perfetto::trace_processor::StringPool::Id> ids;
  ids.reserve(strings.size());
  for (const auto& str : strings) {
    ids.emplace_back(pool.InternString(perfetto::base::StringView(str)));
  }

  uint32_t i = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pool.Get(ids[i++ % ids.size()]));
  }
}
BENCHMARK(BM_StringPoolGet);

}  // namespace
