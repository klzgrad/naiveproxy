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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/create_intervals.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/sorted_timestamps.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor::perfetto_sql {
namespace {

using ColType = dataframe::AdhocDataframeBuilder::ColumnType;

// Given two sorted collections of timestamps (starts and ends), creates
// intervals by pairing each start with the first end that comes strictly after
// it.
//
// Algorithm: O(n + m) two-pointer scan over sorted arrays.
// For each start timestamp, finds the minimum end timestamp that is strictly
// greater than the start. Multiple starts may be paired with the same end.
// Starts with no matching end are excluded from the output.
struct IntervalCreate : public sqlite::Function<IntervalCreate> {
  static constexpr char kName[] = "__intrinsic_interval_create";
  static constexpr int kArgCount = 2;

  struct UserData {
    StringPool* pool;
  };

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);

    auto* starts = sqlite::value::Pointer<SortedTimestamps>(
        argv[0], SortedTimestamps::kName);
    auto* ends = sqlite::value::Pointer<SortedTimestamps>(
        argv[1], SortedTimestamps::kName);

    std::vector<std::string> col_names{"ts", "dur"};
    std::vector<ColType> col_types{ColType::kInt64, ColType::kInt64};

    dataframe::AdhocDataframeBuilder builder(
        col_names, GetUserData(ctx)->pool,
        dataframe::AdhocDataframeBuilder::Options{
            col_types, dataframe::NullabilityType::kDenseNull});

    if (!starts || !ends || starts->timestamps.empty() ||
        ends->timestamps.empty()) {
      SQLITE_ASSIGN_OR_RETURN(ctx, auto ret_table, std::move(builder).Build());
      return sqlite::result::UniquePointer(
          ctx, std::make_unique<dataframe::Dataframe>(std::move(ret_table)),
          "TABLE");
    }

    const auto& start_ts = starts->timestamps;
    const auto& end_ts = ends->timestamps;

    // Two-pointer matching: O(n + m).
    // Both arrays are already sorted (guaranteed by ORDER BY in the SQL macro).
    // For each start, we find the first end strictly greater than it.
    // Since starts are sorted, the end pointer only advances forward.
    size_t end_idx = 0;
    for (size_t i = 0; i < start_ts.size(); ++i) {
      while (end_idx < end_ts.size() && end_ts[end_idx] <= start_ts[i]) {
        ++end_idx;
      }
      if (end_idx >= end_ts.size()) {
        break;
      }
      builder.PushNonNullUnchecked(0, start_ts[i]);
      builder.PushNonNullUnchecked(1, end_ts[end_idx] - start_ts[i]);
    }

    SQLITE_ASSIGN_OR_RETURN(ctx, auto ret_table, std::move(builder).Build());
    return sqlite::result::UniquePointer(
        ctx, std::make_unique<dataframe::Dataframe>(std::move(ret_table)),
        "TABLE");
  }
};

}  // namespace

base::Status RegisterIntervalCreateFunctions(PerfettoSqlEngine& engine,
                                             StringPool* pool) {
  return engine.RegisterFunction<IntervalCreate>(
      std::make_unique<IntervalCreate::UserData>(
          IntervalCreate::UserData{pool}));
}

}  // namespace perfetto::trace_processor::perfetto_sql
