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

#include "src/trace_processor/core/dataframe/query_plan.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/dataframe_register_cache.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"
#include "src/trace_processor/core/util/type_set.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto::trace_processor::core::dataframe {

namespace {

namespace i = interpreter;

// Register type identifiers for cache key encoding.
// Used with DataframeRegisterCache::GetOrAllocate(reg_type, ptr)
// to cache column/index-specific registers.
enum RegType : uint32_t {
  kStorageReg = 0,
  kNullBvReg = 1,
  kPrefixPopcountReg = 2,
  kSmallValueEqBvReg = 3,
  kSmallValueEqPopcountReg = 4,
  kIndexReg = 5,
  kRegTypeCount = 6,
};

// TypeSet of all possible sparse nullability states.
using SparseNullTypes = TypeSet<SparseNull,
                                SparseNullWithPopcountAlways,
                                SparseNullWithPopcountUntilFinalization>;

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
  if (n.Is<NonNull>() && ct.Is<Id>() && op.IsAnyOf<i::InequalityOp>()) {
    return kIdInequality;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() &&
      ct.IsAnyOf<i::IntegerOrDoubleType>() && op.Is<Eq>()) {
    return kNumericSortedEq;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() &&
      ct.IsAnyOf<i::IntegerOrDoubleType>() && op.IsAnyOf<i::InequalityOp>()) {
    return kNumericSortedInequality;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() && ct.Is<String>() &&
      op.Is<Eq>()) {
    return kStringSortedEq;
  }
  if (n.Is<NonNull>() && col.sort_state.Is<Sorted>() && ct.Is<String>() &&
      op.IsAnyOf<i::InequalityOp>()) {
    return kStringSortedInequality;
  }
  return kLeastPreferred;
}

// Gets the appropriate bound modifier and range operation type
// for a given range operation.
std::pair<i::BoundModifier, i::EqualRangeLowerBoundUpperBound>
GetSortedFilterArgs(const i::RangeOp& op) {
  switch (op.index()) {
    case i::RangeOp::GetTypeIndex<Eq>():
      return std::make_pair(i::BothBounds{}, i::EqualRange{});
    case i::RangeOp::GetTypeIndex<Lt>():
      return std::make_pair(i::EndBound{}, i::LowerBound{});
    case i::RangeOp::GetTypeIndex<Le>():
      return std::make_pair(i::EndBound{}, i::UpperBound{});
    case i::RangeOp::GetTypeIndex<Gt>():
      return std::make_pair(i::BeginBound{}, i::UpperBound{});
    case i::RangeOp::GetTypeIndex<Ge>():
      return std::make_pair(i::BeginBound{}, i::LowerBound{});
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

// Helper to get byte size of storage types for layout calculation.
// Returns 0 for Id type as it's handled specially.
uint8_t GetDataSize(StorageType type) {
  switch (type.index()) {
    case StorageType::GetTypeIndex<Id>():
    case StorageType::GetTypeIndex<Uint32>():
    case StorageType::GetTypeIndex<Int32>():
    case StorageType::GetTypeIndex<String>():
      return sizeof(uint32_t);
    case StorageType::GetTypeIndex<Int64>():
      return sizeof(int64_t);
    case StorageType::GetTypeIndex<Double>():
      return sizeof(double);
    default:
      PERFETTO_FATAL("Invalid storage type");
  }
}

i::SparseNullCollapsedNullability NullabilityToSparseNullCollapsedNullability(
    Nullability nullability) {
  switch (nullability.index()) {
    case Nullability::GetTypeIndex<NonNull>():
      return NonNull{};
    case Nullability::GetTypeIndex<DenseNull>():
      return DenseNull{};
    case Nullability::GetTypeIndex<SparseNull>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
      return SparseNull{};
    default:
      PERFETTO_FATAL("Invalid nullability type");
  }
}

struct BestIndex {
  uint32_t best_index_idx;
  std::vector<uint32_t> best_index_specs;
};
std::optional<BestIndex> GetBestIndexForFilterSpecs(
    const QueryPlanImpl::ExecutionParams& params,
    const std::vector<FilterSpec>& all_specs,
    const std::vector<uint8_t>& spec_already_handled,
    const std::vector<Index>& indexes) {
  // If we have very few rows, there's no point in using an index.
  if (params.max_row_count <= 1) {
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
        if (current_spec.col == column && current_spec.op.Is<Eq>()) {
          current_specs_for_this_index.push_back(spec_idx);
          found_spec_for_column = true;
          break;
        }
      }
      if (!found_spec_for_column) {
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

}  // namespace

QueryPlanBuilder::QueryPlanBuilder(
    i::BytecodeBuilder& builder,
    DataframeRegisterCache& cache,
    IndicesReg indices,
    uint32_t row_count,
    const std::vector<std::shared_ptr<Column>>& columns,
    const std::vector<Index>& indexes)
    : columns_(columns),
      indexes_(indexes),
      indices_reg_(indices),
      builder_(builder),
      cache_(cache) {
  // Setup the maximum and estimated row counts.
  plan_.params.max_row_count = row_count;
  plan_.params.estimated_row_count = row_count;
}

base::StatusOr<QueryPlanImpl> QueryPlanBuilder::Build(
    uint32_t row_count,
    const std::vector<std::shared_ptr<Column>>& columns,
    const std::vector<Index>& indexes,
    std::vector<FilterSpec>& specs,
    const std::vector<DistinctSpec>& distinct,
    const std::vector<SortSpec>& sort_specs,
    const LimitSpec& limit_spec,
    uint64_t cols_used) {
  i::BytecodeBuilder bytecode_builder;
  DataframeRegisterCache cache(bytecode_builder);

  // Initialize with a range covering all rows.
  i::RwHandle<Range> range = bytecode_builder.AllocateRegister<Range>();
  {
    using B = i::InitRange;
    auto& ir = bytecode_builder.AddOpcode<B>(i::Index<B>());
    ir.arg<B::size>() = row_count;
    ir.arg<B::dest_register>() = range;
  }

  QueryPlanBuilder builder(bytecode_builder, cache, range, row_count, columns,
                           indexes);
  RETURN_IF_ERROR(builder.Filter(specs));
  builder.Distinct(distinct);
  if (builder.CanUseMinMaxOptimization(sort_specs, limit_spec)) {
    builder.MinMax(sort_specs[0]);
    builder.Output({}, cols_used);
  } else {
    builder.Sort(sort_specs);
    builder.Output(limit_spec, cols_used);
  }
  return std::move(builder).Build();
}

i::RegValue QueryPlanImpl::GetRegisterInitValue(const RegisterInit& init,
                                                const Column* const* columns,
                                                const Index* indexes) {
  switch (init.kind.index()) {
    case RegisterInit::Type::GetTypeIndex<Id>():
      // Id columns don't have actual storage - the row index IS the value.
      // Return a nullptr StoragePtr which the interpreter knows to handle.
      return i::StoragePtr{nullptr, Id{}};
    case RegisterInit::Type::GetTypeIndex<Uint32>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Uint32>(),
          Uint32{},
      };
    case RegisterInit::Type::GetTypeIndex<Int32>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Int32>(),
          Int32{},
      };
    case RegisterInit::Type::GetTypeIndex<Int64>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Int64>(),
          Int64{},
      };
    case RegisterInit::Type::GetTypeIndex<Double>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<Double>(),
          Double{},
      };
    case RegisterInit::Type::GetTypeIndex<String>():
      return i::StoragePtr{
          columns[init.source_index]->storage.unchecked_data<String>(),
          String{},
      };
    case RegisterInit::Type::GetTypeIndex<RegisterInit::NullBitvector>():
      return columns[init.source_index]->null_storage.MaybeGetNullBitVector();
    case RegisterInit::Type::GetTypeIndex<RegisterInit::IndexVector>():
      return Span<uint32_t>(
          indexes[init.source_index].permutation_vector()->data(),
          indexes[init.source_index].permutation_vector()->data() +
              indexes[init.source_index].permutation_vector()->size());
    case RegisterInit::Type::GetTypeIndex<
        RegisterInit::SmallValueEqBitvector>(): {
      const auto& sve = columns[init.source_index]
                            ->specialized_storage
                            .unchecked_get<SpecializedStorage::SmallValueEq>();
      return &sve.bit_vector;
    }
    case RegisterInit::Type::GetTypeIndex<
        RegisterInit::SmallValueEqPopcount>(): {
      const auto& sve = columns[init.source_index]
                            ->specialized_storage
                            .unchecked_get<SpecializedStorage::SmallValueEq>();
      return Span<const uint32_t>(
          sve.prefix_popcount.data(),
          sve.prefix_popcount.data() + sve.prefix_popcount.size());
    }
    default:
      PERFETTO_FATAL("Unhandled RegisterInit kind: %u",
                     static_cast<uint32_t>(init.kind.index()));
  }
}

