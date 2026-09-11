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
#include <memory>

#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/slice_translation_table.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

// Builds a TraceProcessorContext with just the dependencies SliceTracker needs.
struct Fixture {
  explicit Fixture(TraceProcessorContext* context) {
    context->storage = std::make_unique<TraceStorage>();
    context->global_args_tracker =
        std::make_unique<GlobalArgsTracker>(context->storage.get());
    context->global_stats_tracker =
        std::make_unique<GlobalStatsTracker>(context->storage.get());
    context->machine_tracker =
        std::make_unique<MachineTracker>(context, kDefaultMachineId);
    context->args_translation_table =
        std::make_unique<ArgsTranslationTable>(context->storage.get());
    context->slice_translation_table =
        std::make_unique<SliceTranslationTable>(context->storage.get());
    context->trace_state =
        TraceProcessorContextPtr<TraceProcessorContext::TraceState>::MakeRoot(
            TraceProcessorContext::TraceState{TraceId{0}});
    context->stats_tracker = std::make_unique<StatsTracker>(context);
  }
};

// Number of slice events processed per timed iteration.
constexpr int64_t kEvents = 1 << 14;

// Arg-less begin/end: the common fast path.
void BM_SliceTrackerBeginEnd(benchmark::State& state) {
  constexpr TrackId track{0u};
  for (auto _ : state) {
    state.PauseTiming();
    TraceProcessorContext context;
    Fixture fixture(&context);
    StringId name = context.storage->InternString("slice");
    SliceTracker tracker(&context);
    state.ResumeTiming();

    for (int64_t i = 0; i < kEvents; i += 2) {
      tracker.Begin(i, track, kNullStringId, name);
      tracker.End(i + 1, track, kNullStringId, name);
    }
    benchmark::DoNotOptimize(context.storage->slice_table().row_count());
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kEvents);
}

// Begin/end with args on both ends: pooled ArgsTracker + inlined callback.
void BM_SliceTrackerBeginEndArgs(benchmark::State& state) {
  constexpr TrackId track{0u};
  for (auto _ : state) {
    state.PauseTiming();
    TraceProcessorContext context;
    Fixture fixture(&context);
    StringId name = context.storage->InternString("slice");
    StringId key1 = context.storage->InternString("key1");
    StringId key2 = context.storage->InternString("key2");
    SliceTracker tracker(&context);
    state.ResumeTiming();

    for (int64_t i = 0; i < kEvents; i += 2) {
      tracker.Begin(i, track, kNullStringId, name,
                    [&](ArgsTracker::BoundInserter* inserter) {
                      inserter->AddArg(key1, Variadic::Integer(i));
                      inserter->AddArg(key2, Variadic::Integer(i * 2));
                    });
      tracker.End(i + 1, track, kNullStringId, name,
                  [&](ArgsTracker::BoundInserter* inserter) {
                    inserter->AddArg(key1, Variadic::Integer(i + 1));
                  });
    }
    benchmark::DoNotOptimize(context.storage->slice_table().row_count());
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kEvents);
}

// Deep nested stack, then unwind: stack push/pop at depth.
void BM_SliceTrackerNested(benchmark::State& state) {
  constexpr TrackId track{0u};
  constexpr int64_t kDepth = 32;
  for (auto _ : state) {
    state.PauseTiming();
    TraceProcessorContext context;
    Fixture fixture(&context);
    StringId name = context.storage->InternString("slice");
    SliceTracker tracker(&context);
    state.ResumeTiming();

    int64_t ts = 0;
    for (int64_t batch = 0; batch < kEvents / (2 * kDepth); batch++) {
      for (int64_t d = 0; d < kDepth; d++)
        tracker.Begin(ts++, track, kNullStringId, name);
      for (int64_t d = 0; d < kDepth; d++)
        tracker.End(ts++, track, kNullStringId, name);
    }
    benchmark::DoNotOptimize(context.storage->slice_table().row_count());
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          (kEvents / (2 * kDepth)) * (2 * kDepth));
}

// Round-robin across many tracks so consecutive events always switch track:
// the last-track cache misses every time, exercising the real map lookup.
void BM_SliceTrackerMultiTrack(benchmark::State& state) {
  constexpr int64_t kTracks = 512;
  for (auto _ : state) {
    state.PauseTiming();
    TraceProcessorContext context;
    Fixture fixture(&context);
    StringId name = context.storage->InternString("slice");
    SliceTracker tracker(&context);
    state.ResumeTiming();

    int64_t ts = 0;
    for (int64_t b = 0; b < kEvents / (2 * kTracks); b++) {
      for (int64_t t = 0; t < kTracks; t++)
        tracker.Begin(ts++, TrackId(static_cast<uint32_t>(t)), kNullStringId,
                      name);
      for (int64_t t = 0; t < kTracks; t++)
        tracker.End(ts++, TrackId(static_cast<uint32_t>(t)), kNullStringId,
                    name);
    }
    benchmark::DoNotOptimize(context.storage->slice_table().row_count());
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kEvents);
}

BENCHMARK(BM_SliceTrackerBeginEnd);
BENCHMARK(BM_SliceTrackerMultiTrack);
BENCHMARK(BM_SliceTrackerBeginEndArgs);
BENCHMARK(BM_SliceTrackerNested);

}  // namespace
}  // namespace perfetto::trace_processor
