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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROFILER_SAMPLE_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROFILER_SAMPLE_TRACKER_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Writes the profiler_sample table: the generic sampler table shared by all
// profiler sources (linux perf, chrome, instruments, the StackSample packet,
// ...).
class ProfilerSampleTracker {
 public:
  explicit ProfilerSampleTracker(TraceProcessorContext* context);

  tables::ProfilerTaskContextTable::Id InternTaskContext(
      const tables::ProfilerTaskContextTable::Row& row);
  tables::ProfilerExecutionContextTable::Id InternExecutionContext(
      const tables::ProfilerExecutionContextTable::Row& row);

  // Inserts a sample row. `row.ts` must be monotonic across all sources:
  // profiler_sample.ts is a sorted column and sortedness is an unchecked
  // promise to the query planner, so this fails fast in debug builds.
  tables::ProfilerSampleTable::Id AddSample(
      const tables::ProfilerSampleTable::Row& row);

  // Groups the given counter rows (recorded at a single sample point) into a
  // new counter set. Returns std::nullopt if `counter_ids` is empty.
  std::optional<uint32_t> AddCounterSet(
      const std::vector<CounterId>& counter_ids);

 private:
  struct TaskContextKey {
    std::optional<UniquePid> upid;
    std::optional<UniqueTid> utid;
    std::optional<tables::ProfilerAsyncContextTable::Id> async_context_id;

    bool operator==(const TaskContextKey& other) const {
      return upid == other.upid && utid == other.utid &&
             async_context_id == other.async_context_id;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const TaskContextKey& key) {
      return H::Combine(std::move(h), key.upid, key.utid, key.async_context_id);
    }
  };

  struct ExecutionContextKey {
    std::optional<uint32_t> ucpu;
    std::optional<StringId> cpu_mode;

    bool operator==(const ExecutionContextKey& other) const {
      return ucpu == other.ucpu && cpu_mode == other.cpu_mode;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const ExecutionContextKey& key) {
      return H::Combine(std::move(h), key.ucpu, key.cpu_mode);
    }
  };

  TraceProcessorContext* const context_;
  int64_t last_ts_ = std::numeric_limits<int64_t>::min();
  base::FlatHashMap<TaskContextKey,
                    tables::ProfilerTaskContextTable::Id,
                    base::MurmurHash<TaskContextKey>>
      task_contexts_;
  base::FlatHashMap<ExecutionContextKey,
                    tables::ProfilerExecutionContextTable::Id,
                    base::MurmurHash<ExecutionContextKey>>
      execution_contexts_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROFILER_SAMPLE_TRACKER_H_