i::RegValue QueryPlanImpl::GetRegisterInitValue(const RegisterInit& init,
                                                const Dataframe& df) {
  return GetRegisterInitValue(init, df.column_ptrs_.data(), df.indexes_.data());
}

base::StatusOr<FilterResult> QueryPlanBuilder::Filter(
    i::BytecodeBuilder& builder,
    DataframeRegisterCache& cache,
    IndicesReg input_indices,
    const Dataframe& df,
    std::vector<FilterSpec>& specs) {
  QueryPlanBuilder plan_builder(builder, cache, input_indices, df.row_count_,
                                df.columns_, df.indexes_);
  RETURN_IF_ERROR(plan_builder.Filter(specs));
  return FilterResult{plan_builder.indices_reg_,
                      std::move(plan_builder.plan_.register_inits)};
}

base::Status QueryPlanBuilder::Filter(std::vector<FilterSpec>& specs) {
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
    FilterSpec& c = specs[i];
    auto non_null_op = c.op.TryDowncast<i::NonNullOp>();
    if (!non_null_op) {
      continue;
    }
    const Column& col = GetColumn(c.col);
    if (!TrySortedConstraint(c, col.storage.type(), *non_null_op)) {
      continue;
    }
    specs_handled[i] = true;
  }

  // Phase 2: Handle constraints which can use an index.
  std::optional<BestIndex> best_index =
      GetBestIndexForFilterSpecs(plan_.params, specs, specs_handled, indexes_);
  if (best_index) {
    IndexConstraints(specs, specs_handled, best_index->best_index_idx,
                     best_index->best_index_specs);
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
      i::RwHandle<i::CastFilterValueListResult> value =
          builder_.AllocateRegister<i::CastFilterValueListResult>();
      {
        using B = i::CastFilterValueListBase;
        auto& bc = AddOpcode<B>(i::Index<i::CastFilterValueList>(ct),
                                UnchangedRowCount{});
        bc.arg<B::fval_handle>() = {plan_.params.filter_value_count};
        bc.arg<B::write_register>() = value;
        bc.arg<B::op>() = Eq{};
        c.value_index = plan_.params.filter_value_count++;
      }
      auto update = EnsureIndicesAreInSlab();
      PruneNullIndices(c.col, update);
      auto source = TranslateNonNullIndices(c.col, update, false);
      {
        using B = i::InBase;
        B& bc = AddOpcode<B>(i::Index<i::In>(col.storage.type()),
                             RowCountModifier{NonEqualityFilterRowCount{}});
        bc.arg<B::storage_register>() =
            StorageRegisterFor(c.col, col.storage.type());
        bc.arg<B::value_list_register>() = value;
        bc.arg<B::source_register>() = source;
        bc.arg<B::update_register>() = update;
      }
      MaybeReleaseScratchSpanRegister();
      continue;
    }

    // Get the non-null operation (all our ops are non-null at this point)
    auto non_null_op = c.op.TryDowncast<i::NonNullOp>();
    if (!non_null_op) {
      NullConstraint(*c.op.TryDowncast<i::NullOp>(), c);
      continue;
    }

    // Handle non-string data types
    if (const auto& n = ct.TryDowncast<i::NonStringType>(); n) {
      if (auto op = c.op.TryDowncast<i::NonStringOp>(); op) {
        NonStringConstraint(c, *n, *op, CastFilterValue(c, ct, *non_null_op));
      } else {
        SetGuaranteedToBeEmpty();
      }
      continue;
    }

    PERFETTO_CHECK(ct.Is<String>());
    auto op = non_null_op->TryDowncast<i::StringOp>();
    PERFETTO_CHECK(op);
    RETURN_IF_ERROR(
        StringConstraint(c, *op, CastFilterValue(c, ct, *non_null_op)));
  }
  return base::OkStatus();
}

