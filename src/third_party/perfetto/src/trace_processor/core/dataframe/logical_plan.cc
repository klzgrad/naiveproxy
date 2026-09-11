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

#include "src/trace_processor/core/dataframe/logical_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/core/common/op_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"

namespace perfetto::trace_processor::core::dataframe {

namespace {

// Assumed number of distinct values matched by an IN filter when the list size
// is not known at plan time. Scales the single-value equality estimate. Matches
// SQLite's own tuning constant for "x IN (SELECT ...)" (see whereLoopAddBtree).
constexpr double kAssumedInListSize = 25;

// Rows surviving a scalar equality filter on a HasDuplicates column with
// `estimated_distinct` distinct values (0 = unknown). With a known count, a
// uniform column keeps ~1/estimated_distinct of the rows; otherwise fall back
// to the data-blind heuristic.
double EqualityFilterRows(uint32_t row_count, uint32_t estimated_distinct) {
  if (estimated_distinct > 0) {
    return static_cast<double>(row_count) / estimated_distinct;
  }
  return row_count / (2 * log2(row_count));
}

// Tracks how many rows are left as operations are applied.
class RowModel {
 public:
  explicit RowModel(uint32_t row_count) : rows_{row_count, row_count} {}

  logical::RowEstimate rows() const { return rows_; }

  // Filters whose selectivity we cannot reason about keep half the rows.
  void ApplyNonEqualityFilter() {
    if (rows_.estimated > 1) {
      rows_.estimated = rows_.estimated / 2;
    }
  }

  void ApplyEqualityFilter(DuplicateState duplicate_state,
                           uint32_t estimated_distinct) {
    if (duplicate_state.Is<HasDuplicates>()) {
      if (rows_.estimated > 1) {
        // Estimate against the pre-selective-filter row count and keep the
        // most selective result: correlated filters on one scan shouldn't
        // compound and collapse the estimate toward 1.
        SetSelectiveBase();
        rows_.estimated =
            std::min(rows_.estimated,
                     std::max(1u, static_cast<uint32_t>(EqualityFilterRows(
                                      *selective_base_, estimated_distinct))));
      }
      return;
    }
    PERFETTO_CHECK(duplicate_state.Is<NoDuplicates>());
    rows_.estimated = std::min(1u, rows_.estimated);
    rows_.max = std::min(1u, rows_.max);
  }

  void ApplyInFilter(DuplicateState duplicate_state,
                     uint32_t estimated_distinct) {
    if (!duplicate_state.Is<HasDuplicates>() || rows_.estimated <= 1) {
      return;
    }
    // An IN is a union of equalities over the list values. The list size is
    // unknown at plan time, so scale the single-value estimate by an assumed
    // distinct-value count. As with equality, estimate against the
    // pre-selective-filter row count and keep the most selective result.
    SetSelectiveBase();
    double per_value = EqualityFilterRows(*selective_base_, estimated_distinct);
    double new_count = std::min(static_cast<double>(*selective_base_),
                                per_value * kAssumedInListSize);
    rows_.estimated = std::min(rows_.estimated,
                               std::max(1u, static_cast<uint32_t>(new_count)));
  }

  void ApplyOneRow() {
    rows_.estimated = std::min(1u, rows_.estimated);
    rows_.max = std::min(1u, rows_.max);
  }

  void ApplyZeroRows() {
    rows_.estimated = 0;
    rows_.max = 0;
  }

  void ApplyLimitOffset(uint32_t limit, uint32_t offset) {
    // Offset will cut out `offset` rows from the start of indices.
    rows_.max -= std::min(rows_.max, offset);

    // Limit will only preserve at most `limit` rows.
    rows_.max = std::min(limit, rows_.max);

    // The max row count is also the best possible estimate we can make for
    // the row count.
    rows_.estimated = rows_.max;
  }

 private:
  void SetSelectiveBase() {
    if (!selective_base_) {
      selective_base_ = rows_.estimated;
    }
  }

  logical::RowEstimate rows_;

