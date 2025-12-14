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

#include "src/trace_processor/dataframe/impl/query_plan.h"

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
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/impl/bytecode_core.h"
#include "src/trace_processor/dataframe/impl/bytecode_instructions.h"
#include "src/trace_processor/dataframe/impl/bytecode_registers.h"
#include "src/trace_processor/dataframe/impl/slab.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/type_set.h"
#include "src/trace_processor/dataframe/types.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto::trace_processor::dataframe::impl {

namespace {

// Calculates filter preference score for ordering filters.
// Lower scores are applied first for better efficiency.
uint32_t FilterPreference(const FilterSpec& fs, const impl::Column& col) {
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

// Gets the appropriate bound modifier and range operation type
// for a given range operation.
std::pair<BoundModifier, EqualRangeLowerBoundUpperBound> GetSortedFilterArgs(
    const RangeOp& op) {
  switch (op.index()) {
    case RangeOp::GetTypeIndex<Eq>():
      return std::make_pair(BothBounds{}, EqualRange{});
    case RangeOp::GetTypeIndex<Lt>():
      return std::make_pair(EndBound{}, LowerBound{});
    case RangeOp::GetTypeIndex<Le>():
      return std::make_pair(EndBound{}, UpperBound{});
    case RangeOp::GetTypeIndex<Gt>():
      return std::make_pair(BeginBound{}, UpperBound{});
    case RangeOp::GetTypeIndex<Ge>():
      return std::make_pair(BeginBound{}, LowerBound{});
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

SparseNullCollapsedNullability NullabilityToSparseNullCollapsedNullability(
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
    const QueryPlan::ExecutionParams& params,
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
    uint32_t row_count,
    const std::vector<std::shared_ptr<Column>>& columns,
    const std::vector<Index>& indexes)
    : columns_(columns), indexes_(indexes) {
  for (uint32_t i = 0; i < columns_.size(); ++i) {
    column_states_.emplace_back();
  }
  // Setup the maximum and estimated row counts.
  plan_.params.max_row_count = row_count;
  plan_.params.estimated_row_count = row_count;

  // Initialize with a range covering all rows
  bytecode::reg::RwHandle<Range> range{plan_.params.register_count++};
  {
    using B = bytecode::InitRange;
    auto& ir = AddOpcode<B>(UnchangedRowCount{});
    ir.arg<B::size>() = row_count;
    ir.arg<B::dest_register>() = range;
  }
  indices_reg_ = range;
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
    auto non_null_op = c.op.TryDowncast<NonNullOp>();
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
      bytecode::reg::RwHandle<CastFilterValueListResult> value{
          plan_.params.register_count++};
      {
        using B = bytecode::CastFilterValueListBase;
        auto& bc =
            AddOpcode<B>(bytecode::Index<bytecode::CastFilterValueList>(ct),
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
        using B = bytecode::InBase;
        B& bc = AddOpcode<B>(bytecode::Index<bytecode::In>(col.storage.type()),
                             RowCountModifier{NonEqualityFilterRowCount{}});
        bc.arg<B::col>() = c.col;
        bc.arg<B::value_list_register>() = value;
        bc.arg<B::source_register>() = source;
        bc.arg<B::update_register>() = update;
      }
      MaybeReleaseScratchSpanRegister();
      continue;
    }

    // Get the non-null operation (all our ops are non-null at this point)
    auto non_null_op = c.op.TryDowncast<NonNullOp>();
    if (!non_null_op) {
      NullConstraint(*c.op.TryDowncast<NullOp>(), c);
      continue;
    }

    // Handle non-string data types
    if (const auto& n = ct.TryDowncast<NonStringType>(); n) {
      if (auto op = c.op.TryDowncast<NonStringOp>(); op) {
        NonStringConstraint(c, *n, *op, CastFilterValue(c, ct, *non_null_op));
      } else {
        SetGuaranteedToBeEmpty();
      }
      continue;
    }

    PERFETTO_CHECK(ct.Is<String>());
    auto op = non_null_op->TryDowncast<StringOp>();
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
  bytecode::reg::RwHandle<Span<uint32_t>> indices = EnsureIndicesAreInSlab();
  auto buffer_reg =
      CopyToRowLayout(total_row_stride, indices, {}, row_layout_params);
  {
    using B = bytecode::Distinct;
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
            using B = bytecode::Reverse;
            auto& op = AddOpcode<B>(UnchangedRowCount{});
            op.arg<B::update_register>() = indices;
            return;
          }
      }
    }
  }

  // main_indices_span will be modified by the final sort operation.
  // EnsureIndicesAreInSlab makes it an RwHandle.
  bytecode::reg::RwHandle<Span<uint32_t>> indices = EnsureIndicesAreInSlab();

  bool has_string_sort_keys = false;
  for (const auto& spec : sort_specs) {
    if (GetColumn(spec.col).storage.type().Is<String>()) {
      has_string_sort_keys = true;
      break;
    }
  }

  using Map = bytecode::reg::StringIdToRankMap;
  bytecode::reg::RwHandle<Map> string_rank_map;
  if (has_string_sort_keys) {
    string_rank_map =
        bytecode::reg::RwHandle<Map>{plan_.params.register_count++};
    {
      using B = bytecode::InitRankMap;
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

      bytecode::reg::RwHandle<Span<uint32_t>> translated;
      if (col.null_storage.nullability().Is<NonNull>()) {
        // If the column is non-null, we can use the main indices directly.
        translated = indices;
      } else {
        // Get a scratch register to prepare indices for this specific column.
        // This ensures that the main_indices_span is not modified, allowing
        // each string column to be processed independently from the original
        // set of rows.
        bytecode::reg::RwHandle<Span<uint32_t>> scratch =
            GetOrCreateScratchSpanRegister(plan_.params.max_row_count);

        // 1. Copy the current indices to our temporary scratch span.
        {
          auto& op = AddOpcode<bytecode::StrideCopy>(UnchangedRowCount{});
          op.arg<bytecode::StrideCopy::source_register>() = indices;
          op.arg<bytecode::StrideCopy::update_register>() = scratch;
          op.arg<bytecode::StrideCopy::stride>() = 1;
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
        using B = bytecode::CollectIdIntoRankMap;
        auto& op = AddOpcode<B>(UnchangedRowCount{});
        op.arg<B::col>() = spec.col;
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
      using B = bytecode::FinalizeRanksInMap;
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
    using B = bytecode::SortRowLayout;
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

  MinMaxOp mmop = sort_spec.direction == SortDirection::kAscending
                      ? MinMaxOp(MinOp{})
                      : MinMaxOp(MaxOp{});

  auto indices = EnsureIndicesAreInSlab();
  using B = bytecode::FindMinMaxIndexBase;
  auto& op = AddOpcode<B>(
      bytecode::Index<bytecode::FindMinMaxIndex>(storage_type, mmop),
      OneRowCount{});
  op.arg<B::update_register>() = indices;
  op.arg<B::col>() = col_idx;
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
    using B = bytecode::LimitOffsetIndices;
    auto& bc = AddOpcode<B>(LimitOffsetRowCount{l, o});
    bc.arg<B::offset_value>() = o;
    bc.arg<B::limit_value>() = l;
    bc.arg<B::update_register>() = in_memory_indices;
  }

  bytecode::reg::RwHandle<Span<uint32_t>> storage_update_register;
  if (plan_.params.output_per_row > 1) {
    bytecode::reg::RwHandle<Slab<uint32_t>> slab_register{
        plan_.params.register_count++};
    storage_update_register =
        bytecode::reg::RwHandle<Span<uint32_t>>{plan_.params.register_count++};
    {
      using B = bytecode::AllocateIndices;
      auto& bc = AddOpcode<B>(UnchangedRowCount{});
      bc.arg<B::size>() =
          plan_.params.max_row_count * plan_.params.output_per_row;
      bc.arg<B::dest_slab_register>() = slab_register;
      bc.arg<B::dest_span_register>() = storage_update_register;
    }
    {
      using B = bytecode::StrideCopy;
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
          using B = bytecode::StrideTranslateAndCopySparseNullIndices;
          auto reg = PrefixPopcountRegisterFor(col);
          auto& bc = AddOpcode<B>(UnchangedRowCount{});
          bc.arg<B::update_register>() = storage_update_register;
          bc.arg<B::popcount_register>() = {reg};
          bc.arg<B::col>() = col;
          bc.arg<B::offset>() = offset;
          bc.arg<B::stride>() = plan_.params.output_per_row;
          break;
        }
        case Nullability::GetTypeIndex<DenseNull>(): {
          using B = bytecode::StrideCopyDenseNullIndices;
          auto& bc = AddOpcode<B>(UnchangedRowCount{});
          bc.arg<B::update_register>() = storage_update_register;
          bc.arg<B::col>() = col;
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

QueryPlan QueryPlanBuilder::Build() && {
  return std::move(plan_);
}

void QueryPlanBuilder::NonStringConstraint(
    const FilterSpec& c,
    const NonStringType& type,
    const NonStringOp& op,
    const bytecode::reg::ReadHandle<CastFilterValueResult>& result) {
  const auto& col = GetColumn(c.col);
  if (std::holds_alternative<bytecode::reg::RwHandle<Range>>(indices_reg_) &&
      op.Is<Eq>() && col.null_storage.nullability().Is<NonNull>()) {
    // Non null equality on an id column should have been handled earlier.
    PERFETTO_CHECK(!type.Is<Id>());
    auto non_id_type = type.TryDowncast<NonIdStorageType>();
    PERFETTO_CHECK(non_id_type);
    AddLinearFilterEqBytecode(c, result, *non_id_type);
    return;
  }
  auto update = EnsureIndicesAreInSlab();
  PruneNullIndices(c.col, update);
  auto source = TranslateNonNullIndices(c.col, update, false);
  {
    using B = bytecode::NonStringFilterBase;
    B& bc = AddOpcode<B>(
        bytecode::Index<bytecode::NonStringFilter>(type, op),
        op.Is<Eq>()
            ? RowCountModifier{EqualityFilterRowCount{col.duplicate_state}}
            : RowCountModifier{NonEqualityFilterRowCount{}});
    bc.arg<B::col>() = c.col;
    bc.arg<B::val_register>() = result;
    bc.arg<B::source_register>() = source;
    bc.arg<B::update_register>() = update;
  }
  MaybeReleaseScratchSpanRegister();
}

base::Status QueryPlanBuilder::StringConstraint(
    const FilterSpec& c,
    const StringOp& op,
    const bytecode::reg::ReadHandle<CastFilterValueResult>& result) {
  const auto& col = GetColumn(c.col);
  if (op.Is<Eq>() &&
      std::holds_alternative<bytecode::reg::RwHandle<Range>>(indices_reg_) &&
      col.null_storage.nullability().Is<NonNull>()) {
    AddLinearFilterEqBytecode(c, result, NonIdStorageType{String{}});
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
    using B = bytecode::StringFilterBase;
    B& bc = AddOpcode<B>(
        bytecode::Index<bytecode::StringFilter>(op),
        op.Is<Eq>()
            ? RowCountModifier{EqualityFilterRowCount{col.duplicate_state}}
            : RowCountModifier{NonEqualityFilterRowCount{}});
    bc.arg<B::col>() = c.col;
    bc.arg<B::val_register>() = result;
    bc.arg<B::source_register>() = source;
    bc.arg<B::update_register>() = update;
  }
  MaybeReleaseScratchSpanRegister();
  return base::OkStatus();
}

void QueryPlanBuilder::NullConstraint(const NullOp& op, FilterSpec& c) {
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
        using B = bytecode::NullFilterBase;
        B& bc = AddOpcode<B>(bytecode::Index<bytecode::NullFilter>(op),
                             NonEqualityFilterRowCount{});
        bc.arg<B::col>() = c.col;
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
  bytecode::reg::RwHandle<Span<uint32_t>> reg{plan_.params.register_count++};
  {
    using B = bytecode::IndexPermutationVectorToSpan;
    auto& bc_alloc = AddOpcode<B>(UnchangedRowCount{});
    bc_alloc.arg<B::index>() = index_idx;
    bc_alloc.arg<B::write_register>() = reg;
  }

  for (uint32_t spec_idx : filter_specs) {
    FilterSpec& fs = specs[spec_idx];
    const Column& column = GetColumn(fs.col);
    auto value_reg = CastFilterValue(fs, column.storage.type(),
                                     *fs.op.TryDowncast<NonNullOp>());
    auto non_id = column.storage.type().TryDowncast<NonIdStorageType>();
    PERFETTO_CHECK(non_id);
    {
      using B = bytecode::IndexedFilterEqBase;
      using PopcountHandle = bytecode::reg::ReadHandle<Slab<uint32_t>>;
      PopcountHandle popcount_register;
      if (column.null_storage.nullability().IsAnyOf<SparseNullTypes>()) {
        popcount_register = PrefixPopcountRegisterFor(fs.col);
      } else {
        // Dummy register for non-sparse null columns. IndexedFilterEq knows
        // how to handle this.
        popcount_register = bytecode::reg::ReadHandle<Slab<uint32_t>>{
            plan_.params.register_count++};
      }
      auto& bc = AddOpcode<B>(
          bytecode::Index<bytecode::IndexedFilterEq>(
              *non_id, NullabilityToSparseNullCollapsedNullability(
                           column.null_storage.nullability())),
          RowCountModifier{EqualityFilterRowCount{column.duplicate_state}});
      bc.arg<B::col>() = fs.col;
      bc.arg<B::filter_value_reg>() = value_reg;
      bc.arg<B::popcount_register>() = popcount_register;
      bc.arg<B::update_register>() = reg;
    }
    specs_handled[spec_idx] = true;
  }

  PERFETTO_CHECK(
      std::holds_alternative<bytecode::reg::RwHandle<Range>>(indices_reg_));
  const auto& indices_reg =
      base::unchecked_get<bytecode::reg::RwHandle<Range>>(indices_reg_);

  bytecode::reg::RwHandle<Slab<uint32_t>> output_slab_reg{
      plan_.params.register_count++};
  bytecode::reg::RwHandle<Span<uint32_t>> output_span_reg{
      plan_.params.register_count++};
  {
    using B = bytecode::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = output_slab_reg;
    bc.arg<B::dest_span_register>() = output_span_reg;
  }
  {
    using B = bytecode::CopySpanIntersectingRange;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::source_register>() = reg;
    bc.arg<B::source_range_register>() = indices_reg;
    bc.arg<B::update_register>() = output_span_reg;
  }
  indices_reg_ = output_span_reg;
}

bool QueryPlanBuilder::TrySortedConstraint(FilterSpec& fs,
                                           const StorageType& ct,
                                           const NonNullOp& op) {
  const auto& col = GetColumn(fs.col);
  const auto& nullability = col.null_storage.nullability();
  if (!nullability.Is<NonNull>() || col.sort_state.Is<Unsorted>()) {
    return false;
  }
  auto range_op = op.TryDowncast<RangeOp>();
  if (!range_op) {
    return false;
  }

  // We should have ordered the constraints such that we only reach this
  // point with range indices.
  PERFETTO_CHECK(
      std::holds_alternative<bytecode::reg::RwHandle<Range>>(indices_reg_));
  const auto& reg =
      base::unchecked_get<bytecode::reg::RwHandle<Range>>(indices_reg_);

  auto value_reg = CastFilterValue(fs, ct, op);

  // Handle set id equality with a specialized opcode.
  if (ct.Is<Uint32>() && col.sort_state.Is<SetIdSorted>() && op.Is<Eq>()) {
    using B = bytecode::Uint32SetIdSortedEq;
    auto& bc = AddOpcode<B>(
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::col>() = fs.col;
    bc.arg<B::val_register>() = value_reg;
    bc.arg<B::update_register>() = reg;
    return true;
  }

  if (col.specialized_storage.Is<SpecializedStorage::SmallValueEq>() &&
      op.Is<Eq>()) {
    using B = bytecode::SpecializedStorageSmallValueEq;
    auto& bc = AddOpcode<B>(
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::col>() = fs.col;
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
    using B = bytecode::SortedFilterBase;
    auto& bc =
        AddOpcode<B>(bytecode::Index<bytecode::SortedFilter>(ct, erlbub),
                     modifier, bytecode::SortedFilterBase::EstimateCost(ct));
    bc.arg<B::col>() = fs.col;
    bc.arg<B::val_register>() = value_reg;
    bc.arg<B::update_register>() = reg;
    bc.arg<B::write_result_to>() = bound;
  }
  return true;
}

void QueryPlanBuilder::PruneNullIndices(
    uint32_t col,
    bytecode::reg::RwHandle<Span<uint32_t>> indices) {
  switch (GetColumn(col).null_storage.nullability().index()) {
    case Nullability::GetTypeIndex<SparseNull>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
    case Nullability::GetTypeIndex<SparseNullWithPopcountUntilFinalization>():
    case Nullability::GetTypeIndex<DenseNull>(): {
      using B = bytecode::NullFilter<IsNotNull>;
      bytecode::NullFilterBase& bc = AddOpcode<B>(NonEqualityFilterRowCount{});
      bc.arg<B::col>() = col;
      bc.arg<B::update_register>() = indices;
      break;
    }
    case Nullability::GetTypeIndex<NonNull>():
      break;
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

bytecode::reg::RwHandle<Span<uint32_t>>
QueryPlanBuilder::TranslateNonNullIndices(
    uint32_t col,
    bytecode::reg::RwHandle<Span<uint32_t>> table_indices_register,
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
        using B = bytecode::TranslateSparseNullIndices;
        auto& bc = AddOpcode<B>(UnchangedRowCount{});
        bc.arg<B::col>() = col;
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

PERFETTO_NO_INLINE bytecode::reg::RwHandle<Span<uint32_t>>
QueryPlanBuilder::EnsureIndicesAreInSlab() {
  using SpanReg = bytecode::reg::RwHandle<Span<uint32_t>>;
  using SlabReg = bytecode::reg::RwHandle<Slab<uint32_t>>;

  if (PERFETTO_LIKELY(std::holds_alternative<SpanReg>(indices_reg_))) {
    return base::unchecked_get<SpanReg>(indices_reg_);
  }

  using RegRange = bytecode::reg::RwHandle<Range>;
  PERFETTO_DCHECK(std::holds_alternative<RegRange>(indices_reg_));
  auto range_reg = base::unchecked_get<RegRange>(indices_reg_);

  SlabReg slab_reg{plan_.params.register_count++};
  SpanReg span_reg{plan_.params.register_count++};
  {
    using B = bytecode::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }
  {
    using B = bytecode::Iota;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::source_register>() = range_reg;
    bc.arg<B::update_register>() = span_reg;
  }
  indices_reg_ = span_reg;
  return span_reg;
}

PERFETTO_NO_INLINE bytecode::Bytecode& QueryPlanBuilder::AddRawOpcode(
    uint32_t option,
    RowCountModifier rc,
    bytecode::Cost cost) {
  static constexpr uint32_t kFixedBytecodeCost = 5;
  switch (cost.index()) {
    case base::variant_index<bytecode::Cost, bytecode::FixedCost>(): {
      const auto& c = base::unchecked_get<bytecode::FixedCost>(cost);
      plan_.params.estimated_cost += c.cost;
      break;
    }
    case base::variant_index<bytecode::Cost, bytecode::LogPerRowCost>(): {
      const auto& c = base::unchecked_get<bytecode::LogPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * log2(plan_.params.estimated_row_count);
      break;
    }
    case base::variant_index<bytecode::Cost, bytecode::LinearPerRowCost>(): {
      const auto& c = base::unchecked_get<bytecode::LinearPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * plan_.params.estimated_row_count;
      break;
    }
    case base::variant_index<bytecode::Cost, bytecode::LogLinearPerRowCost>(): {
      const auto& c = base::unchecked_get<bytecode::LogLinearPerRowCost>(cost);
      plan_.params.estimated_cost +=
          plan_.params.estimated_row_count == 0
              ? kFixedBytecodeCost
              : c.cost * plan_.params.estimated_row_count *
                    log2(plan_.params.estimated_row_count);
      break;
    }
    case base::variant_index<bytecode::Cost,
                             bytecode::PostOperationLinearPerRowCost>():
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
      base::variant_index<bytecode::Cost,
                          bytecode::PostOperationLinearPerRowCost>()) {
    const auto& c =
        base::unchecked_get<bytecode::PostOperationLinearPerRowCost>(cost);
    plan_.params.estimated_cost += c.cost * plan_.params.estimated_cost;
  }
  plan_.bytecode.emplace_back();
  plan_.bytecode.back().option = option;
  return plan_.bytecode.back();
}

void QueryPlanBuilder::SetGuaranteedToBeEmpty() {
  bytecode::reg::RwHandle<Slab<uint32_t>> slab_reg{
      plan_.params.register_count++};
  bytecode::reg::RwHandle<Span<uint32_t>> span_reg{
      plan_.params.register_count++};
  {
    using B = bytecode::AllocateIndices;
    auto& bc = AddOpcode<B>(ZeroRowCount{});
    bc.arg<B::size>() = 0;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }
  indices_reg_ = span_reg;
}

bytecode::reg::ReadHandle<Slab<uint32_t>>
QueryPlanBuilder::PrefixPopcountRegisterFor(uint32_t col) {
  auto& reg = column_states_[col].prefix_popcount;
  if (!reg) {
    reg =
        bytecode::reg::RwHandle<Slab<uint32_t>>{plan_.params.register_count++};
    {
      using B = bytecode::PrefixPopcount;
      auto& bc = AddOpcode<B>(UnchangedRowCount{});
      bc.arg<B::col>() = col;
      bc.arg<B::dest_register>() = *reg;
    }
  }
  return *reg;
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

bytecode::reg::ReadHandle<CastFilterValueResult>
QueryPlanBuilder::CastFilterValue(FilterSpec& c,
                                  const StorageType& ct,
                                  NonNullOp op) {
  bytecode::reg::RwHandle<CastFilterValueResult> value_reg{
      plan_.params.register_count++};
  {
    using B = bytecode::CastFilterValueBase;
    auto& bc = AddOpcode<B>(bytecode::Index<bytecode::CastFilterValue>(ct),
                            UnchangedRowCount{});
    bc.arg<B::fval_handle>() = {plan_.params.filter_value_count};
    bc.arg<B::write_register>() = value_reg;
    bc.arg<B::op>() = op;
    c.value_index = plan_.params.filter_value_count++;
  }
  return value_reg;
}

bytecode::reg::RwHandle<Span<uint32_t>>
QueryPlanBuilder::GetOrCreateScratchSpanRegister(uint32_t size) {
  bytecode::reg::RwHandle<Slab<uint32_t>> scratch_slab;
  bytecode::reg::RwHandle<Span<uint32_t>> scratch_span;
  if (scratch_indices_) {
    PERFETTO_CHECK(size <= scratch_indices_->size);
    PERFETTO_CHECK(!scratch_indices_->in_use);
    scratch_slab = scratch_indices_->slab;
    scratch_span = scratch_indices_->span;
  } else {
    scratch_slab =
        bytecode::reg::RwHandle<Slab<uint32_t>>{plan_.params.register_count++};
    scratch_span =
        bytecode::reg::RwHandle<Span<uint32_t>>{plan_.params.register_count++};
  }
  {
    using B = bytecode::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = size;
    bc.arg<B::dest_slab_register>() = scratch_slab;
    bc.arg<B::dest_span_register>() = scratch_span;
  }
  scratch_indices_ = ScratchIndices{size, scratch_slab, scratch_span, true};
  return scratch_span;
}

void QueryPlanBuilder::MaybeReleaseScratchSpanRegister() {
  if (scratch_indices_) {
    scratch_indices_->in_use = false;
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

bytecode::reg::RwHandle<Slab<uint8_t>> QueryPlanBuilder::CopyToRowLayout(
    uint16_t row_stride,
    bytecode::reg::RwHandle<Span<uint32_t>> indices,
    bytecode::reg::ReadHandle<bytecode::reg::StringIdToRankMap> rank_map,
    const std::vector<RowLayoutParams>& row_layout_params) {
  uint32_t buffer_size = plan_.params.max_row_count * row_stride;
  bytecode::reg::RwHandle<Slab<uint8_t>> new_buffer_reg{
      plan_.params.register_count++};
  {
    using B = bytecode::AllocateRowLayoutBuffer;
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
                        : bytecode::reg::ReadHandle<Slab<uint32_t>>{
                              std::numeric_limits<uint32_t>::max()};
    {
      using B = bytecode::CopyToRowLayoutBase;
      auto index = bytecode::Index<bytecode::CopyToRowLayout>(
          col.storage.type(),
          NullabilityToSparseNullCollapsedNullability(nullability));
      auto& op = AddOpcode<B>(index, UnchangedRowCount{});
      op.arg<B::col>() = param.column;
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
    const bytecode::reg::ReadHandle<CastFilterValueResult>&
        filter_value_result_reg,
    const NonIdStorageType& non_id_storage_type) {
  const auto& col = GetColumn(c.col);
  PERFETTO_DCHECK(
      std::holds_alternative<bytecode::reg::RwHandle<Range>>(indices_reg_));
  PERFETTO_DCHECK(col.null_storage.nullability().Is<NonNull>());
  PERFETTO_DCHECK(c.op.Is<Eq>());

  using SpanReg = bytecode::reg::RwHandle<Span<uint32_t>>;
  using SlabReg = bytecode::reg::RwHandle<Slab<uint32_t>>;
  using RegRange = bytecode::reg::RwHandle<Range>;

  auto range_reg = base::unchecked_get<RegRange>(indices_reg_);
  SlabReg slab_reg{plan_.params.register_count++};
  SpanReg span_reg{plan_.params.register_count++};
  {
    using B = bytecode::AllocateIndices;
    auto& bc = AddOpcode<B>(UnchangedRowCount{});
    bc.arg<B::size>() = plan_.params.max_row_count;
    bc.arg<B::dest_slab_register>() = slab_reg;
    bc.arg<B::dest_span_register>() = span_reg;
  }

  {
    using B = bytecode::LinearFilterEqBase;
    B& bc = AddOpcode<B>(
        bytecode::Index<bytecode::LinearFilterEq>(non_id_storage_type),
        RowCountModifier{EqualityFilterRowCount{col.duplicate_state}});
    bc.arg<B::col>() = c.col;
    bc.arg<B::filter_value_reg>() = filter_value_result_reg;
    // For NonNull columns, popcount_register is not used by LinearFilterEq
    // logic. Pass a default-constructed handle.
    bc.arg<B::popcount_register>() =
        bytecode::reg::ReadHandle<Slab<uint32_t>>{};
    bc.arg<B::source_register>() = range_reg;
    bc.arg<B::update_register>() = span_reg;
  }
  indices_reg_ = span_reg;
}

template <typename T>
T& QueryPlanBuilder::AddOpcode(RowCountModifier rc) {
  return AddOpcode<T>(bytecode::Index<T>(), rc, T::kCost);
}

}  // namespace perfetto::trace_processor::dataframe::impl