void QueryPlanBuilder::Distinct(
    const std::vector<DistinctSpec>& distinct_specs) {
  if (distinct_specs.empty()) {
    return;
  }
  std::vector<RowLayoutParams> row_layout_params;
  row_layout_params.reserve(distinct_specs.size());
  for (const auto& spec : distinct_specs) {
    row_layout_params.push_back({spec.col, false});
  }
  uint16_t total_row_stride = CalculateRowLayoutStride(row_layout_params);
  i::RwHandle<Span<uint32_t>> indices = EnsureIndicesAreInSlab();
  auto buffer_reg =
      CopyToRowLayout(total_row_stride, indices, {}, row_layout_params);
  {
    using B = i::Distinct;
    auto& bc = AddOpcode<B>(NonEqualityFilterRowCount{});
    bc.arg<B::buffer_register>() = buffer_reg;
    bc.arg<B::total_row_stride>() = total_row_stride;
    bc.arg<B::indices_register>() = indices;
  }
}

void QueryPlanBuilder::Sort(const std::vector<SortSpec>& sort_specs) {
  if (sort_specs.empty()) {
    return;
  }

  // Optimization: If there's a single sort constraint on a NonNull
  // column that is already sorted accordingly, skip the sort operation.
  if (sort_specs.size() == 1) {
    const auto& single_spec = sort_specs[0];
    const Column& col = GetColumn(single_spec.col);
    if (col.null_storage.nullability().Is<NonNull>() &&
        (col.sort_state.Is<Sorted>() || col.sort_state.Is<IdSorted>() ||
         col.sort_state.Is<SetIdSorted>())) {
      switch (single_spec.direction) {
        case SortDirection::kAscending:
          // The column is NonNull and already sorted as required.
          return;
        case SortDirection::kDescending:
          // The column is NonNull and sorted in the reverse order. Just
          // reverse the indices to get the correct order.
          {
            auto indices = EnsureIndicesAreInSlab();
            using B = i::Reverse;
            auto& op = AddOpcode<B>(UnchangedRowCount{});
            op.arg<B::update_register>() = indices;
            return;
          }
      }
    }
  }

  // main_indices_span will be modified by the final sort operation.
  // EnsureIndicesAreInSlab makes it an RwHandle.
  i::RwHandle<Span<uint32_t>> indices = EnsureIndicesAreInSlab();

  bool has_string_sort_keys = false;
  for (const auto& spec : sort_specs) {
    if (GetColumn(spec.col).storage.type().Is<String>()) {
      has_string_sort_keys = true;
      break;
    }
  }

  using Map = i::StringIdToRankMap;
  i::RwHandle<Map> string_rank_map;
  if (has_string_sort_keys) {
    string_rank_map = builder_.AllocateRegister<Map>();
    {
      using B = i::InitRankMap;
      auto& op = AddOpcode<B>(UnchangedRowCount{});
      op.arg<B::dest_register>() = string_rank_map;
    }

    // For each string column in the sort specification, collect its unique IDs.
    // This involves preparing a temporary set of indices for that column which
    // are non-null and translated to storage indices if originally sparse.
    for (const auto& spec : sort_specs) {
      const Column& col = GetColumn(spec.col);
      if (!col.storage.type().Is<String>()) {
        continue;
      }

      i::RwHandle<Span<uint32_t>> translated;
      if (col.null_storage.nullability().Is<NonNull>()) {
        // If the column is non-null, we can use the main indices directly.
        translated = indices;
      } else {
        // Get a scratch register to prepare indices for this specific column.
        // This ensures that the main_indices_span is not modified, allowing
        // each string column to be processed independently from the original
        // set of rows.
        i::RwHandle<Span<uint32_t>> scratch =
            GetOrCreateScratchSpanRegister(plan_.params.max_row_count);

        // 1. Copy the current indices to our temporary scratch span.
        {
          auto& op = AddOpcode<i::StrideCopy>(UnchangedRowCount{});
          op.arg<i::StrideCopy::source_register>() = indices;
          op.arg<i::StrideCopy::update_register>() = scratch;
          op.arg<i::StrideCopy::stride>() = 1;
        }

        // 2. Prune nulls from this temporary span in-place.
        PruneNullIndices(spec.col, scratch);

        // 3. Translate these non-null table indices to storage indices if
        // necessary.
        translated = TranslateNonNullIndices(spec.col, scratch, true);
        PERFETTO_CHECK(translated.index == scratch.index);
      }

      // Collect IDs using the prepared (non-null, translated) indices.
      {
        using B = i::CollectIdIntoRankMap;
        auto& op = AddOpcode<B>(UnchangedRowCount{});
        op.arg<B::storage_register>() =
            StorageRegisterFor(spec.col, col.storage.type());
        op.arg<B::source_register>() = translated;
        op.arg<B::rank_map_register>() = string_rank_map;
      }

      // Maybe release the scratch register if we used one.
      MaybeReleaseScratchSpanRegister();
    }

    // Finalize ranks in the map (sorts keys, updates map values to ranks).
    // The argument name in the patch for FinalizeRanksInMap was
    // 'update_register_register'.
    {
      using B = i::FinalizeRanksInMap;
      auto& op = AddOpcode<B>(UnchangedRowCount{});
      op.arg<B::update_register>() = string_rank_map;
    }
  }

  std::vector<RowLayoutParams> row_layout_params;
  row_layout_params.reserve(sort_specs.size());
  for (const auto& spec : sort_specs) {
    row_layout_params.push_back(
        {spec.col, columns_[spec.col]->storage.type().Is<String>(),
         spec.direction == SortDirection::kDescending});
  }
  uint16_t total_row_stride = CalculateRowLayoutStride(row_layout_params);
  auto buffer_reg = CopyToRowLayout(total_row_stride, indices, string_rank_map,
                                    row_layout_params);
  {
    using B = i::SortRowLayout;
    auto& op = AddOpcode<B>(UnchangedRowCount{});
    op.arg<B::buffer_register>() = buffer_reg;
    op.arg<B::total_row_stride>() = total_row_stride;
    op.arg<B::indices_register>() = indices;
  }
}

void QueryPlanBuilder::MinMax(const SortSpec& sort_spec) {
  uint32_t col_idx = sort_spec.col;
  const auto& col = GetColumn(col_idx);
  StorageType storage_type = col.storage.type();

  i::MinMaxOp mmop = sort_spec.direction == SortDirection::kAscending
                         ? i::MinMaxOp(i::MinOp{})
                         : i::MinMaxOp(i::MaxOp{});

  auto indices = EnsureIndicesAreInSlab();
  using B = i::FindMinMaxIndexBase;
  auto& op = AddOpcode<B>(i::Index<i::FindMinMaxIndex>(storage_type, mmop),
                          OneRowCount{});
  op.arg<B::update_register>() = indices;
  op.arg<B::storage_register>() = StorageRegisterFor(col_idx, storage_type);
}