  // Row count before the first selective (equality/IN) filter was applied.
  // Used to avoid compounding the selectivity of multiple such filters: only
  // the most selective one determines the estimate.
  std::optional<uint32_t> selective_base_;
};

// Calculates filter preference score for ordering filters.
// Lower scores are applied first for better efficiency.
uint32_t FilterPreference(const FilterSpec& fs, const Column& col) {
  enum AbsolutePreference : uint8_t {
    kIdEq,                     // Most efficient: id equality check
    kSetIdSortedEq,            // Set id sorted equality check
    kIdInequality,             // Id inequality check
    kNumericSortedEq,          // Numeric sorted equality check
    kNumericSortedInequality,  // Numeric inequality check
    kStringSortedEq,           // String sorted equality check
    kStringSortedInequality,   // String inequality check
    kLeastPreferred,           // Least preferred
  };
  const auto& op = fs.op;
  const auto& ct = col.storage.type();
  const auto& n = col.null_storage.nullability();
  if (n.Is<NonNull>() && ct.Is<Id>() && op.Is<Eq>()) {
    return kIdEq;
  }
  if (n.Is<NonNull>() && ct.Is<Uint32>() && col.sort_state.Is<SetIdSorted>() &&
      op.Is<Eq>()) {
    return kSetIdSortedEq;
  }
  if (n.Is<NonNull>() && ct.Is<Id>() && op.IsAnyOf<InequalityOp>()) {
    return kIdInequality;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() &&
      ct.IsAnyOf<IntegerOrDoubleType>() && op.Is<Eq>()) {
    return kNumericSortedEq;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() &&
      ct.IsAnyOf<IntegerOrDoubleType>() && op.IsAnyOf<InequalityOp>()) {
    return kNumericSortedInequality;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() && ct.Is<String>() &&
      op.Is<Eq>()) {
    return kStringSortedEq;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() && ct.Is<String>() &&
      op.IsAnyOf<InequalityOp>()) {
    return kStringSortedInequality;
  }
  return kLeastPreferred;
}

struct BestIndex {
  uint32_t best_index_idx;
  std::vector<uint32_t> best_index_specs;
};
std::optional<BestIndex> GetBestIndexForFilterSpecs(
    uint32_t max_row_count,
    const std::vector<FilterSpec>& all_specs,
    const std::vector<uint8_t>& spec_already_handled,
    const std::vector<Index>& indexes) {
  // If we have very few rows, there's no point in using an index.
  if (max_row_count <= 1) {
    return std::nullopt;
  }
  uint32_t best_index_idx = std::numeric_limits<uint32_t>::max();
  std::vector<uint32_t> best_index_specs;
  for (uint32_t i = 0; i < indexes.size(); ++i) {
    const Index& index = indexes[i];
    std::vector<uint32_t> current_specs_for_this_index;
    for (uint32_t column : index.columns()) {
      bool found_spec_for_column = false;
      for (uint32_t spec_idx = 0; spec_idx < all_specs.size(); ++spec_idx) {
        if (spec_already_handled[spec_idx]) {
          continue;
        }
        const FilterSpec& current_spec = all_specs[spec_idx];
        if (current_spec.col == column &&
            (current_spec.op.Is<Eq>() || current_spec.op.Is<In>())) {
          current_specs_for_this_index.push_back(spec_idx);
          found_spec_for_column = true;
          break;
        }
      }
      if (!found_spec_for_column) {
        break;
      }
      // An In filter produces non-contiguous output, breaking the sort
      // invariant needed by subsequent columns' binary searches. So In
      // must be terminal: stop matching further index columns.
      if (all_specs[current_specs_for_this_index.back()].op.Is<In>()) {
        break;
      }
    }
    if (current_specs_for_this_index.size() > best_index_specs.size()) {
      best_index_idx = i;
      best_index_specs = std::move(current_specs_for_this_index);
    }
  }
  if (best_index_idx == std::numeric_limits<uint32_t>::max()) {
    return std::nullopt;
  }
  return BestIndex{best_index_idx, std::move(best_index_specs)};
}

// Builds the logical plan for one query.
class Planner {
 public:
  Planner(uint32_t row_count,
          const std::vector<std::shared_ptr<Column>>& columns,
          const std::vector<Index>& indexes)
      : columns_(columns), indexes_(indexes), rows_(row_count) {
    plan_.ops.emplace_back(logical::Scan{rows_.rows()});
  }

