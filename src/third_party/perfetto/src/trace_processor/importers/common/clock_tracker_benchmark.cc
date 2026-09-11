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

#include "src/trace_processor/importers/common/clock_tracker.h"

#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>

#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/importers/common/global_metadata_tracker.h"
#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/trace_processor_context_ptr.h"
#include "src/trace_processor/util/clock_synchronizer.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using ClockId = ClockTracker::ClockId;

constexpr ClockId REALTIME =
    ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_REALTIME);
constexpr ClockId BOOTTIME =
    ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_BOOTTIME);

// Mirrors the ClockTracker unittest fixture: the minimal context plumbing a
// ClockTracker needs to run.
struct Harness {
  Harness() {
    context.storage.reset(new TraceStorage());
    context.global_stats_tracker =
        std::make_unique<GlobalStatsTracker>(context.storage.get());
    context.global_args_tracker.reset(
        new GlobalArgsTracker(context.storage.get()));
    context.global_metadata_tracker.reset(
        new GlobalMetadataTracker(context.storage.get()));
    context.trace_state =
        TraceProcessorContextPtr<TraceProcessorContext::TraceState>::MakeRoot(
            TraceProcessorContext::TraceState{tables::TraceFileTable::Id(0)});
    context.machine_tracker =
        std::make_unique<MachineTracker>(&context, kDefaultMachineId);
    context.stats_tracker = std::make_unique<StatsTracker>(&context);
    context.metadata_tracker.reset(new MetadataTracker(&context));
    context.import_logs_tracker.reset(
        new ImportLogsTracker(&context, tables::TraceFileTable::Id(1)));
    context.trace_time_state = std::make_unique<TraceTimeState>(BOOTTIME);
    sync = std::make_unique<ClockSynchronizer>(
        context.trace_time_state.get(),
        std::make_unique<ClockSynchronizerListenerImpl>(&context));
    tracker = std::make_unique<ClockTracker>(&context, sync.get(),
                                             /*is_primary=*/true);
  }

  TraceProcessorContext context;
  std::unique_ptr<ClockSynchronizer> sync;
  std::unique_ptr<ClockTracker> tracker;
};

// The critical case: every event timestamp in a proto trace is converted to
// trace time via ToTraceTime. After the first conversion warms the path cache,
// subsequent conversions take the inlined cache-hit fast path. This benchmarks
// that steady state.
void BM_ClockTrackerToTraceTimeCacheHit(benchmark::State& state) {
  Harness h;
  h.tracker->AddSnapshot({{REALTIME, 10}, {BOOTTIME, 10010}});
  h.tracker->SetGlobalClock(BOOTTIME);
  // Warm the cache so the loop below hits the fast path.
  benchmark::DoNotOptimize(h.tracker->ToTraceTime(REALTIME, 100));

  int64_t ts = 100;
  for (auto _ : state) {
    benchmark::DoNotOptimize(h.tracker->ToTraceTime(REALTIME, ts));
    ts = (ts + 1) & 0xffff;  // stay within the cached snapshot's valid range
  }
}
BENCHMARK(BM_ClockTrackerToTraceTimeCacheHit);

// Same fast path reached via the arbitrary-domain Convert entry point.
void BM_ClockTrackerConvertCacheHit(benchmark::State& state) {
  Harness h;
  h.tracker->AddSnapshot({{REALTIME, 10}, {BOOTTIME, 10010}});
  h.tracker->SetGlobalClock(BOOTTIME);
  benchmark::DoNotOptimize(h.tracker->Convert(REALTIME, 100, BOOTTIME));

  int64_t ts = 100;
  for (auto _ : state) {
    benchmark::DoNotOptimize(h.tracker->Convert(REALTIME, ts, BOOTTIME));
    ts = (ts + 1) & 0xffff;
  }
}
BENCHMARK(BM_ClockTrackerConvertCacheHit);

}  // namespace
}  // namespace perfetto::trace_processor