void QueryPlanBuilder::Output(const LimitSpec& limit, uint64_t cols_used) {
  // Structure to track column and offset pairs
  struct ColAndOffset {
    uint32_t col;
    uint32_t offset;
  };

  base::SmallVector<ColAndOffset, 24> null_cols;
  plan_.params.output_per_row = 1;
  for (uint32_t i = 0; i < columns_.size(); ++i) {
    plan_.col_to_output_offset.emplace_back();
  }

  // Process each column that will be used in the output
  for (uint32_t i = 0; i < columns_.size(); ++i) {
    // Any column with index >= 64 uses the 64th bit in cols_used.
    uint64_t mask = 1ULL << std::min(i, 63u);
    if ((cols_used & mask) == 0) {
      continue;
    }
    const auto& col = GetColumn(i);
    switch (col.null_storage.nullability().index()) {
      case Nullability::GetTypeIndex<SparseNull>():
      case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
      case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
      case Nullability::GetTypeIndex<DenseNull>(): {
        uint32_t offset = plan_.params.output_per_row++;
        null_cols.emplace_back(ColAndOffset{i, offset});
        plan_.col_to_output_offset[i] = offset;
        break;
      }
      case Nullability::GetTypeIndex<NonNull>():
        // For non-null columns, we can directly use the indices
        plan_.col_to_output_offset[i] = 0;
        break;
      default:
        PERFETTO_FATAL("Unreachable");
    }
  }

  auto in_memory_indices = EnsureIndicesAreInSlab();
  if (limit.limit || limit.offset) {
    auto o = limit.offset.value_or(0);
    auto l = limit.limit.value_or(std::numeric_limits<uint32_t>::max());
    using B = i::LimitOffsetIndices;
    auto& bc = AddOpcode<B>(LimitOffsetRowCount{l, o});
    bc.arg<B::offset_value>() = o;
    bc.arg<B::limit_value>() = l;
    bc.arg<B::update_register>() = in_memory_indices;
  }

  i::RwHandle<Span<uint32_t>> storage_update_register;
  if (plan_.params.output_per_row > 1) {
    i::RwHandle<Slab<uint32_t>> slab_register =
        builder_.AllocateRegister<Slab<uint32_t>>();
    storage_update_register = builder_.AllocateRegister<Span<uint32_t>>();
    {
      using B = i::AllocateIndices;
      auto& bc = AddOpcode<B>(UnchangedRowCount{});
      bc.arg<B::size>() =
          plan_.params.max_row_count * plan_.params.output_per_row;
      bc.arg<B::dest_slab_register>() = slab_register;
      bc.arg<B::dest_span_register>() = storage_update_register;
    }
    {
      using B = i::StrideCopy;
      auto& bc = AddOpcode<B>(UnchangedRowCount{});
      bc.arg<B::source_register>() = in_memory_indices;
      bc.arg<B::update_register>() = storage_update_register;
      bc.arg<B::stride>() = plan_.params.output_per_row;
    }
    for (auto [col, offset] : null_cols) {
      const auto& c = GetColumn(col);
      switch (c.null_storage.nullability().index()) {
        case Nullability::GetTypeIndex<SparseNull>():
        case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
        case Nullability::GetTypeIndex<
            SparseNullWithPopcountUntilFinalization>(): {
          using B = i::StrideTranslateAndCopySparseNullIndices;
          auto reg = PrefixPopcountRegisterFor(col);
          auto& bc = AddOpcode<B>(UnchangedRowCount{});
          bc.arg<B::update_register>() = storage_update_register;
          bc.arg<B::popcount_register>() = {reg};
          bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(col);
          bc.arg<B::offset>() = offset;
          bc.arg<B::stride>() = plan_.params.output_per_row;
          break;
        }
        case Nullability::GetTypeIndex<DenseNull>(): {
          using B = i::StrideCopyDenseNullIndices;
          auto& bc = AddOpcode<B>(UnchangedRowCount{});
          bc.arg<B::update_register>() = storage_update_register;
          bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(col);
          bc.arg<B::offset>() = offset;
          bc.arg<B::stride>() = plan_.params.output_per_row;
          break;
        }
        case Nullability::GetTypeIndex<NonNull>():
        default:
          PERFETTO_FATAL("Unreachable");
      }
    }
  } else {
    PERFETTO_CHECK(null_cols.empty());
    storage_update_register = in_memory_indices;
  }
  plan_.params.output_register = storage_update_register;
}

QueryPlanImpl QueryPlanBuilder::Build() && {
  plan_.bytecode = std::move(builder_.bytecode());
  plan_.params.register_count = builder_.register_count();
  return std::move(plan_);
}

void QueryPlanBuilder::NonStringConstraint(
    const FilterSpec& c,
    const i::NonStringType& type,
    const i::NonStringOp& op,
    const i::ReadHandle<i::CastFilterValueResult>& result) {
  const auto& col = GetColumn(c.col);
  if (std::holds_alternative<i::RwHandle<Range>>(indices_reg_) && op.Is<Eq>() &&
      col.null_storage.nullability().Is<NonNull>()) {
    // Non null equality on an id column should have been handled earlier.
    PERFETTO_CHECK(!type.Is<Id>());
    auto non_id_type = type.TryDowncast<i::NonIdStorageType>();
    PERFETTO_CHECK(non_id_type);
    AddLinearFilterEqBytecode(c, result, *non_id_type);
    return;
  }
  auto update = EnsureIndicesAreInSlab();
  PruneNullIndices(c.col, update);
  auto source = TranslateNonNullIndices(c.col, update, false);
  {
    using B = i::NonStringFilterBase;
    B& bc = AddOpcode<B>(
        i::Index<i::NonStringFilter>(type, op),
        op.Is<Eq>()
            ? RowCountModifier{EqualityFilterRowCount{col.duplicate_state}}
            : RowCountModifier{NonEqualityFilterRowCount{}});
    bc.arg<B::storage_register>() =
        StorageRegisterFor(c.col, type.Upcast<StorageType>());
    bc.arg<B::val_register>() = result;
    bc.arg<B::source_register>() = source;
    bc.arg<B::update_register>() = update;
  }
  MaybeReleaseScratchSpanRegister();
}

