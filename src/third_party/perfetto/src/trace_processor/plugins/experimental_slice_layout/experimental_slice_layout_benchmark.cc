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
#include <optional>
#include <string>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/plugins/experimental_slice_layout/experimental_slice_layout_impl.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"

namespace perfetto::trace_processor {
namespace {

tables::SliceTable::Id Insert(tables::SliceTable* table,
                              int64_t ts,
                              int64_t dur,
                              uint32_t track_id,
                              StringId name,
                              std::optional<tables::SliceTable::Id> parent_id) {
  tables::SliceTable::Row row;
  row.ts = ts;
  row.dur = dur;
  row.depth = 0;
  std::optional<tables::SliceTable::Id> id = parent_id;
  while (id) {
    row.depth++;
    id = (*table)[id.value().value].parent_id();
  }
  row.track_id = tables::TrackTable::Id{track_id};
  row.name = name;
  row.parent_id = parent_id;
  return table->Insert(row).id;
}

std::string AllTrackIds(int64_t n) {
  std::string ids;
  for (int64_t i = 0; i < n; ++i) {
    if (i) {
      ids += ',';
    }
    ids += std::to_string(i + 1);
  }
  return ids;
}

void RunLayout(benchmark::State& state,
               StringPool* pool,
               const tables::SliceTable* table,
               const std::string& ids) {
  for (auto _ : state) {
    // Fresh gen each iteration so the per-track-id-set cache never short
    // circuits the computation we are trying to measure.
    ExperimentalSliceLayout gen(pool, table);
    auto cursor = gen.MakeCursor();
    bool ok = cursor->Run({SqlValue::String(ids.c_str())});
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(cursor->dataframe());
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          table->row_count());
}

// N depth-0 groups, disjoint in time, one per track. Concurrency 1.
void BM_SequentialDisjoint(benchmark::State& state) {
  int64_t n = state.range(0);
  StringPool pool;
  tables::SliceTable table(&pool);
  StringId name = pool.InternString("s");
  for (int64_t i = 0; i < n; ++i) {
    Insert(&table, i * 10, 5, static_cast<uint32_t>(i + 1), name, std::nullopt);
  }
  RunLayout(state, &pool, &table, AllTrackIds(n));
}
BENCHMARK(BM_SequentialDisjoint)->Range(128, 32768);

// N groups, each a depth-5 tree, disjoint in time, all on one track. Mimics a
// normal thread track (deep-ish call trees back to back).
void BM_NestedSequential(benchmark::State& state) {
  int64_t n = state.range(0);
  StringPool pool;
  tables::SliceTable table(&pool);
  StringId name = pool.InternString("s");
  for (int64_t i = 0; i < n; ++i) {
    int64_t base = i * 100;
    auto a = Insert(&table, base, 50, 1, name, std::nullopt);
    auto b = Insert(&table, base, 40, 1, name, a);
    auto c = Insert(&table, base, 30, 1, name, b);
    auto d = Insert(&table, base, 20, 1, name, c);
    Insert(&table, base, 10, 1, name, d);
  }
  RunLayout(state, &pool, &table, "1");
}
BENCHMARK(BM_NestedSequential)->Range(128, 32768);

// N depth-0 groups all fully overlapping in time, one per track. Concurrency N.
// This is the adversarial case for the tallest-first algorithm: every group
// overlaps every other, so each placement inspects and sorts O(N) neighbours.
void BM_AllConcurrent(benchmark::State& state) {
  int64_t n = state.range(0);
  StringPool pool;
  tables::SliceTable table(&pool);
  StringId name = pool.InternString("s");
  for (int64_t i = 0; i < n; ++i) {
    Insert(&table, 0, 1000000, static_cast<uint32_t>(i + 1), name,
           std::nullopt);
  }
  RunLayout(state, &pool, &table, AllTrackIds(n));
}
BENCHMARK(BM_AllConcurrent)->Range(128, 8192);

// N depth-0 groups on N tracks, each spanning a sliding window so ~W of them
// overlap at any instant. Realistic busy async track.
void BM_StaggeredWindow(benchmark::State& state) {
  int64_t n = state.range(0);
  const int64_t window = 200;
  StringPool pool;
  tables::SliceTable table(&pool);
  StringId name = pool.InternString("s");
  for (int64_t i = 0; i < n; ++i) {
    Insert(&table, i, window, static_cast<uint32_t>(i + 1), name, std::nullopt);
  }
  RunLayout(state, &pool, &table, AllTrackIds(n));
}
BENCHMARK(BM_StaggeredWindow)->Range(128, 32768);

}  // namespace
}  // namespace perfetto::trace_processor
