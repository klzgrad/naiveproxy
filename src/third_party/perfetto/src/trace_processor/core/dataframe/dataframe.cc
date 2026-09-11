/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/core/dataframe/dataframe.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/bytecode_lowering.h"
#include "src/trace_processor/core/dataframe/logical_plan.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/typed_cursor.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_to_string.h"
#include "src/trace_processor/core/util/ops.h"

namespace perfetto::trace_processor::core::dataframe {
namespace {

// Estimates the distinct-value count of a finalized column, or 0 if unknown.
// Only computed for HasDuplicates columns: unique columns select at most one
// row regardless, and the planner already handles them exactly.
uint32_t EstimateDistinct(base::FlatHashMap<int64_t, uint32_t>& counts,
                          const Column& c) {
  if (!c.duplicate_state.Is<HasDuplicates>()) {
    return 0;
  }
  switch (c.storage.type().index()) {
    case StorageType::GetTypeIndex<Uint32>(): {
      const auto& values = c.storage.unchecked_get<Uint32>();
      return core::ops::EstimateDistinctCount(&counts, values.span());
    }
    case StorageType::GetTypeIndex<Int32>(): {
      const auto& values = c.storage.unchecked_get<Int32>();
      return core::ops::EstimateDistinctCount(&counts, values.span());
    }
    case StorageType::GetTypeIndex<Int64>(): {
      const auto& values = c.storage.unchecked_get<Int64>();
      return core::ops::EstimateDistinctCount(&counts, values.span());
    }
    case StorageType::GetTypeIndex<String>(): {
      const auto& values = c.storage.unchecked_get<String>();
      return core::ops::EstimateDistinctCount(&counts, values.span());
    }
    case StorageType::GetTypeIndex<Double>():
    case StorageType::GetTypeIndex<Id>():
      return 0;
    default:
      PERFETTO_FATAL("Invalid storage type");
  }
}

}  // namespace

Dataframe::Dataframe(StringPool* string_pool,
                     uint32_t column_count,
                     const char* const* column_names,
                     const ColumnSpec* column_specs)
    : Dataframe(
          false,
          std::vector<std::string>(column_names, column_names + column_count),
          CreateColumnVector(column_specs, column_count),
          0,
          string_pool) {}

Dataframe::Dataframe(bool finalized,
                     std::vector<std::string> column_names,
                     std::vector<std::shared_ptr<Column>> columns,
                     uint32_t row_count,
                     StringPool* string_pool)
    : column_names_(std::move(column_names)),
      columns_(std::move(columns)),
      row_count_(row_count),
      string_pool_(string_pool) {
  column_ptrs_.reserve(columns_.size());
  for (const auto& col : columns_) {
    column_ptrs_.emplace_back(col.get());
  }
  if (finalized) {
    Finalize();
  }
}

base::StatusOr<Dataframe::QueryPlan> Dataframe::PlanQuery(
    std::vector<FilterSpec>& filter_specs,
    const std::vector<DistinctSpec>& distinct_specs,
    const std::vector<SortSpec>& sort_specs,
    const LimitSpec& limit_spec,
    uint64_t cols_used) const {
  ASSIGN_OR_RETURN(
      LogicalPlan logical,
      LogicalPlanner::Plan(row_count_, columns_, indexes_, filter_specs,
                           distinct_specs, sort_specs, limit_spec, cols_used));
  return QueryPlan(BytecodeLowering::Lower(logical, columns_, indexes_));
}

base::StatusOr<LogicalPlan> Dataframe::PlanQueryLogicalForTesting(
    std::vector<FilterSpec>& filter_specs,
    const std::vector<DistinctSpec>& distinct_specs,
    const std::vector<SortSpec>& sort_specs,
    const LimitSpec& limit_spec,
    uint64_t cols_used) const {
  return LogicalPlanner::Plan(row_count_, columns_, indexes_, filter_specs,
                              distinct_specs, sort_specs, limit_spec,
                              cols_used);
}

void Dataframe::Clear() {
  PERFETTO_DCHECK(!finalized_);
  for (const auto& c : columns_) {
    switch (c->storage.type().index()) {
      case StorageType::GetTypeIndex<Uint32>():
        c->storage.unchecked_get<Uint32>().clear();
        break;
      case StorageType::GetTypeIndex<Int32>():
        c->storage.unchecked_get<Int32>().clear();
        break;
      case StorageType::GetTypeIndex<Int64>():
        c->storage.unchecked_get<Int64>().clear();
        break;
      case StorageType::GetTypeIndex<Double>():
        c->storage.unchecked_get<Double>().clear();
        break;
      case StorageType::GetTypeIndex<String>():
        c->storage.unchecked_get<String>().clear();
        break;
      case StorageType::GetTypeIndex<Id>():
        c->storage.unchecked_get<Id>().size = 0;
        break;
      default:
        PERFETTO_FATAL("Invalid storage type");
    }
    switch (c->null_storage.nullability().index()) {
      case Nullability::GetTypeIndex<NonNull>():
        break;
      case Nullability::GetTypeIndex<SparseNull>():
      case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
      case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>(): {
        auto& null = c->null_storage.unchecked_get<SparseNull>();
        null.bit_vector.clear();
        null.prefix_popcount_for_cell_get.clear();
        break;
      }
      case Nullability::GetTypeIndex<DenseNull>():
        c->null_storage.unchecked_get<DenseNull>().bit_vector.clear();
        break;
      default:
        PERFETTO_FATAL("Invalid nullability type");
    }
  }
  row_count_ = 0;
  ++non_column_mutations_;
}

base::StatusOr<Index> Dataframe::BuildIndex(const uint32_t* columns_start,
                                            const uint32_t* columns_end) const {
  std::vector<uint32_t> cols(columns_start, columns_end);
  std::vector<SortSpec> sorts;
  sorts.reserve(cols.size());
  for (const auto& col : cols) {
    sorts.push_back(SortSpec{col, SortDirection::kAscending});
  }

  // Heap allocate to avoid potential stack overflows due to large cursor
  // object.
  auto c = std::make_unique<TypedCursor>(this, std::vector<FilterSpec>(),
                                         std::move(sorts));
  c->ExecuteUnchecked();

  std::vector<uint32_t> permutation;
  permutation.reserve(row_count_);
  for (; !c->Eof(); c->Next()) {
    permutation.push_back(c->RowIndex());
  }
  return Index(std::move(cols),
               std::make_shared<std::vector<uint32_t>>(std::move(permutation)));
}

Dataframe Dataframe::AddIndex(Index index) const {
  PERFETTO_CHECK(finalized_);
  Dataframe result(*this);
  result.indexes_.emplace_back(std::move(index));
  ++result.non_column_mutations_;
  return result;
}

Dataframe Dataframe::RemoveIndexAt(uint32_t pos) const {
  PERFETTO_CHECK(finalized_);
  Dataframe result(*this);
  result.indexes_.erase(result.indexes_.begin() +
                        static_cast<std::ptrdiff_t>(pos));
  ++result.non_column_mutations_;
  return result;
}

void Dataframe::Finalize() {
  if (finalized_) {
    return;
  }
  finalized_ = true;
  // Reused across columns; Clear() keeps its capacity so it allocates at most
  // once for the whole dataframe.
  base::FlatHashMap<int64_t, uint32_t> distinct_counts;
  for (const auto& c : columns_) {
    switch (c->storage.type().index()) {
      case StorageType::GetTypeIndex<Uint32>():
        c->storage.unchecked_get<Uint32>().shrink_to_fit();
        break;
      case StorageType::GetTypeIndex<Int32>():
        c->storage.unchecked_get<Int32>().shrink_to_fit();
        break;
      case StorageType::GetTypeIndex<Int64>():
        c->storage.unchecked_get<Int64>().shrink_to_fit();
        break;
      case StorageType::GetTypeIndex<Double>():
        c->storage.unchecked_get<Double>().shrink_to_fit();
        break;
      case StorageType::GetTypeIndex<String>():
        c->storage.unchecked_get<String>().shrink_to_fit();
        break;
      case StorageType::GetTypeIndex<Id>():
        break;
      default:
        PERFETTO_FATAL("Invalid storage type");
    }
    switch (c->null_storage.nullability().index()) {
      case Nullability::GetTypeIndex<NonNull>():
        break;
      case Nullability::GetTypeIndex<SparseNull>():
        c->null_storage.unchecked_get<SparseNull>().bit_vector.shrink_to_fit();
        break;
      case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>(): {
        auto& null = c->null_storage.unchecked_get<SparseNull>();
        null.bit_vector.shrink_to_fit();
        null.prefix_popcount_for_cell_get.shrink_to_fit();
        break;
      }
      case Nullability::GetTypeIndex<
          SparseNullWithPopcountUntilFinalization>(): {
        auto& null = c->null_storage.unchecked_get<SparseNull>();
        null.bit_vector.shrink_to_fit();
        null.prefix_popcount_for_cell_get.clear();
        null.prefix_popcount_for_cell_get.shrink_to_fit();
        break;
      }
      case Nullability::GetTypeIndex<DenseNull>():
        c->null_storage.unchecked_get<DenseNull>().bit_vector.shrink_to_fit();
        break;
      default:
        PERFETTO_FATAL("Invalid nullability type");
    }
    c->estimated_distinct = EstimateDistinct(distinct_counts, *c);
  }
  // Bump the mutation counter so that any cursors with cached pointers
  // know to refresh them: shrink_to_fit() may have reallocated the internal
  // storage.
  ++non_column_mutations_;
}

dataframe::Dataframe Dataframe::CopyFinalized() const {
  PERFETTO_CHECK(finalized_);
  return *this;
}

DataframeSpec Dataframe::CreateSpec() const {
  DataframeSpec spec{column_names_, {}};
  spec.column_specs.reserve(columns_.size());
  for (const auto& c : columns_) {
    spec.column_specs.push_back({c->storage.type(),
                                 c->null_storage.nullability(), c->sort_state,
                                 c->duplicate_state});
  }
  return spec;
}

std::vector<std::shared_ptr<Column>> Dataframe::CreateColumnVector(
    const ColumnSpec* column_specs,
    uint32_t column_count) {
  auto make_storage = [](const ColumnSpec& spec) {
    switch (spec.type.index()) {
      case StorageType::GetTypeIndex<Id>():
        return Storage(Storage::Id{});
      case StorageType::GetTypeIndex<Uint32>():
        return Storage(Storage::Uint32{});
      case StorageType::GetTypeIndex<Int32>():
        return Storage(Storage::Int32{});
      case StorageType::GetTypeIndex<Int64>():
        return Storage(Storage::Int64{});
      case StorageType::GetTypeIndex<Double>():
        return Storage(Storage::Double{});
      case StorageType::GetTypeIndex<String>():
        return Storage(Storage::String{});
      default:
        PERFETTO_FATAL("Invalid storage type");
    }
  };
  auto make_null_storage = [](const ColumnSpec& spec) {
    switch (spec.nullability.index()) {
      case Nullability::GetTypeIndex<NonNull>():
        return NullStorage(NullStorage::NonNull{});
      case Nullability::GetTypeIndex<SparseNull>():
        return NullStorage(NullStorage::SparseNull{}, SparseNull{});
      case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
        return NullStorage(NullStorage::SparseNull{},
                           SparseNullWithPopcountAlways{});
      case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
        return NullStorage(NullStorage::SparseNull{},
                           SparseNullWithPopcountUntilFinalization{});
      case Nullability::GetTypeIndex<DenseNull>():
        return NullStorage(NullStorage::DenseNull{});
      default:
        PERFETTO_FATAL("Invalid nullability type");
    }
  };
  std::vector<std::shared_ptr<Column>> columns;
  columns.reserve(column_count);
  for (uint32_t i = 0; i < column_count; ++i) {
    columns.emplace_back(std::make_shared<Column>(Column{
        make_storage(column_specs[i]),
        make_null_storage(column_specs[i]),
        column_specs[i].sort_state,
        column_specs[i].duplicate_state,
    }));
  }
  return columns;
}

std::vector<std::string> Dataframe::QueryPlan::BytecodeToString() const {
  std::vector<std::string> result;
  for (const auto& instr : plan_.bytecode) {
    result.push_back(interpreter::ToString(instr));
  }
  return result;
}

std::string Dataframe::QueryPlan::Serialize() const {
  return plan_.Serialize();
}

Dataframe::QueryPlan Dataframe::QueryPlan::Deserialize(
    std::string_view serialized) {
  return QueryPlan(QueryPlanImpl::Deserialize(serialized));
}

const QueryPlanImpl& Dataframe::QueryPlan::GetImplForTesting() const {
  return plan_;
}

uint32_t Dataframe::QueryPlan::max_row_count() const {
  return plan_.params.max_row_count;
}

uint32_t Dataframe::QueryPlan::estimated_row_count() const {
  return plan_.params.estimated_row_count;
}

double Dataframe::QueryPlan::estimated_cost() const {
  return plan_.params.estimated_cost;
}

}  // namespace perfetto::trace_processor::core::dataframe
