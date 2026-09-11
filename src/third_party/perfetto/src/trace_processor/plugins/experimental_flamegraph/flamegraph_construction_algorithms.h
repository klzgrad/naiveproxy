/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_FLAMEGRAPH_FLAMEGRAPH_CONSTRUCTION_ALGORITHMS_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_FLAMEGRAPH_FLAMEGRAPH_CONSTRUCTION_ALGORITHMS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_fwd.h"

namespace perfetto::trace_processor {

class PerfettoSqlConnection;

// Represents a time boundary for a column.
struct TimeConstraints {
  dataframe::Op op;
  int64_t value;
};

// Builds the heap graph ('graph') flamegraph for the dump (ts, upid): expands
// the shortest-path object tree with SQL (graphs.search + heap graph tables)
// and runs it through the shared __intrinsic_flamegraph pipeline. Returns an
// empty table when the dump has no roots and was not truncated; the caller
// treats that as an error.
std::unique_ptr<tables::ExperimentalFlamegraphTable> BuildHeapGraphFlamegraph(
    PerfettoSqlConnection* connection,
    TraceStorage* storage,
    int64_t ts,
    UniquePid upid);

std::unique_ptr<tables::ExperimentalFlamegraphTable> BuildHeapProfileFlamegraph(
    TraceStorage* storage,
    UniquePid upid,
    int64_t timestamp);

std::unique_ptr<tables::ExperimentalFlamegraphTable>
BuildNativeCallStackSamplingFlamegraph(
    TraceStorage* storage,
    std::optional<UniquePid> upid,
    std::optional<std::string> upid_group,
    const std::vector<TimeConstraints>& time_constraints);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_FLAMEGRAPH_FLAMEGRAPH_CONSTRUCTION_ALGORITHMS_H_