  base::Status Filter(std::vector<FilterSpec>& specs);
  void Distinct(const std::vector<DistinctSpec>& specs);
  void Sort(const std::vector<SortSpec>& specs);
  void MinMax(const SortSpec& spec);
  void Limit(const LimitSpec& limit);
  void Output(uint64_t cols_used);

  bool CanUseMinMaxOptimization(const std::vector<SortSpec>& sort_specs,
                                const LimitSpec& limit_spec) const {
    return sort_specs.size() == 1 &&
           GetColumn(sort_specs[0].col)
               .null_storage.nullability()
               .Is<NonNull>() &&
           limit_spec.limit == 1 && limit_spec.offset.value_or(0) == 0;
  }

  LogicalPlan Build() && {
    plan_.rows = rows_.rows();
    return std::move(plan_);
  }

 private:
  // Records `spec` as evaluated by `strategy`, applying that strategy's effect
  // on the row estimate.
  void AddFilter(FilterSpec& spec, logical::FilterStrategy strategy);

  bool TrySortedFilter(FilterSpec& spec);
  void AddIndexFilter(std::vector<FilterSpec>& specs,
                      uint32_t index_idx,
                      const std::vector<uint32_t>& spec_idxs);
  void AddNullFilter(const NullOp& op, FilterSpec& spec);
  void AddEmpty();

  uint32_t ReserveFilterValueSlot(FilterSpec& spec) {
    uint32_t slot = plan_.filter_value_count++;
    spec.value_index = slot;
    return slot;
  }

  const Column& GetColumn(uint32_t idx) const { return *columns_[idx]; }

  const std::vector<std::shared_ptr<Column>>& columns_;
  const std::vector<Index>& indexes_;

  LogicalPlan plan_;
  RowModel rows_;