base::Status QueryPlanBuilder::StringConstraint(
    const FilterSpec& c,
    const i::StringOp& op,
    const i::ReadHandle<i::CastFilterValueResult>& result) {
  const auto& col = GetColumn(c.col);
  if (op.Is<Eq>() && std::holds_alternative<i::RwHandle<Range>>(indices_reg_) &&
      col.null_storage.nullability().Is<NonNull>()) {
    AddLinearFilterEqBytecode(c, result, i::NonIdStorageType{String{}});
    return base::OkStatus();
  }
  if constexpr (!regex::IsRegexSupported()) {
    if (op.Is<Regex>()) {
      return base::ErrStatus(
          "Regex is not supported on non-Unix platforms (e.g. Windows).");
    }
  }
  auto update = EnsureIndicesAreInSlab();
  PruneNullIndices(c.col, update);
  auto source = TranslateNonNullIndices(c.col, update, false);
  {
    using B = i::StringFilterBase;
    B& bc = AddOpcode<B>(
        i::Index<i::StringFilter>(op),
        op.Is<Eq>()
            ? RowCountModifier{EqualityFilterRowCount{col.duplicate_state}}
            : RowCountModifier{NonEqualityFilterRowCount{}});
    bc.arg<B::storage_register>() = StorageRegisterFor(c.col, String{});
    bc.arg<B::val_register>() = result;
    bc.arg<B::source_register>() = source;
    bc.arg<B::update_register>() = update;
  }
  MaybeReleaseScratchSpanRegister();
  return base::OkStatus();
}

