/*
 * Copyright (C) 2026 The Android Open Source Project
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
#include <string>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

// Benchmarks the args tracker commit path: AddArgsTo -> AddArg* -> commit (on
// inserter destruction) -> GlobalArgsTracker::AddArgSet (dedup + materialize
// into __intrinsic_args).
// This is the ~13-18% of trace-load self-time the args rewrite targets, so it
// is the metric to track that rewrite against. Each iteration commits one
// sched_switch-shaped arg set (7 args) to a fresh ftrace event row.
//
// state.range(0): if 1, values are identical every set (dedup HIT path, every
// set after the first collapses); if 0, values vary per set (dedup MISS path,
// the realistic per-event ftrace case where each set is materialized).
void BM_ArgsTrackerCommit(benchmark::State& state) {
  const bool dedup_hit = state.range(0) == 1;

  TraceProcessorContext context;
  context.storage = std::make_unique<TraceStorage>();
  context.global_args_tracker =
      std::make_unique<GlobalArgsTracker>(context.storage.get());
  auto* storage = context.storage.get();

  // Pre-intern the keys once (parsers do this; keys never vary per set).
  const StringId k_prev_comm = storage->InternString("prev_comm");
  const StringId k_prev_pid = storage->InternString("prev_pid");
  const StringId k_prev_prio = storage->InternString("prev_prio");
  const StringId k_prev_state = storage->InternString("prev_state");
  const StringId k_next_comm = storage->InternString("next_comm");
  const StringId k_next_pid = storage->InternString("next_pid");
  const StringId k_next_prio = storage->InternString("next_prio");

  // A small pool of comm strings to reuse, as real traces do.
  std::vector<StringId> comms;
  for (int i = 0; i < 64; ++i) {
    comms.push_back(
        storage->InternString(base::StringView("task_" + std::to_string(i))));
  }

  int64_t i = 0;
  for (auto _ : state) {
    int64_t v = dedup_hit ? 0 : i;
    StringId prev_comm = comms[static_cast<size_t>(v) % comms.size()];
    StringId next_comm = comms[static_cast<size_t>(v + 1) % comms.size()];

    tables::FtraceEventTable::Row row;
    row.ts = i;
    auto id = storage->mutable_ftrace_event_table()->Insert(row).id;

    {
      ArgsTracker tracker(&context);
      auto inserter = tracker.AddArgsTo(id);
      inserter.AddArg(k_prev_comm, k_prev_comm, Variadic::String(prev_comm));
      inserter.AddArg(k_prev_pid, k_prev_pid, Variadic::Integer(v));
      inserter.AddArg(k_prev_prio, k_prev_prio, Variadic::Integer(120));
      inserter.AddArg(k_prev_state, k_prev_state, Variadic::Integer(1));
      inserter.AddArg(k_next_comm, k_next_comm, Variadic::String(next_comm));
      inserter.AddArg(k_next_pid, k_next_pid, Variadic::Integer(v + 1));
      inserter.AddArg(k_next_prio, k_next_prio, Variadic::Integer(120));
      // inserter destructor commits the arg set.
    }
    ++i;
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
  benchmark::DoNotOptimize(storage->arg_table().row_count());
}

BENCHMARK(BM_ArgsTrackerCommit)->Arg(0)->Arg(1);

}  // namespace
}  // namespace perfetto::trace_processor
