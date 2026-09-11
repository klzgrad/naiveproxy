// Copyright (C) 2021 The Android Open Source Project
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

#include <algorithm>
#include <string>

#include "perfetto/ext/base/file_utils.h"
#include "src/base/test/utils.h"
#include "src/protozero/filtering/message_filter.h"

static void BM_ProtozeroMessageFilter(benchmark::State& state) {
  std::string trace_data;
  static const char kTestTrace[] = "test/data/example_android_trace_30s.pb";
  perfetto::base::ReadFile(perfetto::base::GetTestDataPath(kTestTrace),
                           &trace_data);
  PERFETTO_CHECK(!trace_data.empty());

  std::string filter;
  static const char kFullTraceFilter[] = "test/data/full_trace_filter.bytecode";
  perfetto::base::ReadFile(kFullTraceFilter, &filter);
  PERFETTO_CHECK(!filter.empty());

  protozero::MessageFilter filt;
  filt.LoadFilterBytecode(filter.data(), filter.size());

  for (auto _ : state) {
    auto res = filt.FilterMessage(trace_data.data(), trace_data.size());
    benchmark::DoNotOptimize(res);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations() * trace_data.size()));
}

BENCHMARK(BM_ProtozeroMessageFilter);
