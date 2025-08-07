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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/interval_intersect.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/interval_intersector.h"
#include "src/trace_processor/containers/interval_tree.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/partitioned_intervals.h"
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

constexpr uint32_t kArgCols = 2;
constexpr uint32_t kIdCols = 5;
constexpr uint32_t kPartitionColsOffset = kArgCols + kIdCols;

using Intervals = std::vector<Interval>;
using ColType = dataframe::AdhocDataframeBuilder::ColumnType;

struct MultiIndexInterval {
  uint64_t start;
  uint64_t end;
  std::vector<int64_t> idx_in_table;
};

ColType FromSqlValueTypeToBuilderType(SqlValue::Type type) {
  switch (type) {
    case SqlValue::kLong:
      return ColType::kInt64;
    case SqlValue::kDouble:
      return ColType::kDouble;
    case SqlValue::kString:
      return ColType::kString;
    case SqlValue::kNull:
    case SqlValue::kBytes:
      PERFETTO_FATAL("Wrong type");
  }
  PERFETTO_FATAL("For gcc");
}

base::StatusOr<std::vector<ColType>> GetPartitionsSqlType(
    const Partitions& partitions) {
  auto partition_it = partitions.GetIterator();
  if (!partition_it) {
    return std::vector<ColType>();
  }
  auto p_count = static_cast<uint32_t>(partition_it.value().sql_values.size());

  // We expect this loop to be broken very early, but it has to be
  // implemented as loop as we can't deduce the type partition with NULL value.
  std::vector<std::optional<ColType>> types(p_count);
  for (; partition_it; ++partition_it) {
    bool has_unknown_type = false;
    for (uint32_t i = 0; i < p_count; i++) {
      auto& type = types[i];
      if (type) {
        continue;
      }
      if (partition_it.value().sql_values[i].is_null()) {
        has_unknown_type = true;
        continue;
      }
      type = FromSqlValueTypeToBuilderType(
          partition_it.value().sql_values[i].type);
    }
    if (!has_unknown_type) {
      // If we have all types known, we can break the loop.
      break;
    }
  }
  std::vector<ColType> result;
  for (uint32_t i = 0; i < p_count; i++) {
    auto& type = types[i];
    if (!type) {
      return base::ErrStatus("Partition has unknown type in column %u", i);
    }
    result.push_back(*type);
  }
  return result;
}

