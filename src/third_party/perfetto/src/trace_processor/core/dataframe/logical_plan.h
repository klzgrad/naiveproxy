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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_LOGICAL_PLAN_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_LOGICAL_PLAN_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"

namespace perfetto::trace_processor::core::dataframe {

// The sequence of operations a query performs, in the order it performs them.
//
// A logical plan says *what* a query does. Every operation states its meaning
// in terms the caller asked for -- a column, a predicate, a filter value slot
// -- and carries, separately, the strategy the planner chose to satisfy it.
// A backend that wants the planner's access-path choice reads the strategy; a
// backend which picks its own reads only the predicate and ignores it.
//
// Nothing here refers to the bytecode interpreter. Registers, scratch buffers,
// null bitvectors and index materialization are all decisions about *how* to
// run an operation and belong to whatever lowers this plan.
namespace logical {

// The number of rows flowing into or out of an operation.
struct RowEstimate {
  // The most rows there can possibly be.
  uint32_t max = 0;

  // The number of rows the planner expects.
  uint32_t estimated = 0;

  bool operator==(const RowEstimate& o) const {
    return max == o.max && estimated == o.estimated;
  }
};

// How the planner chose to evaluate a filter. This is an annotation, not part
// of the filter's meaning: dropping it leaves the predicate intact.
//
// Binary search over the sorted values of the column. Which end(s) of the
// range move follows from the operation, so it is not restated here.
struct BinarySearch {};

// Binary search specialized for a SetId-sorted column.
struct SetIdSortedSearch {};

// Lookup through the column's SmallValueEq specialized storage.
struct SmallValueLookup {};

// Scan of a contiguous row range, materializing the matches.
struct RangeScan {};

// Scan of an already materialized list of rows.
struct IndexListScan {};

using FilterStrategy = std::variant<BinarySearch,
                                    SetIdSortedSearch,
                                    SmallValueLookup,
                                    RangeScan,
                                    IndexListScan>;

// Reads all the rows of the dataframe. Always the first operation.
struct Scan {
  RowEstimate rows;
};

// Applies one predicate to the rows.
struct Filter {
  // The predicate. This is the operation's meaning and is what a backend
  // choosing its own access path should read.
  uint32_t col = 0;
  Op op{Eq{}};

  // Slot the caller fills with the value to compare against, at execution
  // time. Assigned here because it is part of the contract with the caller,
  // not a property of any particular lowering.
  std::optional<uint32_t> value_index;

  // Properties of the column the strategy was chosen from, so that a backend
  // need not consult the dataframe to interpret this operation.
  StorageType storage{Id{}};
  Nullability nullability{NonNull{}};

  FilterStrategy strategy{IndexListScan{}};

  RowEstimate rows_in;

  // A filter on a nullable column drops null rows before comparing values.
  // Equal to rows_in.estimated when the column cannot hold nulls.
  uint32_t estimated_rows_after_null_prune = 0;

  RowEstimate rows_out;
};

// Applies a run of equality and IN predicates through one index. The
// predicates are given in the index's own column order, each narrowing the
// rows the next one considers.
struct IndexFilter {
  struct Predicate {
    uint32_t col = 0;
    Op op{Eq{}};
    std::optional<uint32_t> value_index;
    StorageType storage{Id{}};
    Nullability nullability{NonNull{}};
    RowEstimate rows_in;
    RowEstimate rows_out;
  };
  uint32_t index = 0;
  std::vector<Predicate> predicates;
};

// The result is provably empty, so no rows need be examined.
struct Empty {};

// Keeps one row per distinct combination of the given columns.
struct Distinct {
  std::vector<uint32_t> cols;
  RowEstimate rows_in;
  RowEstimate rows_out;
};

// Reverses the row order. Emitted when the data is already sorted on the key,
// but opposite to the direction asked for.
struct Reverse {};

// Orders the rows by the given keys.
struct Sort {
  std::vector<SortSpec> keys;
  RowEstimate rows;
};

// Takes the single row with the smallest or largest value of a column,
// i.e. an ordering followed by a limit of one.
struct MinMax {
  uint32_t col = 0;
  SortDirection direction = SortDirection::kAscending;
  RowEstimate rows_in;
  RowEstimate rows_out;
};

// Drops `offset` rows from the front and keeps at most `limit` of the rest.
struct Limit {
  std::optional<uint32_t> limit;
  std::optional<uint32_t> offset;
  RowEstimate rows_in;
  RowEstimate rows_out;
};

// Produces the columns the caller asked for. Always the final operation.
struct Output {
  // Bit i set means column i is read. Columns from 64 upwards share bit 63.
  uint64_t cols_used = 0;
};

using Operation = std::variant<Scan,
                               Filter,
                               IndexFilter,
                               Empty,
                               Distinct,
                               Reverse,
                               Sort,
                               MinMax,
                               Limit,
                               Output>;

}  // namespace logical

// A query expressed as a sequence of logical operations.
struct LogicalPlan {
  std::vector<logical::Operation> ops;

  // Number of filter value slots the caller must supply.
  uint32_t filter_value_count = 0;

  // Rows the whole plan produces.
  logical::RowEstimate rows;

  // Renders the plan one operation per line. For debugging and tests; not
  // parsed back.
  std::string ToString() const;
};

// Chooses how a query should be run and records that as a LogicalPlan.
//
// Everything this class decides -- the order filters are applied in, which
// strategy serves each one, whether an index is worth using, whether a sort
// can be skipped -- is a choice about the query, not about any particular way
// of executing it.
class LogicalPlanner {
 public:
  // `specs` is reordered into the order the filters will be applied, and each
  // spec that the plan can handle has its value_index assigned.
  static base::StatusOr<LogicalPlan> Plan(
      uint32_t row_count,
      const std::vector<std::shared_ptr<Column>>& columns,
      const std::vector<Index>& indexes,
      std::vector<FilterSpec>& specs,
      const std::vector<DistinctSpec>& distinct,
      const std::vector<SortSpec>& sort_specs,
      const LimitSpec& limit_spec,
      uint64_t cols_used);
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_LOGICAL_PLAN_H_
