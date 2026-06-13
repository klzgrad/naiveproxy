/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/storage/trace_storage.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/tables/all_tables_fwd.h"
#include "src/trace_processor/tables/android_tables_py.h"   // IWYU pragma: keep
#include "src/trace_processor/tables/counter_tables_py.h"   // IWYU pragma: keep
#include "src/trace_processor/tables/etm_tables_py.h"       // IWYU pragma: keep
#include "src/trace_processor/tables/flow_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/jit_tables_py.h"       // IWYU pragma: keep
#include "src/trace_processor/tables/memory_tables_py.h"    // IWYU pragma: keep
#include "src/trace_processor/tables/metadata_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/perf_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/profiler_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/sched_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/slice_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/trace_proto_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/track_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/v8_tables_py.h"        // IWYU pragma: keep
#include "src/trace_processor/tables/winscope_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {

// Struct holding the parameters needed to initialize a table.
struct TableInitParams {
  uint32_t column_count;
  const char* const* column_names;
  const dataframe::ColumnSpec* column_specs;
};

template <class Variant, class T>
struct table_init_params;

template <class T, class... Ts>
struct table_init_params<std::variant<Ts...>, T> {
  static constexpr std::array<TableInitParams, sizeof...(Ts)> value = {
      {{decltype(Ts::kSpec)::kColumnCount, Ts::kSpec.column_names.data(),
        Ts::kSpec.column_specs.data()}...}};
};

// Array of initialization parameters for all tables, in the same order as
// AllTables variant. This allows us to initialize all tables in a simple loop
// without template instantiation bloat.
constexpr std::array kTableInitParams =
    table_init_params<tables::AllTables, void>::value;

static_assert(
    std::size(kTableInitParams) == tables::kTableCount,
    "kTableInitParams must have the same number of entries as tables");

}  // namespace

TraceStorage::TraceStorage(const Config&) {
  // Initialize all tables using placement new in a simple loop.
  for (size_t i = 0; i < tables::kTableCount; ++i) {
    const auto& params = kTableInitParams[i];
    new (&tables_storage_[i * sizeof(dataframe::Dataframe)])
        dataframe::Dataframe(&string_pool_, params.column_count,
                             params.column_names, params.column_specs);
  }
  for (uint32_t i = 0; i < variadic_type_ids_.size(); ++i) {
    variadic_type_ids_[i] = InternString(Variadic::kTypeNames[i]);
  }
}

TraceStorage::~TraceStorage() {
  // Destroy all tables in reverse order of construction.
  for (size_t i = tables::kTableCount; i > 0; --i) {
    reinterpret_cast<dataframe::Dataframe*>(
        &tables_storage_[(i - 1) * sizeof(dataframe::Dataframe)])
        ->~Dataframe();
  }
}

uint32_t TraceStorage::SqlStats::RecordQueryBegin(const std::string& query,
                                                  int64_t time_started) {
  if (queries_.size() >= kMaxLogEntries) {
    queries_.pop_front();
    times_started_.pop_front();
    times_first_next_.pop_front();
    times_ended_.pop_front();
    popped_queries_++;
  }
  queries_.push_back(query);
  times_started_.push_back(time_started);
  times_first_next_.push_back(0);
  times_ended_.push_back(0);
  return static_cast<uint32_t>(popped_queries_ + queries_.size() - 1);
}

void TraceStorage::SqlStats::RecordQueryFirstNext(uint32_t row,
                                                  int64_t time_first_next) {
  // This means we've popped this query off the queue of queries before it had
  // a chance to finish. Just silently drop this number.
  if (popped_queries_ > row)
    return;
  uint32_t queue_row = row - popped_queries_;
  PERFETTO_DCHECK(queue_row < queries_.size());
  times_first_next_[queue_row] = time_first_next;
}

void TraceStorage::SqlStats::RecordQueryEnd(uint32_t row, int64_t time_ended) {
  // This means we've popped this query off the queue of queries before it had
  // a chance to finish. Just silently drop this number.
  if (popped_queries_ > row)
    return;
  uint32_t queue_row = row - popped_queries_;
  PERFETTO_DCHECK(queue_row < queries_.size());
  times_ended_[queue_row] = time_ended;
}

}  // namespace perfetto::trace_processor