// Pushes partition into the result table. Returns the number of rows pushed.
// All operations in this function are done on sets of intervals from each
// table that correspond to the same partition.
base::StatusOr<uint32_t> PushPartition(
    StringPool* string_pool,
    dataframe::AdhocDataframeBuilder& builder,
    const std::vector<Partition*>& intervals_in_table) {
  size_t tables_count = intervals_in_table.size();

  // Sort `tables_order` from the smallest to the biggest.
  std::vector<uint32_t> tables_order(tables_count);
  std::iota(tables_order.begin(), tables_order.end(), 0);
  std::sort(tables_order.begin(), tables_order.end(),
            [intervals_in_table](const uint32_t idx_a, const uint32_t idx_b) {
              return intervals_in_table[idx_a]->intervals.size() <
                     intervals_in_table[idx_b]->intervals.size();
            });
  uint32_t idx_of_smallest_part = tables_order.front();
  PERFETTO_DCHECK(!intervals_in_table[idx_of_smallest_part]->intervals.empty());

  // Trivially translate intervals table with the smallest partition to
  // `MultiIndexIntervals`.
  std::vector<MultiIndexInterval> last_results;
  last_results.reserve(intervals_in_table.back()->intervals.size());
  for (const auto& interval :
       intervals_in_table[idx_of_smallest_part]->intervals) {
    MultiIndexInterval m_int;
    m_int.start = interval.start;
    m_int.end = interval.end;
    m_int.idx_in_table.resize(tables_count);
    m_int.idx_in_table[idx_of_smallest_part] = interval.id;
    last_results.push_back(m_int);
  }

  // Create an interval tree on all tables except the smallest - the first one.
  std::vector<MultiIndexInterval> overlaps_with_this_table;
  overlaps_with_this_table.reserve(intervals_in_table.back()->intervals.size());
  for (uint32_t i = 1; i < tables_count && !last_results.empty(); i++) {
    overlaps_with_this_table.clear();
    uint32_t table_idx = tables_order[i];

    IntervalIntersector::Mode mode = IntervalIntersector::DecideMode(
        intervals_in_table[table_idx]->is_nonoverlapping,
        static_cast<uint32_t>(last_results.size()));
    IntervalIntersector cur_intersector(
        intervals_in_table[table_idx]->intervals, mode);
    for (const auto& prev_result : last_results) {
      Intervals new_overlaps;
      cur_intersector.FindOverlaps(prev_result.start, prev_result.end,
                                   new_overlaps);
      for (const auto& overlap : new_overlaps) {
        MultiIndexInterval m_int;
        m_int.idx_in_table = prev_result.idx_in_table;
        m_int.idx_in_table[table_idx] = overlap.id;
        m_int.start = overlap.start;
        m_int.end = overlap.end;
        overlaps_with_this_table.push_back(std::move(m_int));
      }
    }

    last_results = std::move(overlaps_with_this_table);
  }

  auto rows_count = static_cast<uint32_t>(last_results.size());
  for (uint32_t i = 0; i < rows_count; i++) {
    const MultiIndexInterval& interval = last_results[i];
    builder.PushNonNullUnchecked(0, static_cast<int64_t>(interval.start));
    builder.PushNonNullUnchecked(1, static_cast<int64_t>(interval.end) -
                                        static_cast<int64_t>(interval.start));
    for (uint32_t j = 0; j < tables_count; j++) {
      builder.PushNonNullUnchecked(j + kArgCols, interval.idx_in_table[j]);
    }
  }
  for (uint32_t i = 0; i < intervals_in_table[0]->sql_values.size(); i++) {
    const SqlValue& part_val = intervals_in_table[0]->sql_values[i];
    switch (part_val.type) {
      case SqlValue::kLong:
        if (!builder.PushNonNull(i + kPartitionColsOffset, part_val.long_value,
                                 rows_count)) {
          return builder.status();
        }
        continue;
      case SqlValue::kDouble:
        if (!builder.PushNonNull(i + kPartitionColsOffset,
                                 part_val.double_value, rows_count)) {
          return builder.status();
        }
        continue;
      case SqlValue::kString:
        if (!builder.PushNonNull(
                i + kPartitionColsOffset,
                string_pool->InternString(part_val.string_value), rows_count)) {
          return builder.status();
        }
        continue;
      case SqlValue::kNull:
        builder.PushNull(i + kPartitionColsOffset, rows_count);
        continue;
      case SqlValue::kBytes:
        PERFETTO_FATAL("Invalid partition type");
    }
  }
  return static_cast<uint32_t>(last_results.size());
}

struct IntervalIntersect : public SqliteFunction<IntervalIntersect> {
  static constexpr char kName[] = "__intrinsic_interval_intersect";
  // Two tables that are being intersected.
  // TODO(mayzner): Support more tables.
  static constexpr int kArgCount = -1;

