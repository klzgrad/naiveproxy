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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_WAKEUP_GRAPH_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_WAKEUP_GRAPH_H_

#include <cstdint>
#include <optional>
#include <vector>

namespace perfetto::trace_processor::perfetto_sql {

// One entry in the wakeup graph: a thread's transition from idle to
// runnable, plus the run that follows. Mirrors the columns of the
// `_wakeup_graph` stdlib table.
struct WakeupNode {
  uint32_t utid = 0;
  // The thread is running across [ts, ts + dur). The preceding idle
  // period is [ts - *idle_dur, ts) when `idle_dur` is set; otherwise
  // the idle period has no lower bound from this node.
  int64_t ts = 0;
  int64_t dur = 0;
  std::optional<int64_t> idle_dur;
  // Wakeup-graph entry of the thread that woke this one. Empty when
  // no waker is recorded.
  std::optional<uint32_t> waker_id;
  // Previous wakeup-graph entry on this thread. Empty at the first
  // entry on a thread.
  std::optional<uint32_t> prev_id;
};

// Pointer-tagged value consumed by `__intrinsic_critical_path_walk`.
// Direct id indexing keeps lookup O(1); gaps in the id space hold
// default `std::nullopt`. Wakeup-graph ids derive from thread_state
// ids and are dense in practice, so the per-slot overhead is fine.
struct WakeupGraph {
  static constexpr char kName[] = "WAKEUP_GRAPH";

  std::vector<std::optional<WakeupNode>> nodes_by_id;
};

}  // namespace perfetto::trace_processor::perfetto_sql

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_WAKEUP_GRAPH_H_
