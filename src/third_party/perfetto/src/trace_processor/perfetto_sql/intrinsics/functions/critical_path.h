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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CRITICAL_PATH_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CRITICAL_PATH_H_

#include "perfetto/base/status.h"

namespace perfetto::trace_processor {

class PerfettoSqlEngine;
class StringPool;

// Registers the SQLite intrinsics backing
// `sched.thread_executing_span`'s critical-path computation:
//   - __intrinsic_wakeup_graph_agg: aggregate that builds a WakeupGraph
//       from per-row inputs.
//   - __intrinsic_critical_path_walk: scalar that walks the graph for
//       each root id and returns a layered Dataframe with columns
//       (root_id, depth, ts, dur, blocker_id, blocker_utid, parent_id);
//       callers collapse to one blocker per `(root_id, ts)` via
//       `_intervals_flatten`.
base::Status RegisterCriticalPathFunctions(PerfettoSqlEngine& engine,
                                           StringPool& pool);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CRITICAL_PATH_H_
