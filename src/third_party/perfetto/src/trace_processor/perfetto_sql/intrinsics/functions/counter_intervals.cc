/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/counter_intervals.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/counter.h"
#include "src/trace_processor/sqlite/bindings/sqlite_bind.h"
#include "src/trace_processor/sqlite/bindings/sqlite_column.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_stmt.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor::perfetto_sql {
namespace {

struct CounterIntervals : public sqlite::Function<CounterIntervals> {
  static constexpr char kName[] = "__intrinsic_counter_intervals";
  static constexpr int kArgCount = 3;

  struct UserData {
    PerfettoSqlEngine* engine;
    StringPool* pool;
  };

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);
    const char* leading_str = sqlite::value::Text(argv[0]);
    if (!leading_str) {
      return sqlite::result::Error(
          ctx, "interval intersect: column list cannot be null");
    }

    // TODO(mayzner): Support 'lagging'.
    if (base::CaseInsensitiveEqual("lagging", leading_str)) {
      return sqlite::result::Error(
          ctx, "interval intersect: 'lagging' is not implemented");
    }
    if (!base::CaseInsensitiveEqual("leading", leading_str)) {
      return sqlite::result::Error(ctx,
                                   "interval intersect: second argument has to "
                                   "be either 'leading' or 'lagging");
    }

    int64_t trace_end = sqlite::value::Int64(argv[1]);

    // Get column names of return columns.
    std::vector<std::string> ret_col_names{
        "id", "ts", "dur", "track_id", "value", "next_value", "delta_value"};
    using CT = dataframe::AdhocDataframeBuilder::ColumnType;
    std::vector<CT> col_types{
        CT::kInt64,   // id
        CT::kInt64,   // ts,
        CT::kInt64,   // dur
        CT::kInt64,   // track_id
        CT::kDouble,  // value
        CT::kDouble,  // next_value
        CT::kDouble,  // delta_value
    };
    dataframe::AdhocDataframeBuilder builder(ret_col_names,
                                             GetUserData(ctx)->pool, col_types);

    auto* partitioned_counter = sqlite::value::Pointer<PartitionedCounter>(
        argv[2], PartitionedCounter::kName);
    if (!partitioned_counter) {
      SQLITE_ASSIGN_OR_RETURN(ctx, auto ret_table, std::move(builder).Build());
      return sqlite::result::UniquePointer(
          ctx, std::make_unique<dataframe::Dataframe>(std::move(ret_table)),
          "TABLE");
    }

    for (auto track_counter = partitioned_counter->partitions_map.GetIterator();
         track_counter; ++track_counter) {
      int64_t track_id = track_counter.key();
      const auto& cols = track_counter.value();
      size_t r_count = cols.id.size();

      // Id
      for (size_t i = 0; i < r_count; i++) {
        builder.PushNonNullUnchecked(0, cols.id[i]);
      }
      for (size_t i = 0; i < r_count; i++) {
        builder.PushNonNullUnchecked(1, cols.ts[i]);
      }

      // Dur
      for (size_t i = 0; i < r_count - 1; i++) {
        builder.PushNonNullUnchecked(2, cols.ts[i + 1] - cols.ts[i]);
      }
      builder.PushNonNullUnchecked(2, trace_end - cols.ts.back());

      // Track id
      for (size_t i = 0; i < r_count; i++) {
        builder.PushNonNullUnchecked(3, track_id);
      }
      // Value
      for (size_t i = 0; i < r_count; i++) {
        builder.PushNonNullUnchecked(4, cols.val[i]);
      }

      // Next value
      for (size_t i = 0; i < r_count - 1; i++) {
        builder.PushNonNullUnchecked(5, cols.val[i + 1]);
      }
      builder.PushNull(5);

      // Delta value
      builder.PushNull(6);
      for (size_t i = 0; i < r_count - 1; i++) {
        builder.PushNonNullUnchecked(6, cols.val[i + 1] - cols.val[i]);
      }
    }

    SQLITE_ASSIGN_OR_RETURN(ctx, auto tab, std::move(builder).Build());
    return sqlite::result::UniquePointer(
        ctx, std::make_unique<dataframe::Dataframe>(std::move(tab)), "TABLE");
  }
};

}  // namespace

base::Status RegisterCounterIntervalsFunctions(PerfettoSqlEngine& engine,
                                               StringPool* pool) {
  return engine.RegisterFunction<CounterIntervals>(
      std::make_unique<CounterIntervals::UserData>(
          CounterIntervals::UserData{&engine, pool}));
}

}  // namespace perfetto::trace_processor::perfetto_sql
