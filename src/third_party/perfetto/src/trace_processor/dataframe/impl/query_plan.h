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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_QUERY_PLAN_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_QUERY_PLAN_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/impl/bytecode_core.h"
#include "src/trace_processor/dataframe/impl/bytecode_registers.h"
#include "src/trace_processor/dataframe/impl/slab.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/types.h"

namespace perfetto::trace_processor::dataframe::impl {

// A QueryPlan encapsulates all the information needed to execute a query,
// including the bytecode instructions and interpreter configuration.
struct QueryPlan {
  // Contains various parameters required for execution of this query plan.
  struct ExecutionParams {
    // An estimate for the cost of executing the query plan.
    double estimated_cost = 0;

    // Register holding the final filtered indices.
    bytecode::reg::ReadHandle<Span<uint32_t>> output_register;

    // The maximum number of rows it's possible for this query plan to return.
    uint32_t max_row_count = 0;

    // The number of rows this query plan estimates it will return.
    uint32_t estimated_row_count = 0;

    // The number of registers used by this query plan.
    uint32_t register_count = 0;

    // Number of filter values used by this query.
    uint32_t filter_value_count = 0;

    // Number of output indices per row.
    uint32_t output_per_row = 0;
  };
  static_assert(std::is_trivially_copyable_v<ExecutionParams>);
  static_assert(std::is_trivially_destructible_v<ExecutionParams>);
  static_assert(sizeof(ExecutionParams) == 32);

  // Serializes the query plan to a Base64-encoded string.
  // This allows plans to be stored or transmitted between processes.
  std::string Serialize() const {
    size_t size = sizeof(params) + sizeof(size_t) +
                  (bytecode.size() * sizeof(bytecode::Bytecode)) +
                  sizeof(size_t) +
                  (col_to_output_offset.size() * sizeof(uint32_t));
    std::string res(size, '\0');
    char* p = res.data();
    {
      memcpy(p, &params, sizeof(params));
      p += sizeof(params);
    }
    {
      size_t bytecode_size = bytecode.size();
      memcpy(p, &bytecode_size, sizeof(bytecode_size));
      p += sizeof(bytecode_size);
    }
    {
      memcpy(p, bytecode.data(), bytecode.size() * sizeof(bytecode::Bytecode));
      p += bytecode.size() * sizeof(bytecode::Bytecode);
    }
    {
      size_t columns_size = col_to_output_offset.size();
      memcpy(p, &columns_size, sizeof(columns_size));
      p += sizeof(columns_size);
    }
    {
      size_t columns_size = col_to_output_offset.size();
      memcpy(p, col_to_output_offset.data(), columns_size * sizeof(uint32_t));
      p += columns_size * sizeof(uint32_t);
    }
    PERFETTO_CHECK(p == res.data() + res.size());
    return base::Base64Encode(base::StringView(res));
  }

  // Deserializes a query plan from a Base64-encoded string.
  // Returns the reconstructed QueryPlan.
  static QueryPlan Deserialize(std::string_view serialized) {
    QueryPlan res;
    std::optional<std::string> raw_data = base::Base64Decode(
        base::StringView(serialized.data(), serialized.size()));
    PERFETTO_CHECK(raw_data);
    const char* p = raw_data->data();
    size_t bytecode_size;
    size_t columns_size;
    {
      memcpy(&res.params, p, sizeof(res.params));
      p += sizeof(res.params);
    }
    {
      memcpy(&bytecode_size, p, sizeof(bytecode_size));
      p += sizeof(bytecode_size);
    }
    {
      for (uint32_t i = 0; i < bytecode_size; ++i) {
        res.bytecode.emplace_back();
      }
      memcpy(res.bytecode.data(), p,
             bytecode_size * sizeof(bytecode::Bytecode));
      p += bytecode_size * sizeof(bytecode::Bytecode);
    }
    {
      memcpy(&columns_size, p, sizeof(columns_size));
      p += sizeof(columns_size);
    }
    {
      for (uint32_t i = 0; i < columns_size; ++i) {
        res.col_to_output_offset.emplace_back();
      }
      memcpy(res.col_to_output_offset.data(), p,
             columns_size * sizeof(uint32_t));
      p += columns_size * sizeof(uint32_t);
    }
    PERFETTO_CHECK(p == raw_data->data() + raw_data->size());
    return res;
  }