  struct UserDataContext {
    PerfettoSqlEngine* engine;
    StringPool* pool;
  };

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc >= 2);
    auto tabc = static_cast<size_t>(argc - 1);
    if (tabc > kIdCols) {
      return sqlite::result::Error(
          ctx, "interval intersect: Can intersect at most 5 tables");
    }
    const char* partition_list = sqlite::value::Text(argv[argc - 1]);
    if (!partition_list) {
      return sqlite::result::Error(
          ctx, "interval intersect: column list cannot be null");
    }

    // Get column names of return columns.
    std::vector<std::string> ret_col_names{"ts", "dur"};
    for (uint32_t i = 0; i < kIdCols; i++) {
      ret_col_names.push_back(base::StackString<32>("id_%u", i).ToStdString());
    }
    std::vector<std::string> partition_columns =
        base::SplitString(base::StripChars(partition_list, "()", ' '), ",");
    if (partition_columns.size() > 4) {
      return sqlite::result::Error(
          ctx, "interval intersect: Can take at most 4 partitions.");
    }
    for (const auto& c : partition_columns) {
      std::string p_col_name = base::TrimWhitespace(c);
      if (!p_col_name.empty()) {
        ret_col_names.push_back(p_col_name);
      }
    }

    // Get data from of each table.
    std::vector<PartitionedTable*> tables(tabc);
    std::vector<Partitions*> t_partitions(tabc);

    for (uint32_t i = 0; i < tabc; i++) {
      tables[i] = sqlite::value::Pointer<PartitionedTable>(
          argv[i], PartitionedTable::kName);

      // If any of the tables is empty the intersection with it also has to be
      // empty.
      if (!tables[i] || tables[i]->partitions_map.size() == 0) {
        dataframe::AdhocDataframeBuilder builder(ret_col_names,
                                                 GetUserData(ctx)->pool);
        SQLITE_ASSIGN_OR_RETURN(ctx, dataframe::Dataframe ret_table,
                                std::move(builder).Build());
        return sqlite::result::UniquePointer(
            ctx, std::make_unique<dataframe::Dataframe>(std::move(ret_table)),
            "TABLE");
      }
      t_partitions[i] = &tables[i]->partitions_map;
    }

    std::vector<ColType> col_types(kArgCols + tabc);
    col_types.resize(kArgCols + kIdCols, ColType::kInt64);

    Partitions* p_values = &tables[0]->partitions_map;
    SQLITE_ASSIGN_OR_RETURN(ctx, std::vector<ColType> p_types,
                            GetPartitionsSqlType(*p_values));
    col_types.insert(col_types.end(), p_types.begin(), p_types.end());

    // Partitions will be taken from the table which has the least number of
    // them.
    auto min_el = std::min_element(t_partitions.begin(), t_partitions.end(),
                                   [](const auto& t_a, const auto& t_b) {
                                     return t_a->size() < t_b->size();
                                   });

    dataframe::AdhocDataframeBuilder builder(ret_col_names,
                                             GetUserData(ctx)->pool, col_types);
    auto t_least_partitions =
        static_cast<uint32_t>(std::distance(t_partitions.begin(), min_el));

    // The only partitions we should look at are partitions from the table
    // with the least partitions.
    const Partitions* p_intervals = t_partitions[t_least_partitions];

    // For each partition insert into table.
    uint32_t rows = 0;
    for (auto p_it = p_intervals->GetIterator(); p_it; ++p_it) {
      std::vector<Partition*> cur_partition_in_table;
      bool all_have_p = true;

      // From each table get all vectors of intervals.
      for (uint32_t i = 0; i < tabc; i++) {
        Partitions* t = t_partitions[i];
        if (auto* found = t->Find(p_it.key()); found) {
          cur_partition_in_table.push_back(found);
        } else {
          all_have_p = false;
          break;
        }
      }

      // Only push into the table if all tables have this partition present.
      if (all_have_p) {
        SQLITE_ASSIGN_OR_RETURN(ctx, uint32_t pushed_rows,
                                PushPartition(GetUserData(ctx)->pool, builder,
                                              cur_partition_in_table));
        rows += pushed_rows;
      }
    }

    // Fill the dummy id columns with nulls.
    for (auto i = static_cast<uint32_t>(tabc); i < kIdCols; i++) {
      builder.PushNull(i + kArgCols, rows);
    }

    SQLITE_ASSIGN_OR_RETURN(ctx, dataframe::Dataframe ret_tab,
                            std::move(builder).Build());
    return sqlite::result::UniquePointer(
        ctx, std::make_unique<dataframe::Dataframe>(std::move(ret_tab)),
        "TABLE");
  }
};

}  // namespace

base::Status RegisterIntervalIntersectFunctions(PerfettoSqlEngine& engine,
                                                StringPool* pool) {
  return engine.RegisterSqliteFunction<IntervalIntersect>(
      std::make_unique<IntervalIntersect::UserDataContext>(
          IntervalIntersect::UserDataContext{&engine, pool}));
}

}  // namespace perfetto::trace_processor::perfetto_sql
