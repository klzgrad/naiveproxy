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
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/typed_cursor.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_to_string.h"
#include "src/trace_processor/core/util/bit_vector.h"

namespace perfetto::trace_processor::core::dataframe {
namespace {

template <typename T>
void GatherInPlace(T& storage, const uint32_t* indices, uint32_t count) {
  // Since indices are sorted, indices[i] >= i, so we can gather in-place
  // without overwriting unread data.
  for (uint32_t i = 0; i < count; ++i) {
    storage[i] = storage[indices[i]];
  }
  storage.resize(count);
}

void GatherBitsInPlace(core::BitVector& bv,
                       const uint32_t* indices,
                       uint32_t count) {
  // Since indices are sorted, indices[i] >= i, so we can gather in-place.
  for (uint32_t i = 0; i < count; ++i) {
    bv.change(i, bv.is_set(indices[i]));
  }
  bv.resize(count);
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
  ASSIGN_OR_RETURN(auto plan,
                   QueryPlanBuilder::Build(row_count_, columns_, indexes_,
                                           filter_specs, distinct_specs,
                                           sort_specs, limit_spec, cols_used));
  return QueryPlan(std::move(plan));
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

void Dataframe::AddIndex(Index index) {
  PERFETTO_CHECK(finalized_);
  indexes_.emplace_back(std::move(index));
  ++non_column_mutations_;
}

void Dataframe::RemoveIndexAt(uint32_t index) {
  PERFETTO_CHECK(finalized_);
  indexes_.erase(indexes_.begin() + static_cast<std::ptrdiff_t>(index));
  ++non_column_mutations_;
}

void Dataframe::Finalize() {
  if (finalized_) {
    return;
  }
  finalized_ = true;
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

// static
base::StatusOr<Dataframe> Dataframe::HorizontalConcat(Dataframe&& left,
                                                      Dataframe&& right) {
  PERFETTO_CHECK(left.finalized_);
  PERFETTO_CHECK(right.finalized_);
  if (left.row_count_ != right.row_count_) {
    return base::ErrStatus(
        "HorizontalConcat: row count mismatch. Left has %u rows, right has %u "
        "rows.",
        left.row_count_, right.row_count_);
  }

  std::vector<std::string> column_names;
  std::vector<std::shared_ptr<Column>> columns;
  bool had_auto_id = false;

  // Add columns from left, excluding _auto_id.
  for (uint32_t i = 0; i < left.column_names_.size(); ++i) {
    if (left.column_names_[i] == "_auto_id") {
      had_auto_id = true;
    } else {
      column_names.emplace_back(std::move(left.column_names_[i]));
      columns.emplace_back(std::move(left.columns_[i]));
    }
  }

  // Add columns from right, excluding _auto_id.
  for (uint32_t i = 0; i < right.column_names_.size(); ++i) {
    if (right.column_names_[i] == "_auto_id") {
      had_auto_id = true;
    } else {
      column_names.emplace_back(std::move(right.column_names_[i]));
      columns.emplace_back(std::move(right.columns_[i]));
    }
  }

  // Check for duplicate column names.
  {
    std::unordered_set<std::string> seen;
    for (const auto& name : column_names) {
      if (!seen.insert(name).second) {
        return base::ErrStatus("HorizontalConcat: duplicate column name '%s'.",
                               name.c_str());
      }
    }
  }

  // Add a new _auto_id column only if either input had one.
  if (had_auto_id) {
    column_names.emplace_back("_auto_id");
    columns.emplace_back(std::make_shared<Column>(
        Column{Storage{Storage::Id{left.row_count_}}, NullStorage::NonNull{},
               IdSorted{}, NoDuplicates{}}));
  }

  return Dataframe(true, std::move(column_names), std::move(columns),
                   left.row_count_, left.string_pool_);
}

Dataframe Dataframe::SelectRows(const uint32_t* indices, uint32_t count) && {
  PERFETTO_CHECK(finalized_);
  // Check that the indices must be sorted and duplicate-free.
  for (uint32_t i = 1; i < count; ++i) {
    PERFETTO_DCHECK(indices[i - 1] < indices[i]);
  }
  for (auto& col : columns_) {
    auto type = col->storage.type();
    if (type.Is<Id>()) {
      col->storage.unchecked_get<Id>().size = count;
    } else if (type.Is<Uint32>()) {
      GatherInPlace(col->storage.unchecked_get<Uint32>(), indices, count);
    } else if (type.Is<Int32>()) {
      GatherInPlace(col->storage.unchecked_get<Int32>(), indices, count);
    } else if (type.Is<Int64>()) {
      GatherInPlace(col->storage.unchecked_get<Int64>(), indices, count);
    } else if (type.Is<Double>()) {
      GatherInPlace(col->storage.unchecked_get<Double>(), indices, count);
    } else if (type.Is<String>()) {
      GatherInPlace(col->storage.unchecked_get<String>(), indices, count);
    } else {
      PERFETTO_FATAL("Invalid storage type");
    }
    auto nullability = col->null_storage.nullability();
    if (nullability.Is<NonNull>()) {
      // Nothing to do.
    } else if (nullability.Is<DenseNull>()) {
      GatherBitsInPlace(col->null_storage.unchecked_get<DenseNull>().bit_vector,
                        indices, count);
    } else {
      auto& sparse = col->null_storage.unchecked_get<SparseNull>();
      GatherBitsInPlace(sparse.bit_vector, indices, count);
      if (nullability.Is<SparseNullWithPopcountAlways>()) {
        sparse.prefix_popcount_for_cell_get =
            sparse.bit_vector.PrefixPopcountFlexVector();
      } else {
        PERFETTO_CHECK(sparse.prefix_popcount_for_cell_get.empty());
      }
    }
  }
  row_count_ = count;
  return std::move(*this);
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
