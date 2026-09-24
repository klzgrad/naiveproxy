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

#include "src/trace_processor/importers/common/profiler_sample_tracker.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ProfilerSampleTracker::ProfilerSampleTracker(TraceProcessorContext* context)
    : context_(context) {}

tables::ProfilerTaskContextTable::Id ProfilerSampleTracker::InternTaskContext(
    const tables::ProfilerTaskContextTable::Row& row) {
  TaskContextKey key{row.upid, row.utid, row.async_context_id};
  auto [id, inserted] = task_contexts_.Insert(key, {});
  if (inserted) {
    *id = context_->storage->mutable_profiler_task_context_table()
              ->Insert(row)
              .id;
  }
  return *id;
}

tables::ProfilerExecutionContextTable::Id
ProfilerSampleTracker::InternExecutionContext(
    const tables::ProfilerExecutionContextTable::Row& row) {
  ExecutionContextKey key{row.ucpu, row.cpu_mode};
  auto [id, inserted] = execution_contexts_.Insert(key, {});
  if (inserted) {
    *id = context_->storage->mutable_profiler_execution_context_table()
              ->Insert(row)
              .id;
  }
  return *id;
}

tables::ProfilerSampleTable::Id ProfilerSampleTracker::AddSample(
    const tables::ProfilerSampleTable::Row& row) {
  PERFETTO_DCHECK(row.ts >= last_ts_);
  last_ts_ = row.ts;
  return context_->storage->mutable_profiler_sample_table()->Insert(row).id;
}

std::optional<uint32_t> ProfilerSampleTracker::AddCounterSet(
    const std::vector<CounterId>& counter_ids) {
  if (counter_ids.empty()) {
    return std::nullopt;
  }
  auto* table = context_->storage->mutable_profiler_counter_set_table();
  uint32_t set_id = static_cast<uint32_t>(table->row_count());
  for (CounterId counter_id : counter_ids) {
    tables::ProfilerCounterSetTable::Row row;
    row.counter_set_id = set_id;
    row.counter_id = counter_id;
    table->Insert(row);
  }
  return set_id;
}

}  // namespace perfetto::trace_processor
