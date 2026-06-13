// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/intrinsics/functions/perf_counter.h"

#include <cstdint>
#include <memory>
#include <optional>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/counter_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"

namespace perfetto::trace_processor {

// A reusable cursor for looking up perf counters by counter_set_id.
// This avoids creating a new cursor for each lookup.
class PerfCounterExtractor {
 public:
  explicit PerfCounterExtractor(
      const tables::PerfCounterSetTable& perf_counter_set_table)
      : cursor_(perf_counter_set_table.CreateCursor({dataframe::FilterSpec{
            tables::PerfCounterSetTable::ColumnIndex::perf_counter_set_id, 0,
            dataframe::Eq{}, std::nullopt}})) {}

  // Sets up the cursor for the given counter_set_id and executes the query.
  void SetCounterSetId(uint32_t counter_set_id) {
    cursor_.SetFilterValueUnchecked(0, counter_set_id);
    cursor_.Execute();
  }

  bool Eof() const { return cursor_.Eof(); }
  void Next() { cursor_.Next(); }

  // Access to the underlying cursor for retrieving values.
  const tables::PerfCounterSetTable::ConstCursor& cursor() const {
    return cursor_;
  }

 private:
  tables::PerfCounterSetTable::ConstCursor cursor_;
};

// Context constructor - defined here where PerfCounterExtractor is complete.
PerfCounterForSampleFunction::Context::Context(TraceStorage* s)
    : storage(s),
      extractor(
          std::make_unique<PerfCounterExtractor>(s->perf_counter_set_table())) {
}

// Context destructor - must be defined here where PerfCounterExtractor is
// complete.
PerfCounterForSampleFunction::Context::~Context() = default;

// static
void PerfCounterForSampleFunction::Step(sqlite3_context* ctx,
                                        int,
                                        sqlite3_value** argv) {
  sqlite::Type sample_id_type = sqlite::value::Type(argv[0]);
  sqlite::Type counter_name_type = sqlite::value::Type(argv[1]);

  // If the sample_id is null, return null.
  if (sample_id_type == sqlite::Type::kNull) {
    return;
  }

  if (sample_id_type != sqlite::Type::kInteger) {
    return sqlite::result::Error(ctx,
                                 "__intrinsic_perf_counter_for_sample: 1st "
                                 "argument should be sample id");
  }

  if (counter_name_type != sqlite::Type::kText) {
    return sqlite::result::Error(
        ctx,
        "__intrinsic_perf_counter_for_sample: 2nd argument should be counter "
        "name");
  }

  auto sample_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* counter_name = sqlite::value::Text(argv[1]);

  auto* user_data = GetUserData(ctx);
  auto* storage = user_data->storage;

  // Look up the sample to get counter_set_id.
  const auto& perf_sample_table = storage->perf_sample_table();
  if (sample_id >= perf_sample_table.row_count()) {
    return sqlite::result::Error(
        ctx, "__intrinsic_perf_counter_for_sample: invalid sample id");
  }

  auto counter_set_id = perf_sample_table[sample_id].counter_set_id();
  if (!counter_set_id.has_value()) {
    // No counter set for this sample.
    return;
  }

  // Look up the counter name in the string pool first.
  // If it's not in the pool, no track can have this name.
  std::optional<StringPool::Id> counter_name_id =
      storage->string_pool().GetId(counter_name);
  if (!counter_name_id.has_value()) {
    // Name not in pool, so no match possible.
    return;
  }

  // Use the memoized cursor to iterate through the counter set.
  const auto& counter_table = storage->counter_table();
  const auto& track_table = storage->track_table();

  user_data->extractor->SetCounterSetId(*counter_set_id);
  for (; !user_data->extractor->Eof(); user_data->extractor->Next()) {
    const auto& cursor = user_data->extractor->cursor();

    // Get the counter row.
    CounterId counter_id = cursor.counter_id();
    if (counter_id.value >= counter_table.row_count()) {
      continue;
    }

    // Get the track for this counter.
    TrackId track_id = counter_table[counter_id.value].track_id();
    if (track_id.value >= track_table.row_count()) {
      continue;
    }

    // Compare string IDs directly (O(1) instead of string comparison).
    if (track_table[track_id.value].name() == *counter_name_id) {
      // Found matching counter, return its value.
      double value = counter_table[counter_id.value].value();
      return sqlite::result::Double(ctx, value);
    }
  }

  // No matching counter found.
  return;
}

}  // namespace perfetto::trace_processor