  ExecutionParams params;
  bytecode::BytecodeVector bytecode;
  base::SmallVector<uint32_t, 24> col_to_output_offset;
};

// Builder class for creating query plans.
//
// QueryPlans contain the bytecode instructions and interpreter configuration
// needed to execute a query.
class QueryPlanBuilder {
 public:
  static base::StatusOr<QueryPlan> Build(
      uint32_t row_count,
      const std::vector<std::shared_ptr<Column>>& columns,
      const std::vector<Index>& indexes,
      std::vector<FilterSpec>& specs,
      const std::vector<DistinctSpec>& distinct,
      const std::vector<SortSpec>& sort_specs,
      const LimitSpec& limit_spec,
      uint64_t cols_used) {
    QueryPlanBuilder builder(row_count, columns, indexes);
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

 private:
  // Represents register types for holding indices.
  using IndicesReg = std::variant<bytecode::reg::RwHandle<Range>,
                                  bytecode::reg::RwHandle<Span<uint32_t>>>;

  // Indicates that the bytecode does not change the estimated or maximum number
  // of rows.
  struct UnchangedRowCount {};

  // Indicates that the bytecode is a non-equality filter.
  struct NonEqualityFilterRowCount {};

  // Indicates that the bytecode is a equality filter with given duplicate
  // state.
  struct EqualityFilterRowCount {
    DuplicateState duplicate_state;
  };

  // Indicates that the bytecode produces *exactly* one row and the estimated
  // and maximum should be set to 1.
  struct OneRowCount {};

  // Indicates that the bytecode produces *exactly* zero rows and the estimated
  // and maximum should be set to 0.
  struct ZeroRowCount {};

  // Indicates that the bytecode produces `limit` rows starting at `offset`.
  struct LimitOffsetRowCount {
    uint32_t limit;
    uint32_t offset;
  };
  using RowCountModifier = std::variant<UnchangedRowCount,
                                        NonEqualityFilterRowCount,
                                        EqualityFilterRowCount,
                                        OneRowCount,
                                        ZeroRowCount,
                                        LimitOffsetRowCount>;

  // State information for a column during query planning.
  struct ColumnState {
    std::optional<bytecode::reg::RwHandle<Slab<uint32_t>>> prefix_popcount;
  };

  // Constructs a builder for the given number of rows and columns.
  QueryPlanBuilder(uint32_t row_count,
                   const std::vector<std::shared_ptr<Column>>& columns,
                   const std::vector<Index>& indexes);

  // Adds filter operations to the query plan based on filter specifications.
  // Optimizes the order of filters for efficiency.
  base::Status Filter(std::vector<FilterSpec>& specs);

  // Adds distinct operations to the query plan based on distinct
  // specifications. Distinct are applied after filters, in reverse order of
  // specification.
  void Distinct(const std::vector<DistinctSpec>& distinct_specs);

  // Adds min/max operations to the query plan given a single column which
  // should be sorted on.
  void MinMax(const SortSpec& spec);

  // Adds sort operations to the query plan based on sort specifications.
  // Sorts are applied after filters and disinct.
  void Sort(const std::vector<SortSpec>& sort_specs);

  // Configures output handling for the filtered rows.
  // |cols_used_bitmap| is a bitmap with bits set for columns that will be
  // accessed.
  void Output(const LimitSpec&, uint64_t cols_used_bitmap);

  // Finalizes and returns the built query plan.
  QueryPlan Build() &&;

  // Processes non-string filter constraints.
  void NonStringConstraint(
      const FilterSpec& c,
      const NonStringType& type,
      const NonStringOp& op,
      const bytecode::reg::ReadHandle<CastFilterValueResult>& result);

  // Processes string filter constraints.
  base::Status StringConstraint(
      const FilterSpec& c,
      const StringOp& op,
      const bytecode::reg::ReadHandle<CastFilterValueResult>& result);

  // Processes null filter constraints.
  void NullConstraint(const NullOp&, FilterSpec&);

  // Processes constraints which can be handled with an index.
  void IndexConstraints(std::vector<FilterSpec>&,
                        std::vector<uint8_t>& specs_handled,
                        uint32_t,
                        const std::vector<uint32_t>&);

  // Attempts to apply optimized filtering on sorted data.
  // Returns true if the optimization was applied.
  bool TrySortedConstraint(FilterSpec& fs,
                           const StorageType& ct,
                           const NonNullOp& op);

  // Given a list of indices, prunes any indices that point to null rows
  // in the given column. The indices are pruned in-place, and the
  // `indices_register` is updated to contain only non-null indices.
  void PruneNullIndices(uint32_t col,
                        bytecode::reg::RwHandle<Span<uint32_t>> indices);

  // Given a list of table indices pointing to *only* non-null rows,
  // if necessary, translates them to the storage indices for the given column.
  // If no translation is needed, the indices are returned as-is.
  // If translation *is* needed, the value of `in_place` determines
  // whether the translation is done in-place or whether the data is stored
  // in the scratch register.
  //
  // Returns a register handle to the translated indices (either
  // `indices_register` or the scratch register).
  bytecode::reg::RwHandle<Span<uint32_t>> TranslateNonNullIndices(
      uint32_t col,
      bytecode::reg::RwHandle<Span<uint32_t>> indices_register,
      bool in_place);

  // Ensures indices are stored in a Slab, converting from Range if necessary.
  PERFETTO_NO_INLINE bytecode::reg::RwHandle<Span<uint32_t>>
  EnsureIndicesAreInSlab();

  // Adds a new bytecode instruction of type T to the plan.
  template <typename T>
  T& AddOpcode(RowCountModifier rc);

  // Adds a new bytecode instruction of type T with the given option value.
  template <typename T>
  T& AddOpcode(uint32_t option, RowCountModifier rc) {
    return static_cast<T&>(AddRawOpcode(option, rc, T::kCost));
  }

  // Adds a new bytecode instruction of type T with the given option value.
  template <typename T>
  T& AddOpcode(uint32_t option, RowCountModifier rc, bytecode::Cost cost) {
    return static_cast<T&>(AddRawOpcode(option, rc, cost));
  }

  PERFETTO_NO_INLINE bytecode::Bytecode& AddRawOpcode(uint32_t option,
                                                      RowCountModifier rc,
                                                      bytecode::Cost cost);

  // Sets the result to an empty set. Use when a filter guarantees no matches.
  void SetGuaranteedToBeEmpty();

  // Returns the prefix popcount register for the given column.
  bytecode::reg::ReadHandle<Slab<uint32_t>> PrefixPopcountRegisterFor(
      uint32_t col);

  bytecode::reg::ReadHandle<CastFilterValueResult>
  CastFilterValue(FilterSpec& c, const StorageType& ct, NonNullOp non_null_op);

  bytecode::reg::RwHandle<Span<uint32_t>> GetOrCreateScratchSpanRegister(
      uint32_t size);

  // Parameters for conversion to row layout.
  struct RowLayoutParams {
    // The column to be copied.
    uint32_t column;

    // Whether, instead of copying the string column, we should replace it
    // with a rank of the string.
    bool replace_string_with_rank = false;

    // Whether the bits when copied should be inverted.
    bool invert_copied_bits = false;
  };
  uint16_t CalculateRowLayoutStride(
      const std::vector<RowLayoutParams>& row_layout_params);

  bytecode::reg::RwHandle<Slab<uint8_t>> CopyToRowLayout(
      uint16_t row_stride,
      bytecode::reg::RwHandle<Span<uint32_t>> indices,
      bytecode::reg::ReadHandle<bytecode::reg::StringIdToRankMap> rank_map,
      const std::vector<RowLayoutParams>& row_layout_params);

  void MaybeReleaseScratchSpanRegister();

  void AddLinearFilterEqBytecode(
      const FilterSpec&,
      const bytecode::reg::ReadHandle<CastFilterValueResult>&,
      const NonIdStorageType&);

  bool CanUseMinMaxOptimization(const std::vector<SortSpec>&, const LimitSpec&);

  const Column& GetColumn(uint32_t idx) { return *columns_[idx]; }

  // Reference to the columns being queried.
  const std::vector<std::shared_ptr<Column>>& columns_;

  // Reference to the indexes available.
  const std::vector<Index>& indexes_;

  // The query plan being built.
  QueryPlan plan_;

  // State information for each column during planning.
  std::vector<ColumnState> column_states_;

  // Current register holding the set of matching indices.
  IndicesReg indices_reg_;

  // If scratch indices are needed, this holds the size and handles to
  // the scratch indices in both Span and Slab forms.
  struct ScratchIndices {
    uint32_t size;
    bytecode::reg::RwHandle<Slab<uint32_t>> slab;
    bytecode::reg::RwHandle<Span<uint32_t>> span;
    bool in_use = false;
  };
  std::optional<ScratchIndices> scratch_indices_;
};

}  // namespace perfetto::trace_processor::dataframe::impl

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_QUERY_PLAN_H_
