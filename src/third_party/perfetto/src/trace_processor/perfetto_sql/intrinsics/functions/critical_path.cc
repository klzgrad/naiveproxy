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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/critical_path.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/tables_py.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/array.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/wakeup_graph.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"

// SQLite intrinsics backing `sched.thread_executing_span`'s critical
// path: an aggregate that materialises the wakeup graph and a walk that
// turns it into per-root blocker frames. Each root is walked
// independently as iterative DFS over the wakeup chain; every frame is
// bounded by its node's [ts - idle_dur, ts + dur] window intersected
// with the caller's recursion window. Each push descends to a node
// with a strictly smaller `ts`, bounding work at one (node, sub-window)
// per reachable causal predecessor.
namespace perfetto::trace_processor {
namespace {

using perfetto_sql::WakeupGraph;
using perfetto_sql::WakeupNode;

// Args, in order:
//   id, utid, ts, dur, idle_dur, waker_id, prev_id.
struct WakeupGraphAgg : public sqlite::AggregateFunction<WakeupGraphAgg> {
  static constexpr char kName[] = "__intrinsic_wakeup_graph_agg";
  static constexpr int kArgCount = 7;

  struct AggCtx : sqlite::AggregateContext<AggCtx> {
    WakeupGraph graph;
  };

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);
    auto& g = AggCtx::GetOrCreateContextForStep(ctx).graph;

    auto id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
    if (id >= g.nodes_by_id.size()) {
      g.nodes_by_id.resize(id + 1);
    }
    WakeupNode n;
    n.utid = static_cast<uint32_t>(sqlite::value::Int64(argv[1]));
    n.ts = sqlite::value::Int64(argv[2]);
    n.dur = sqlite::value::Int64(argv[3]);
    if (sqlite::value::Type(argv[4]) != sqlite::Type::kNull) {
      n.idle_dur = sqlite::value::Int64(argv[4]);
    }
    if (sqlite::value::Type(argv[5]) != sqlite::Type::kNull) {
      n.waker_id = static_cast<uint32_t>(sqlite::value::Int64(argv[5]));
    }
    if (sqlite::value::Type(argv[6]) != sqlite::Type::kNull) {
      n.prev_id = static_cast<uint32_t>(sqlite::value::Int64(argv[6]));
    }
    g.nodes_by_id[id] = std::move(n);
  }

  static void Final(sqlite3_context* ctx) {
    auto raw = AggCtx::GetContextOrNullForFinal(ctx);
    if (!raw.get()) {
      return sqlite::result::Null(ctx);
    }
    return sqlite::result::UniquePointer(
        ctx, std::make_unique<WakeupGraph>(std::move(raw.get()->graph)),
        WakeupGraph::kName);
  }
};

// Attribute time during `[window_start, window_end)` using `node_id`
// at chain `depth` from the root. `parent_node_id` is the layer one
// level above this frame in the attribution hierarchy (the root for
// depth-0 frames) and propagates into every emitted row's `parent_id`
// so `_intervals_flatten` can collapse overlapping layers to the
// deepest active blocker per `(root, ts)`.
struct Frame {
  uint32_t node_id;
  int64_t window_start;
  int64_t window_end;
  uint32_t depth;
  uint32_t parent_node_id;
};