  // Whether the rows are still a contiguous range rather than a materialized
  // list. Strategies which scan a range are only available while this holds.
  bool rows_are_range_ = true;
};

base::Status Planner::Filter(std::vector<FilterSpec>& specs) {
  // Sort filters by efficiency (most selective/cheapest first)
  std::stable_sort(specs.begin(), specs.end(),
                   [this](const FilterSpec& a, const FilterSpec& b) {
                     const auto& a_col = GetColumn(a.col);
                     const auto& b_col = GetColumn(b.col);
                     return FilterPreference(a, a_col) <
                            FilterPreference(b, b_col);
                   });

  std::vector<uint8_t> specs_handled(specs.size(), false);

  // Phase 1: Handle sorted constraints first
  for (uint32_t i = 0; i < specs.size(); ++i) {
    if (specs_handled[i]) {
      continue;
    }
    if (!TrySortedFilter(specs[i])) {
      continue;
    }
    specs_handled[i] = true;
  }

  // Phase 2: Handle constraints which can use an index.
  std::optional<BestIndex> best_index = GetBestIndexForFilterSpecs(
      rows_.rows().max, specs, specs_handled, indexes_);
  if (best_index) {
    AddIndexFilter(specs, best_index->best_index_idx,
                   best_index->best_index_specs);
    for (uint32_t spec_idx : best_index->best_index_specs) {
      specs_handled[spec_idx] = true;
    }
  }

  // Phase 3: Handle all remaining constraints.
  for (uint32_t i = 0; i < specs.size(); ++i) {
    if (specs_handled[i]) {
      continue;
    }
    FilterSpec& c = specs[i];
    const Column& col = GetColumn(c.col);
    StorageType ct = col.storage.type();

    if (c.op.Is<In>()) {
      AddFilter(c, logical::IndexListScan{});
      continue;
    }

    // Get the non-null operation (all our ops are non-null at this point)
    auto non_null_op = c.op.TryDowncast<NonNullOp>();
    if (!non_null_op) {
      AddNullFilter(*c.op.TryDowncast<NullOp>(), c);
      continue;
    }

    // Handle non-string data types
    if (const auto& n = ct.TryDowncast<NonStringType>(); n) {
      if (c.op.TryDowncast<NonStringOp>()) {
        // A non-null equality over a contiguous range can be answered by a
        // single scan which materializes the matches as it goes. An Id column
        // never reaches here: it is sorted, so phase 1 took it.
        bool range_scan = rows_are_range_ && non_null_op->Is<Eq>() &&
                          col.null_storage.nullability().Is<NonNull>();
        if (range_scan) {
          PERFETTO_CHECK(!n->Is<Id>());
        }
        AddFilter(c, range_scan
                         ? logical::FilterStrategy{logical::RangeScan{}}
                         : logical::FilterStrategy{logical::IndexListScan{}});
      } else {
        // No slot is claimed: the operation cannot apply to this type, so the
        // caller keeps responsibility for the constraint.
        AddEmpty();
      }
      continue;
    }

    PERFETTO_CHECK(ct.Is<String>());
    PERFETTO_CHECK(non_null_op->TryDowncast<StringOp>());
    bool range_scan = non_null_op->Is<Eq>() && rows_are_range_ &&
                      col.null_storage.nullability().Is<NonNull>();
    AddFilter(c, range_scan
                     ? logical::FilterStrategy{logical::RangeScan{}}
                     : logical::FilterStrategy{logical::IndexListScan{}});
  }
  return base::OkStatus();
}

bool Planner::TrySortedFilter(FilterSpec& spec) {
  auto non_null_op = spec.op.TryDowncast<NonNullOp>();
  if (!non_null_op) {
    return false;
  }
  const Column& col = GetColumn(spec.col);
  const auto& nullability = col.null_storage.nullability();
  if (!nullability.Is<NonNull>() || col.sort_state.Is<Unsorted>()) {
    return false;
  }
  if (!non_null_op->TryDowncast<RangeOp>()) {
    return false;
  }

  // We should have ordered the constraints such that we only reach this
  // point with range indices.
  PERFETTO_CHECK(rows_are_range_);

  StorageType ct = col.storage.type();
  if (ct.Is<Uint32>() && col.sort_state.Is<SetIdSorted>() &&
      non_null_op->Is<Eq>()) {
    AddFilter(spec, logical::SetIdSortedSearch{});
    return true;
  }
  if (col.specialized_storage.Is<SpecializedStorage::SmallValueEq>() &&
      non_null_op->Is<Eq>()) {
    AddFilter(spec, logical::SmallValueLookup{});
    return true;
  }
  AddFilter(spec, logical::BinarySearch{});
  return true;
}

void Planner::AddFilter(FilterSpec& spec, logical::FilterStrategy strategy) {
  const Column& col = GetColumn(spec.col);
  logical::Filter f;
  f.col = spec.col;
  f.op = spec.op;
  f.storage = col.storage.type();
  f.nullability = col.null_storage.nullability();
  f.strategy = strategy;
  f.rows_in = rows_.rows();
  f.value_index = ReserveFilterValueSlot(spec);

  // A filter reading a materialized list drops null rows before comparing
  // values; every other strategy is only ever chosen for non-null columns.
  bool prunes_nulls =
      std::holds_alternative<logical::IndexListScan>(strategy) &&
      !f.nullability.Is<NonNull>();
  if (prunes_nulls) {
    rows_.ApplyNonEqualityFilter();
  }
  f.estimated_rows_after_null_prune = rows_.rows().estimated;

  if (spec.op.Is<In>()) {
    rows_.ApplyInFilter(col.duplicate_state, col.estimated_distinct);
  } else if (spec.op.Is<Eq>()) {
    rows_.ApplyEqualityFilter(col.duplicate_state, col.estimated_distinct);
  } else {
    rows_.ApplyNonEqualityFilter();
  }
  f.rows_out = rows_.rows();

  if (std::holds_alternative<logical::IndexListScan>(strategy) ||
      std::holds_alternative<logical::RangeScan>(strategy)) {
    rows_are_range_ = false;
  }
  plan_.ops.emplace_back(std::move(f));
}

void Planner::AddIndexFilter(std::vector<FilterSpec>& specs,
                             uint32_t index_idx,
                             const std::vector<uint32_t>& spec_idxs) {
  PERFETTO_CHECK(rows_are_range_);
  logical::IndexFilter idx;
  idx.index = index_idx;
  idx.predicates.reserve(spec_idxs.size());
  for (uint32_t spec_idx : spec_idxs) {
    FilterSpec& fs = specs[spec_idx];
    const Column& col = GetColumn(fs.col);
    PERFETTO_CHECK(col.storage.type().TryDowncast<NonIdStorageType>());

    logical::IndexFilter::Predicate p;
    p.col = fs.col;
    p.op = fs.op;
    p.storage = col.storage.type();
    p.nullability = col.null_storage.nullability();
    p.rows_in = rows_.rows();
    p.value_index = ReserveFilterValueSlot(fs);
    if (fs.op.Is<In>()) {
      rows_.ApplyInFilter(col.duplicate_state, col.estimated_distinct);
    } else {
      rows_.ApplyEqualityFilter(col.duplicate_state, col.estimated_distinct);
    }
    p.rows_out = rows_.rows();
    idx.predicates.emplace_back(std::move(p));
  }
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(idx));
}

void Planner::AddNullFilter(const NullOp& op, FilterSpec& spec) {
  // Even if we don't need this to filter null/non-null, we record the slot so
  // that the caller (i.e. SQLite) knows that we are able to handle the
  // constraint.
  uint32_t slot = ReserveFilterValueSlot(spec);

  const Column& col = GetColumn(spec.col);
  if (col.null_storage.nullability().Is<NonNull>()) {
    if (op.Is<IsNull>()) {
      AddEmpty();
    }
    // Otherwise nothing to do: every row is non-null, so the predicate is a
    // tautology and needs no operation at all.
    return;
  }
  logical::Filter f;
  f.col = spec.col;
  f.op = spec.op;
  f.value_index = slot;
  f.storage = col.storage.type();
  f.nullability = col.null_storage.nullability();
  f.strategy = logical::IndexListScan{};
  f.rows_in = rows_.rows();
  f.estimated_rows_after_null_prune = rows_.rows().estimated;
  rows_.ApplyNonEqualityFilter();
  f.rows_out = rows_.rows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(f));
}