void QueryPlanBuilder::NullConstraint(const i::NullOp& op, FilterSpec& c) {
  // Even if we don't need this to filter null/non-null, we add it so that
  // the caller (i.e. SQLite) knows that we are able to handle the constraint.
  c.value_index = plan_.params.filter_value_count++;

  const auto& col = GetColumn(c.col);
  uint32_t nullability_type_index = col.null_storage.nullability().index();
  switch (nullability_type_index) {
    case Nullability::GetTypeIndex<SparseNull>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
    case Nullability::GetTypeIndex<DenseNull>(): {
      auto indices = EnsureIndicesAreInSlab();
      {
        using B = i::NullFilterBase;
        B& bc = AddOpcode<B>(i::Index<i::NullFilter>(op),
                             NonEqualityFilterRowCount{});
        bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(c.col);
        bc.arg<B::update_register>() = indices;
      }
      break;
    }
    case Nullability::GetTypeIndex<NonNull>():
      if (op.Is<IsNull>()) {
        SetGuaranteedToBeEmpty();
        return;
      }
      // Nothing to do as the column is non-null.
      return;
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

void QueryPlanBuilder::IndexConstraints(
    std::vector<FilterSpec>& specs,
    std::vector<uint8_t>& specs_handled,
    uint32_t index_idx,
    const std::vector<uint32_t>& filter_specs) {
  i::RwHandle<Span<uint32_t>> source_reg = IndexRegisterFor(index_idx);
  i::RwHandle<Span<uint32_t>> dest_reg =
      builder_.AllocateRegister<Span<uint32_t>>();
  for (uint32_t spec_idx : filter_specs) {
    FilterSpec& fs = specs[spec_idx];
    const Column& column = GetColumn(fs.col);
    auto value_reg = CastFilterValue(fs, column.storage.type(),
                                     *fs.op.TryDowncast<i::NonNullOp>());
    auto non_id = column.storage.type().TryDowncast<i::NonIdStorageType>();
    PERFETTO_CHECK(non_id);
    {
      using B = i::IndexedFilterEqBase;
      using PopcountHandle = i::ReadHandle<Slab<uint32_t>>;
      PopcountHandle popcount_register;
      if (column.null_storage.nullability().IsAnyOf<SparseNullTypes>()) {
        popcount_register = PrefixPopcountRegisterFor(fs.col);
      } else {
        // Dummy register for non-sparse null columns. IndexedFilterEq knows
        // how to handle this.
        popcount_register = builder_.AllocateRegister<Slab<uint32_t>>();
      }
      auto& bc = AddOpcode<B>(
          i::Index<i::IndexedFilterEq>(
              *non_id, NullabilityToSparseNullCollapsedNullability(
                           column.null_storage.nullability())),
          RowCountModifier{EqualityFilterRowCount{column.duplicate_state}});
      bc.arg<B::storage_register>() =
          StorageRegisterFor(fs.col, non_id->Upcast<StorageType>());
      bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(fs.col);
      bc.arg<B::filter_value_reg>() = value_reg;
      bc.arg<B::popcount_register>() = popcount_register;
      bc.arg<B::source_register>() = source_reg;
      bc.arg<B::dest_register>() = dest_reg;
    }
    // After first filter, subsequent filters read from dest and write back to
    // dest.
    source_reg = dest_reg;
    specs_handled[spec_idx] = true;
  }

  PERFETTO_CHECK(std::holds_alternative<i::RwHandle<Range>>(indices_reg_));
  const auto& indices_reg =
      base::unchecked_get<i::RwHandle<Range>>(indices_reg_);

  i::RwHandle<Slab<uint32_t>> output_slab_reg =
      builder_.AllocateRegister<Slab<uint32_t>>();
  i::RwHandle<Span<uint32_t>> output_span_reg =
      builder_.AllocateRegister<Span<uint32_t>>();
  {
    using B = i::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = output_slab_reg;
    bc.arg<B::dest_span_register>() = output_span_reg;
  }
  {
    using B = i::CopySpanIntersectingRange;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::source_register>() = dest_reg;
    bc.arg<B::source_range_register>() = indices_reg;
    bc.arg<B::update_register>() = output_span_reg;
  }
  indices_reg_ = output_span_reg;
}

bool QueryPlanBuilder::TrySortedConstraint(FilterSpec& fs,
                                           const StorageType& ct,
                                           const i::NonNullOp& op) {
  const auto& col = GetColumn(fs.col);
  const auto& nullability = col.null_storage.nullability();
  if (!nullability.Is<NonNull>() || col.sort_state.Is<Unsorted>()) {
    return false;
  }
  auto range_op = op.TryDowncast<i::RangeOp>();
  if (!range_op) {
    return false;
  }

  // We should have ordered the constraints such that we only reach this
  // point with range indices.
  PERFETTO_CHECK(std::holds_alternative<i::RwHandle<Range>>(indices_reg_));
  const auto& reg = base::unchecked_get<i::RwHandle<Range>>(indices_reg_);

  auto value_reg = CastFilterValue(fs, ct, op);

  // Handle set id equality with a specialized opcode.
  if (ct.Is<Uint32>() && col.sort_state.Is<SetIdSorted>() && op.Is<Eq>()) {
    using B = i::Uint32SetIdSortedEq;
    auto& bc = AddOpcode<B>(
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::storage_register>() = StorageRegisterFor(fs.col, ct);
    bc.arg<B::val_register>() = value_reg;
    bc.arg<B::update_register>() = reg;
    return true;
  }

  if (col.specialized_storage.Is<SpecializedStorage::SmallValueEq>() &&
      op.Is<Eq>()) {
    using B = i::SpecializedStorageSmallValueEq;
    auto& bc = AddOpcode<B>(
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::small_value_bv_register>() = SmallValueEqBvRegisterFor(fs.col);
    bc.arg<B::small_value_popcount_register>() =
        SmallValueEqPopcountRegisterFor(fs.col);
    bc.arg<B::val_register>() = value_reg;
    bc.arg<B::update_register>() = reg;
    return true;
  }

  const auto& [bound, erlbub] = GetSortedFilterArgs(*range_op);
  RowCountModifier modifier;
  if (op.Is<Eq>()) {
    modifier = EqualityFilterRowCount{col.duplicate_state};
  } else {
    modifier = NonEqualityFilterRowCount{};
  }
  {
    using B = i::SortedFilterBase;
    auto& bc = AddOpcode<B>(i::Index<i::SortedFilter>(ct, erlbub), modifier,
                            i::SortedFilterBase::EstimateCost(ct));
    bc.arg<B::storage_register>() = StorageRegisterFor(fs.col, ct);
    bc.arg<B::val_register>() = value_reg;
    bc.arg<B::update_register>() = reg;
    bc.arg<B::write_result_to>() = bound;
  }
  return true;
}

void QueryPlanBuilder::PruneNullIndices(uint32_t col,
                                        i::RwHandle<Span<uint32_t>> indices) {
  switch (GetColumn(col).null_storage.nullability().index()) {
    case Nullability::GetTypeIndex<SparseNull>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
    case Nullability::GetTypeIndex<DenseNull>(): {
      using B = i::NullFilter<IsNotNull>;
      i::NullFilterBase& bc = AddOpcode<B>(NonEqualityFilterRowCount{});
      bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(col);
      bc.arg<B::update_register>() = indices;
      break;
    }
    case Nullability::GetTypeIndex<NonNull>():
      break;
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

i::RwHandle<Span<uint32_t>> QueryPlanBuilder::TranslateNonNullIndices(
    uint32_t col,
    i::RwHandle<Span<uint32_t>> table_indices_register,
    bool in_place) {
  switch (GetColumn(col).null_storage.nullability().index()) {
    case Nullability::GetTypeIndex<SparseNull>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>(): {
      auto update =
          in_place ? table_indices_register
                   : GetOrCreateScratchSpanRegister(plan_.params.max_row_count);
      auto popcount_reg = PrefixPopcountRegisterFor(col);
      {
        using B = i::TranslateSparseNullIndices;
        auto& bc = AddOpcode<B>(UnchangedRowCount{});
        bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(col);
        bc.arg<B::popcount_register>() = popcount_reg;
        bc.arg<B::source_register>() = table_indices_register;
        bc.arg<B::update_register>() = update;
      }
      return update;
    }
    case Nullability::GetTypeIndex<DenseNull>():
    case Nullability::GetTypeIndex<NonNull>():
      return table_indices_register;
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

PERFETTO_NO_INLINE i::RwHandle<Span<uint32_t>>
QueryPlanBuilder::EnsureIndicesAreInSlab() {
  using SpanReg = i::RwHandle<Span<uint32_t>>;
  using SlabReg = i::RwHandle<Slab<uint32_t>>;

  if (PERFETTO_LIKELY(std::holds_alternative<SpanReg>(indices_reg_))) {
    return base::unchecked_get<SpanReg>(indices_reg_);
  }

  using RegRange = i::RwHandle<Range>;
  PERFETTO_DCHECK(std::holds_alternative<RegRange>(indices_reg_));
  auto range_reg = base::unchecked_get<RegRange>(indices_reg_);

  SlabReg slab_reg = builder_.AllocateRegister<Slab<uint32_t>>();
  SpanReg span_reg = builder_.AllocateRegister<Span<uint32_t>>();
  {
    using B = i::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }
  {
    using B = i::Iota;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::source_register>() = range_reg;
    bc.arg<B::update_register>() = span_reg;
  }
  indices_reg_ = span_reg;
  return span_reg;
}

PERFETTO_NO_INLINE i::Bytecode& QueryPlanBuilder::AddRawOpcode(
    uint32_t option,
    RowCountModifier rc,
    i::Cost cost) {
  static constexpr uint32_t kFixedBytecodeCost = 5;
  switch (cost.index()) {
    case base::variant_index<i::Cost, i::FixedCost>(): {
      const auto& c = base::unchecked_get<i::FixedCost>(cost);
      plan_.params.estimated_cost += c.cost;
      break;
    }
    case base::variant_index<i::Cost, i::LogPerRowCost>(): {
      const auto& c = base::unchecked_get<i::LogPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * log2(plan_.params.estimated_row_count);
      break;
    }
    case base::variant_index<i::Cost, i::LinearPerRowCost>(): {
      const auto& c = base::unchecked_get<i::LinearPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * plan_.params.estimated_row_count;
      break;
    }
    case base::variant_index<i::Cost, i::LogLinearPerRowCost>(): {
      const auto& c = base::unchecked_get<i::LogLinearPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * plan_.params.estimated_row_count *
                    log2(plan_.params.estimated_row_count);
      break;
    }
    case base::variant_index<i::Cost, i::PostOperationLinearPerRowCost>():
      break;
    default:
      PERFETTO_FATAL("Unknown cost type");
  }
  switch (rc.index()) {
    case base::variant_index<RowCountModifier, UnchangedRowCount>():
      break;
    case base::variant_index<RowCountModifier, NonEqualityFilterRowCount>():
      if (plan_.params.estimated_row_count > 1) {
        plan_.params.estimated_row_count = plan_.params.estimated_row_count / 2;
      } else {
        // Leave the estimated row count as is if it is already 1 or less.
      }
      break;
    case base::variant_index<RowCountModifier, EqualityFilterRowCount>(): {
      const auto& eq = base::unchecked_get<EqualityFilterRowCount>(rc);
      if (eq.duplicate_state.Is<HasDuplicates>()) {
        if (plan_.params.estimated_row_count > 1) {
          double new_count = plan_.params.estimated_row_count /
                             (2 * log2(plan_.params.estimated_row_count));
          plan_.params.estimated_row_count =
              std::max(1u, static_cast<uint32_t>(new_count));
        } else {
          // Leave the estimated row count as is if it is already 1 or less.
        }
      } else {
        PERFETTO_CHECK(eq.duplicate_state.Is<NoDuplicates>());
        plan_.params.estimated_row_count =
            std::min(1u, plan_.params.estimated_row_count);
        plan_.params.max_row_count = std::min(1u, plan_.params.max_row_count);
      }
      break;
    }
    case base::variant_index<RowCountModifier, OneRowCount>():
      plan_.params.estimated_row_count =
          std::min(1u, plan_.params.estimated_row_count);
      plan_.params.max_row_count = std::min(1u, plan_.params.max_row_count);
      break;
    case base::variant_index<RowCountModifier, ZeroRowCount>():
      plan_.params.estimated_row_count = 0;
      plan_.params.max_row_count = 0;
      break;
    case base::variant_index<RowCountModifier, LimitOffsetRowCount>(): {
      const auto& lc = base::unchecked_get<LimitOffsetRowCount>(rc);

      // Offset will cut out `offset` rows from the start of indices.
      uint32_t remove_from_start =
          std::min(plan_.params.max_row_count, lc.offset);
      plan_.params.max_row_count -= remove_from_start;

      // Limit will only preserve at most `limit` rows.
      plan_.params.max_row_count =
          std::min(lc.limit, plan_.params.max_row_count);

      // The max row count is also the best possible estimate we can make for
      // the row count.
      plan_.params.estimated_row_count = plan_.params.max_row_count;
      break;
    }
    default:
      PERFETTO_FATAL("Unknown row count modifier type");
  }
  // Handle all the cost types which need to be calculated *post* the row
  // estimate update.
  if (cost.index() ==
      base::variant_index<i::Cost, i::PostOperationLinearPerRowCost>()) {
    const auto& c = base::unchecked_get<i::PostOperationLinearPerRowCost>(cost);
    plan_.params.estimated_cost += c.cost * plan_.params.estimated_cost;
  }
  return builder_.AddRawOpcode(option);
}

void QueryPlanBuilder::SetGuaranteedToBeEmpty() {
  i::RwHandle<Slab<uint32_t>> slab_reg =
      builder_.AllocateRegister<Slab<uint32_t>>();
  i::RwHandle<Span<uint32_t>> span_reg =
      builder_.AllocateRegister<Span<uint32_t>>();
  {
    using B = i::AllocateIndices;
    auto& bc = AddOpcode<B>(ZeroRowCount{});
    bc.arg<B::size>() = 0;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }
  indices_reg_ = span_reg;
}

i::ReadHandle<Slab<uint32_t>> QueryPlanBuilder::PrefixPopcountRegisterFor(
    uint32_t col) {
  auto [reg, inserted] = cache_.GetOrAllocate<Slab<uint32_t>>(
      kPrefixPopcountReg, columns_[col].get());
  if (inserted) {
    using B = i::PrefixPopcount;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::null_bv_register>() = NullBitvectorRegisterFor(col);
    bc.arg<B::dest_register>() = reg;
  }
  return reg;
}

i::RwHandle<i::StoragePtr> QueryPlanBuilder::StorageRegisterFor(
    uint32_t col,
    StorageType type) {
  auto [reg, inserted] =
      cache_.GetOrAllocate<i::StoragePtr>(kStorageReg, columns_[col].get());
  if (inserted) {
    plan_.register_inits.emplace_back(
        RegisterInit{reg.index, type.Upcast<RegisterInit::Type>(),
                     static_cast<uint16_t>(col)});
  }
  return reg;
}

i::ReadHandle<const BitVector*> QueryPlanBuilder::NullBitvectorRegisterFor(
    uint32_t col) {
  auto [reg, inserted] =
      cache_.GetOrAllocate<const BitVector*>(kNullBvReg, columns_[col].get());
  if (inserted) {
    plan_.register_inits.emplace_back(RegisterInit{
        reg.index, RegisterInit::NullBitvector{}, static_cast<uint16_t>(col)});
  }
  return reg;
}

i::ReadHandle<const BitVector*> QueryPlanBuilder::SmallValueEqBvRegisterFor(
    uint32_t col) {
  auto [reg, inserted] = cache_.GetOrAllocate<const BitVector*>(
      kSmallValueEqBvReg, columns_[col].get());
  if (inserted) {
    plan_.register_inits.emplace_back(
        RegisterInit{reg.index, RegisterInit::SmallValueEqBitvector{},
                     static_cast<uint16_t>(col)});
  }
  return reg;
}

i::ReadHandle<Span<const uint32_t>>
QueryPlanBuilder::SmallValueEqPopcountRegisterFor(uint32_t col) {
  auto [reg, inserted] = cache_.GetOrAllocate<Span<const uint32_t>>(
      kSmallValueEqPopcountReg, columns_[col].get());
  if (inserted) {
    plan_.register_inits.emplace_back(
        RegisterInit{reg.index, RegisterInit::SmallValueEqPopcount{},
                     static_cast<uint16_t>(col)});
  }
  return reg;
}

i::RwHandle<Span<uint32_t>> QueryPlanBuilder::IndexRegisterFor(uint32_t pos) {
  auto [reg, inserted] =
      cache_.GetOrAllocate<Span<uint32_t>>(kIndexReg, &indexes_[pos]);
  if (inserted) {
    plan_.register_inits.emplace_back(RegisterInit{
        reg.index, RegisterInit::IndexVector{}, static_cast<uint16_t>(pos)});
  }
  return reg;
}

bool QueryPlanBuilder::CanUseMinMaxOptimization(
    const std::vector<SortSpec>& sort_specs,
    const LimitSpec& limit_spec) {
  return sort_specs.size() == 1 &&
         GetColumn(sort_specs[0].col)
             .null_storage.nullability()
             .Is<NonNull>() &&
         limit_spec.limit == 1 && limit_spec.offset.value_or(0) == 0;
}

i::ReadHandle<i::CastFilterValueResult> QueryPlanBuilder::CastFilterValue(
    FilterSpec& c,
    const StorageType& ct,
    i::NonNullOp op) {
  i::RwHandle<i::CastFilterValueResult> value_reg =
      builder_.AllocateRegister<i::CastFilterValueResult>();
  {
    using B = i::CastFilterValueBase;
    auto& bc =
        AddOpcode<B>(i::Index<i::CastFilterValue>(ct), UnchangedRowCount{});
    bc.arg<B::fval_handle>() = {plan_.params.filter_value_count};
    bc.arg<B::write_register>() = value_reg;
    bc.arg<B::op>() = op;
    c.value_index = plan_.params.filter_value_count++;
  }
  return value_reg;
}

i::RwHandle<Span<uint32_t>> QueryPlanBuilder::GetOrCreateScratchSpanRegister(
    uint32_t size) {
  auto scratch = builder_.GetOrCreateScratchRegisters(size);
  {
    using B = i::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = size;
    bc.arg<B::dest_slab_register>() = scratch.slab;
    bc.arg<B::dest_span_register>() = scratch.span;
  }
  builder_.MarkScratchInUse(scratch);
  scratch_ = scratch;
  return scratch.span;
}

void QueryPlanBuilder::MaybeReleaseScratchSpanRegister() {
  if (scratch_.has_value()) {
    builder_.ReleaseScratch(*scratch_);
    scratch_ = std::nullopt;
  }
}

uint16_t QueryPlanBuilder::CalculateRowLayoutStride(
    const std::vector<RowLayoutParams>& row_layout_params) {
  PERFETTO_CHECK(!row_layout_params.empty());
  uint16_t calculated_total_row_stride = 0;
  for (const auto& param : row_layout_params) {
    const Column& col = GetColumn(param.column);
    bool is_non_null = col.null_storage.nullability().Is<NonNull>();
    calculated_total_row_stride +=
        (is_non_null ? 0u : 1u) + GetDataSize(col.storage.type());
  }
  return calculated_total_row_stride;
}

i::RwHandle<Slab<uint8_t>> QueryPlanBuilder::CopyToRowLayout(
    uint16_t row_stride,
    i::RwHandle<Span<uint32_t>> indices,
    i::ReadHandle<i::StringIdToRankMap> rank_map,
    const std::vector<RowLayoutParams>& row_layout_params) {
  uint32_t buffer_size = plan_.params.max_row_count * row_stride;
  i::RwHandle<Slab<uint8_t>> new_buffer_reg =
      builder_.AllocateRegister<Slab<uint8_t>>();
  {
    using B = i::AllocateRowLayoutBuffer;
    auto& op = AddOpcode<B>(UnchangedRowCount{});
    op.arg<B::buffer_size>() = buffer_size;
    op.arg<B::dest_buffer_register>() = new_buffer_reg;
  }
  uint16_t current_offset = 0;
  for (const auto& param : row_layout_params) {
    const Column& col = GetColumn(param.column);
    const auto& nullability = col.null_storage.nullability();
    auto popcount = nullability.IsAnyOf<SparseNullTypes>()
                        ? PrefixPopcountRegisterFor(param.column)
                        : i::ReadHandle<Slab<uint32_t>>{
                              std::numeric_limits<uint32_t>::max()};
    {
      using B = i::CopyToRowLayoutBase;
      auto index = i::Index<i::CopyToRowLayout>(
          col.storage.type(),
          NullabilityToSparseNullCollapsedNullability(nullability));
      auto& op = AddOpcode<B>(index, UnchangedRowCount{});
      op.arg<B::storage_register>() =
          StorageRegisterFor(param.column, col.storage.type());
      op.arg<B::null_bv_register>() = NullBitvectorRegisterFor(param.column);
      op.arg<B::source_indices_register>() = indices;
      op.arg<B::dest_buffer_register>() = new_buffer_reg;
      op.arg<B::rank_map_register>() = rank_map;
      op.arg<B::row_layout_offset>() = current_offset;
      op.arg<B::row_layout_stride>() = row_stride;
      op.arg<B::invert_copied_bits>() = param.invert_copied_bits;
      op.arg<B::popcount_register>() = popcount;
    }
    current_offset +=
        (nullability.Is<NonNull>() ? 0u : 1u) + GetDataSize(col.storage.type());
  }
  PERFETTO_CHECK(current_offset == row_stride);
  return new_buffer_reg;
}

void QueryPlanBuilder::AddLinearFilterEqBytecode(
    const FilterSpec& c,
    const i::ReadHandle<i::CastFilterValueResult>& filter_value_result_reg,
    const i::NonIdStorageType& non_id_storage_type) {
  const auto& col = GetColumn(c.col);
  PERFETTO_DCHECK(std::holds_alternative<i::RwHandle<Range>>(indices_reg_));
  PERFETTO_DCHECK(col.null_storage.nullability().Is<NonNull>());
  PERFETTO_DCHECK(c.op.Is<Eq>());

  using SpanReg = i::RwHandle<Span<uint32_t>>;
  using SlabReg = i::RwHandle<Slab<uint32_t>>;
  using RegRange = i::RwHandle<Range>;

  auto range_reg = base::unchecked_get<RegRange>(indices_reg_);
  SlabReg slab_reg = builder_.AllocateRegister<Slab<uint32_t>>();
  SpanReg span_reg = builder_.AllocateRegister<Span<uint32_t>>();
  {
    using B = i::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }

  {
    using B = i::LinearFilterEqBase;
    B& bc = AddOpcode<B>(
        i::Index<i::LinearFilterEq>(non_id_storage_type),
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::storage_register>() =
        StorageRegisterFor(c.col, non_id_storage_type.Upcast<StorageType>());
    bc.arg<B::filter_value_reg>() = filter_value_result_reg;
    // For NonNull columns, popcount_register is not used by LinearFilterEq
    // logic. Pass a default-constructed handle.
    bc.arg<B::popcount_register>() = i::ReadHandle<Slab<uint32_t>>{};
    bc.arg<B::source_register>() = range_reg;
    bc.arg<B::update_register>() = span_reg;
  }
  indices_reg_ = span_reg;
}

template <typename T>
T& QueryPlanBuilder::AddOpcode(RowCountModifier rc) {
  return AddOpcode<T>(i::Index<T>(), rc, T::kCost);
}

}  // namespace perfetto::trace_processor::core::dataframe