void WalkOneRoot(const WakeupGraph& graph,
                 uint32_t root_id,
                 tables::CriticalPathWalkTable& out,
                 std::vector<Frame>& stack) {
  if (root_id >= graph.nodes_by_id.size() || !graph.nodes_by_id[root_id]) {
    return;
  }
  const WakeupNode& root = *graph.nodes_by_id[root_id];
  stack.clear();

  // Seed with the root's idle window plus its run. Unknown `idle_dur`
  // collapses the idle half (no lower bound from this node).
  int64_t initial_start = root.ts - root.idle_dur.value_or(0);
  int64_t initial_end = root.ts + root.dur;
  stack.push_back({root_id, initial_start, initial_end, 0, root_id});

  while (!stack.empty()) {
    Frame f = stack.back();
    stack.pop_back();

    if (f.window_start >= f.window_end ||
        f.node_id >= graph.nodes_by_id.size() ||
        !graph.nodes_by_id[f.node_id]) {
      continue;
    }

    const WakeupNode& n = *graph.nodes_by_id[f.node_id];
    // Unset `idle_dur` leaves the idle half open below; clip to the
    // caller's window so the `waker_id` chain still propagates.
    int64_t node_idle_start =
        n.idle_dur.has_value() ? (n.ts - *n.idle_dur) : f.window_start;
    int64_t node_run_end = n.ts + n.dur;

    int64_t eff_start = std::max(f.window_start, node_idle_start);
    int64_t eff_end = std::min(f.window_end, node_run_end);

    // Time predating this node's idle: descend into `prev_id` at the
    // same depth to keep the same-thread chain going backwards.
    if (n.idle_dur.has_value() && f.window_start < node_idle_start &&
        n.prev_id) {
      int64_t prev_window_end = std::min(f.window_end, node_idle_start);
      stack.push_back({*n.prev_id, f.window_start, prev_window_end, f.depth,
                       f.parent_node_id});
    }

    if (eff_start >= eff_end) {
      continue;
    }

    // Idle portion of the effective window. With no `waker_id` (IRQ
    // context, no thread chain to walk) the woken thread is in kernel
    // and self-attributes; otherwise descend into `waker_id` at
    // depth+1 to chain through the cross-thread waker.
    int64_t idle_clip_start = eff_start;
    int64_t idle_clip_end = std::min(eff_end, n.ts);
    if (idle_clip_start < idle_clip_end) {
      if (!n.waker_id && n.prev_id) {
        tables::CriticalPathWalkTable::Row row;
        row.root_id = root_id;
        row.depth = f.depth;
        row.ts = idle_clip_start;
        row.dur = idle_clip_end - idle_clip_start;
        row.blocker_id = f.node_id;
        row.blocker_utid = n.utid;
        row.parent_id = f.parent_node_id;
        out.Insert(row);
      } else if (n.waker_id) {
        stack.push_back({*n.waker_id, idle_clip_start, idle_clip_end,
                         f.depth + 1, f.parent_node_id});
      }
    }

    // Run portion: this thread is on-CPU and is the blocker.
    int64_t run_start = std::max(eff_start, n.ts);
    if (run_start < eff_end) {
      tables::CriticalPathWalkTable::Row row;
      row.root_id = root_id;
      row.depth = f.depth;
      row.ts = run_start;
      row.dur = eff_end - run_start;
      row.blocker_id = f.node_id;
      row.blocker_utid = n.utid;
      row.parent_id = f.parent_node_id;
      out.Insert(row);
    }
  }
}

// Args, in order: WakeupGraph* (from `__intrinsic_wakeup_graph_agg`),
// IntArray* of root ids (from `__intrinsic_array_agg`). Returns a
// `Dataframe*` tagged "TABLE", consumed via `__intrinsic_table_ptr`.
struct CriticalPathWalk : public sqlite::AggregateFunction<CriticalPathWalk> {
  static constexpr char kName[] = "__intrinsic_critical_path_walk";
  static constexpr int kArgCount = 2;
  using UserData = StringPool;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);
    auto out =
        std::make_unique<tables::CriticalPathWalkTable>(GetUserData(ctx));

    auto* graph =
        sqlite::value::Pointer<WakeupGraph>(argv[0], WakeupGraph::kName);
    auto* roots =
        sqlite::value::Pointer<perfetto_sql::IntArray>(argv[1], "ARRAY<LONG>");
    if (!graph || !roots || graph->nodes_by_id.empty() || roots->empty()) {
      return sqlite::result::UniquePointer(
          ctx,
          std::make_unique<dataframe::Dataframe>(std::move(out->dataframe())),
          "TABLE");
    }

    std::vector<Frame> stack;
    for (int64_t raw_root : *roots) {
      auto root_id = static_cast<uint32_t>(raw_root);
      WalkOneRoot(*graph, root_id, *out, stack);
    }
    return sqlite::result::UniquePointer(
        ctx,
        std::make_unique<dataframe::Dataframe>(std::move(out->dataframe())),
        "TABLE");
  }
};

}  // namespace

base::Status RegisterCriticalPathFunctions(PerfettoSqlEngine& engine,
                                           StringPool& pool) {
  RETURN_IF_ERROR(engine.RegisterAggregateFunction<WakeupGraphAgg>(nullptr));
  return engine.RegisterFunction<CriticalPathWalk>(&pool);
}

}  // namespace perfetto::trace_processor