void Planner::AddEmpty() {
  rows_.ApplyZeroRows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(logical::Empty{});
}

void Planner::Distinct(const std::vector<DistinctSpec>& specs) {
  if (specs.empty()) {
    return;
  }
  logical::Distinct d;
  d.cols.reserve(specs.size());
  for (const auto& spec : specs) {
    d.cols.push_back(spec.col);
  }
  d.rows_in = rows_.rows();
  rows_.ApplyNonEqualityFilter();
  d.rows_out = rows_.rows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(d));
}

void Planner::Sort(const std::vector<SortSpec>& sort_specs) {
  if (sort_specs.empty()) {
    return;
  }

  // If there's a single sort constraint on a NonNull column that is already
  // sorted accordingly, the data is in order already.
  if (sort_specs.size() == 1) {
    const auto& single_spec = sort_specs[0];
    const Column& col = GetColumn(single_spec.col);
    if (col.null_storage.nullability().Is<NonNull>() &&
        (col.sort_state.Is<Sorted>() || col.sort_state.Is<IdSorted>() ||
         col.sort_state.Is<SetIdSorted>())) {
      switch (single_spec.direction) {
        case SortDirection::kAscending:
          return;
        case SortDirection::kDescending:
          // Sorted the opposite way round, so reversing suffices.
          rows_are_range_ = false;
          plan_.ops.emplace_back(logical::Reverse{});
          return;
      }
    }
  }
  logical::Sort s;
  s.keys = sort_specs;
  s.rows = rows_.rows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(s));
}

void Planner::MinMax(const SortSpec& spec) {
  logical::MinMax m;
  m.col = spec.col;
  m.direction = spec.direction;
  m.rows_in = rows_.rows();
  rows_.ApplyOneRow();
  m.rows_out = rows_.rows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(m));
}

