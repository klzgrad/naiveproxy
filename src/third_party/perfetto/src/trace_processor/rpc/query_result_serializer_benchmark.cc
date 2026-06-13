/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/base/logging.h"
#include "perfetto/ext/trace_processor/rpc/query_result_serializer.h"

#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"

using perfetto::trace_processor::Config;
using perfetto::trace_processor::QueryResultSerializer;
using perfetto::trace_processor::TraceProcessor;
using VectorType = std::vector<uint8_t>;

namespace {

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

void BenchmarkArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Ranges({{1024, 1024}, {4096, 4096}});
  } else {
    b->RangeMultiplier(8)->Ranges({{128, 8192}, {4096, 1024 * 512}});
  }
}

void RunQueryChecked(TraceProcessor* tp, const std::string& query) {
  auto iter = tp->ExecuteQuery(query);
  iter.Next();
  PERFETTO_CHECK(iter.Status().ok());
}

}  // namespace

static void BM_QueryResultSerializer_Mixed(benchmark::State& state) {
  auto tp = TraceProcessor::CreateInstance(Config());
  RunQueryChecked(
      tp.get(),
      "create virtual table win using __intrinsic_window(0, 50000, 1);");
  VectorType buf;
  for (auto _ : state) {
    auto iter = tp->ExecuteQuery(
        "select dur || dur as x, ts, dur * 1.0 as dur, quantum_ts from win");
    QueryResultSerializer serializer(std::move(iter));
    serializer.set_batch_size_for_testing(
        static_cast<uint32_t>(state.range(0)),
        static_cast<uint32_t>(state.range(1)));
    while (serializer.Serialize(&buf)) {
    }
    benchmark::DoNotOptimize(buf.data());
    buf.clear();
  }
  benchmark::ClobberMemory();
}

static void BM_QueryResultSerializer_Strings(benchmark::State& state) {
  auto tp = TraceProcessor::CreateInstance(Config());
  RunQueryChecked(
      tp.get(),
      "create virtual table win using __intrinsic_window(0, 100000, 1);");
  VectorType buf;
  for (auto _ : state) {
    auto iter = tp->ExecuteQuery(
        "select  ts || '-' || ts , (dur * 1.0) || dur from win");
    QueryResultSerializer serializer(std::move(iter));
    serializer.set_batch_size_for_testing(
        static_cast<uint32_t>(state.range(0)),
        static_cast<uint32_t>(state.range(1)));
    while (serializer.Serialize(&buf)) {
    }
    benchmark::DoNotOptimize(buf.data());
    buf.clear();
  }
  benchmark::ClobberMemory();
}

BENCHMARK(BM_QueryResultSerializer_Mixed)->Apply(BenchmarkArgs);
BENCHMARK(BM_QueryResultSerializer_Strings)->Apply(BenchmarkArgs);