void Planner::Limit(const LimitSpec& limit) {
  if (!limit.limit && !limit.offset) {
    return;
  }
  logical::Limit l;
  l.limit = limit.limit;
  l.offset = limit.offset;
  l.rows_in = rows_.rows();
  rows_.ApplyLimitOffset(
      limit.limit.value_or(std::numeric_limits<uint32_t>::max()),
      limit.offset.value_or(0));
  l.rows_out = rows_.rows();
  rows_are_range_ = false;
  plan_.ops.emplace_back(std::move(l));
}

void Planner::Output(uint64_t cols_used) {
  rows_are_range_ = false;
  plan_.ops.emplace_back(logical::Output{cols_used});
}

// === Printing ===

const char* ToString(const Op& op) {
  switch (op.index()) {
    case Op::GetTypeIndex<Eq>():
      return "Eq";
    case Op::GetTypeIndex<Ne>():
      return "Ne";
    case Op::GetTypeIndex<Lt>():
      return "Lt";
    case Op::GetTypeIndex<Le>():
      return "Le";
    case Op::GetTypeIndex<Gt>():
      return "Gt";
    case Op::GetTypeIndex<Ge>():
      return "Ge";
    case Op::GetTypeIndex<Glob>():
      return "Glob";
    case Op::GetTypeIndex<Regex>():
      return "Regex";
    case Op::GetTypeIndex<IsNotNull>():
      return "IsNotNull";
    case Op::GetTypeIndex<IsNull>():
      return "IsNull";
    case Op::GetTypeIndex<In>():
      return "In";
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

const char* ToString(const StorageType& type) {
  switch (type.index()) {
    case StorageType::GetTypeIndex<Id>():
      return "Id";
    case StorageType::GetTypeIndex<Uint32>():
      return "Uint32";
    case StorageType::GetTypeIndex<Int32>():
      return "Int32";
    case StorageType::GetTypeIndex<Int64>():
      return "Int64";
    case StorageType::GetTypeIndex<Double>():
      return "Double";
    case StorageType::GetTypeIndex<String>():
      return "String";
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

const char* ToString(const Nullability& n) {
  switch (n.index()) {
    case Nullability::GetTypeIndex<NonNull>():
      return "NonNull";
    case Nullability::GetTypeIndex<DenseNull>():
      return "DenseNull";
    case Nullability::GetTypeIndex<SparseNull>():
      return "SparseNull";
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
      return "SparseNullWithPopcountAlways";
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
      return "SparseNullWithPopcountUntilFinalization";
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

const char* ToString(const logical::FilterStrategy& s) {
  switch (s.index()) {
    case base::variant_index<logical::FilterStrategy, logical::BinarySearch>():
      return "BinarySearch";
    case base::variant_index<logical::FilterStrategy,
                             logical::SetIdSortedSearch>():
      return "SetIdSortedSearch";
    case base::variant_index<logical::FilterStrategy,
                             logical::SmallValueLookup>():
      return "SmallValueLookup";
    case base::variant_index<logical::FilterStrategy, logical::RangeScan>():
      return "RangeScan";
    case base::variant_index<logical::FilterStrategy, logical::IndexListScan>():
      return "IndexListScan";
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

std::string ValueSlotToString(const std::optional<uint32_t>& slot) {
  return slot ? std::to_string(*slot) : "none";
}

std::string RowsToString(const logical::RowEstimate& in,
                         const logical::RowEstimate& out) {
  return "rows=" + std::to_string(in.estimated) + "/" + std::to_string(in.max) +
         "->" + std::to_string(out.estimated) + "/" + std::to_string(out.max);
}

}  // namespace

std::string LogicalPlan::ToString() const {
  std::string res;
  for (const auto& op : ops) {
    switch (op.index()) {
      case base::variant_index<logical::Operation, logical::Scan>(): {
        const auto& sc = base::unchecked_get<logical::Scan>(op);
        res += "Scan[rows=" + std::to_string(sc.rows.estimated) + "/" +
               std::to_string(sc.rows.max) + "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::Filter>(): {
        const auto& f = base::unchecked_get<logical::Filter>(op);
        res += "Filter[col=" + std::to_string(f.col) +
               " op=" + dataframe::ToString(f.op) +
               " value=" + ValueSlotToString(f.value_index) +
               " storage=" + dataframe::ToString(f.storage) +
               " null=" + dataframe::ToString(f.nullability) +
               " strategy=" + dataframe::ToString(f.strategy) + " " +
               RowsToString(f.rows_in, f.rows_out);
        if (f.estimated_rows_after_null_prune != f.rows_in.estimated) {
          res += " after_null_prune=" +
                 std::to_string(f.estimated_rows_after_null_prune);
        }
        res += "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::IndexFilter>(): {
        const auto& i = base::unchecked_get<logical::IndexFilter>(op);
        res += "IndexFilter[index=" + std::to_string(i.index);
        for (const auto& p : i.predicates) {
          res += " (col=" + std::to_string(p.col) +
                 " op=" + dataframe::ToString(p.op) +
                 " value=" + ValueSlotToString(p.value_index) +
                 " storage=" + dataframe::ToString(p.storage) +
                 " null=" + dataframe::ToString(p.nullability) + " " +
                 RowsToString(p.rows_in, p.rows_out) + ")";
        }
        res += "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::Empty>():
        res += "Empty[]\n";
        break;
      case base::variant_index<logical::Operation, logical::Distinct>(): {
        const auto& d = base::unchecked_get<logical::Distinct>(op);
        res += "Distinct[cols=";
        for (size_t i = 0; i < d.cols.size(); ++i) {
          res += (i == 0 ? "" : ",") + std::to_string(d.cols[i]);
        }
        res += " " + RowsToString(d.rows_in, d.rows_out) + "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::Reverse>():
        res += "Reverse[]\n";
        break;
      case base::variant_index<logical::Operation, logical::Sort>(): {
        const auto& s = base::unchecked_get<logical::Sort>(op);
        res += "Sort[keys=";
        for (size_t i = 0; i < s.keys.size(); ++i) {
          res += (i == 0 ? "" : ",") + std::to_string(s.keys[i].col) +
                 (s.keys[i].direction == SortDirection::kAscending ? ":asc"
                                                                   : ":desc");
        }
        res += " " + RowsToString(s.rows, s.rows) + "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::MinMax>(): {
        const auto& m = base::unchecked_get<logical::MinMax>(op);
        res += "MinMax[col=" + std::to_string(m.col) + " dir=" +
               (m.direction == SortDirection::kAscending ? "asc" : "desc") +
               " " + RowsToString(m.rows_in, m.rows_out) + "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::Limit>(): {
        const auto& l = base::unchecked_get<logical::Limit>(op);
        res += "Limit[limit=" + ValueSlotToString(l.limit) +
               " offset=" + ValueSlotToString(l.offset) + " " +
               RowsToString(l.rows_in, l.rows_out) + "]\n";
        break;
      }
      case base::variant_index<logical::Operation, logical::Output>(): {
        const auto& o = base::unchecked_get<logical::Output>(op);
        res += "Output[cols_used=0x" +
               base::Uint64ToHexStringNoPrefix(o.cols_used) + "]\n";
        break;
      }
      default:
        PERFETTO_FATAL("Unreachable");
    }
  }
  return res;
}

base::StatusOr<LogicalPlan> LogicalPlanner::Plan(
    uint32_t row_count,
    const std::vector<std::shared_ptr<Column>>& columns,
    const std::vector<Index>& indexes,
    std::vector<FilterSpec>& specs,
    const std::vector<DistinctSpec>& distinct,
    const std::vector<SortSpec>& sort_specs,
    const LimitSpec& limit_spec,
    uint64_t cols_used) {
  Planner planner(row_count, columns, indexes);
  RETURN_IF_ERROR(planner.Filter(specs));
  planner.Distinct(distinct);
  if (planner.CanUseMinMaxOptimization(sort_specs, limit_spec)) {
    planner.MinMax(sort_specs[0]);
  } else {
    planner.Sort(sort_specs);
    planner.Limit(limit_spec);
  }
  planner.Output(cols_used);
  return std::move(planner).Build();
}

}  // namespace perfetto::trace_processor::core::dataframe
