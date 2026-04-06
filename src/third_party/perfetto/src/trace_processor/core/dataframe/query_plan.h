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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/dataframe/dataframe_register_cache.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"
#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core::dataframe {

class Dataframe;

// Specification for initializing a register before bytecode execution.
// The plan contains abstract references (column indices, index IDs), and
// the cursor converts these to concrete pointers based on the kind.
struct RegisterInit {
  struct NullBitvector {};
  struct IndexVector {};
  struct SmallValueEqBitvector {};
  struct SmallValueEqPopcount {};

  using Type = TypeSet<Id,
                       Uint32,
                       Int32,
                       Int64,
                       Double,
                       String,
                       NullBitvector,
                       IndexVector,
                       SmallValueEqBitvector,
                       SmallValueEqPopcount>;
  uint32_t dest_register = 0;
  Type kind{Id{}};
  uint16_t source_index = 0;  // col_index or index_id depending on kind
  uint16_t pad_ = 0;          // Explicit trailing padding
};
static_assert(std::is_trivially_copyable_v<RegisterInit>);

// Result of applying filters via the static Filter() method.
// Contains both the filtered indices register and the RegisterInit specs
// needed to initialize storage registers before bytecode execution.
struct FilterResult {
  std::variant<interpreter::RwHandle<Range>,
               interpreter::RwHandle<Span<uint32_t>>>
      indices;
  base::SmallVector<RegisterInit, 16> register_inits;
};

// A QueryPlan encapsulates all the information needed to execute a query,
// including the bytecode instructions and interpreter configuration.
struct QueryPlanImpl {
  // Contains various parameters required for execution of this query plan.
  struct ExecutionParams {
    // An estimate for the cost of executing the query plan.
    double estimated_cost = 0;

    // Register holding the final filtered indices.
    interpreter::ReadHandle<Span<uint32_t>> output_register;

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
    size_t size =
        sizeof(params) + sizeof(size_t) +
        (bytecode.size() * sizeof(interpreter::Bytecode)) + sizeof(size_t) +
        (col_to_output_offset.size() * sizeof(uint32_t)) + sizeof(size_t) +
        (register_inits.size() * sizeof(RegisterInit));
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
      memcpy(p, bytecode.data(),
             bytecode.size() * sizeof(interpreter::Bytecode));
      p += bytecode.size() * sizeof(interpreter::Bytecode);
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
    {
      size_t register_inits_size = register_inits.size();
      memcpy(p, &register_inits_size, sizeof(register_inits_size));
      p += sizeof(register_inits_size);
    }
    {
      memcpy(p, register_inits.data(),
             register_inits.size() * sizeof(RegisterInit));
      p += register_inits.size() * sizeof(RegisterInit);
    }
    PERFETTO_CHECK(p == res.data() + res.size());
    return base::Base64Encode(base::StringView(res));
  }

  // Deserializes a query plan from a Base64-encoded string.
  // Returns the reconstructed QueryPlan.
  static QueryPlanImpl Deserialize(std::string_view serialized) {
    QueryPlanImpl res;
    std::optional<std::string> raw_data = base::Base64Decode(
        base::StringView(serialized.data(), serialized.size()));
    PERFETTO_CHECK(raw_data);
    const char* p = raw_data->data();
    size_t bytecode_size;
    size_t columns_size;
    size_t register_inits_size;
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
             bytecode_size * sizeof(interpreter::Bytecode));
      p += bytecode_size * sizeof(interpreter::Bytecode);
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
    {
      memcpy(&register_inits_size, p, sizeof(register_inits_size));
      p += sizeof(register_inits_size);
    }
    {
      for (size_t i = 0; i < register_inits_size; ++i) {
        res.register_inits.emplace_back();
      }
      memcpy(res.register_inits.data(), p,
             register_inits_size * sizeof(RegisterInit));
      p += register_inits_size * sizeof(RegisterInit);
    }
    PERFETTO_CHECK(p == raw_data->data() + raw_data->size());
    return res;
  }

  // Converts a RegisterInit spec to the actual register value for execution.
  // Used by Cursor and TreeTransformer to initialize registers before
  // bytecode execution.
  static interpreter::RegValue GetRegisterInitValue(
      const RegisterInit& init,
      const Column* const* columns,
      const Index* indexes);

  // Convenience overload that extracts pointers from a Dataframe.
  static interpreter::RegValue GetRegisterInitValue(const RegisterInit& init,
                                                    const Dataframe& df);

  ExecutionParams params;
  interpreter::BytecodeVector bytecode;
  base::SmallVector<uint32_t, 24> col_to_output_offset;

  // Register initialization specifications.
  // The cursor processes these to set up registers before bytecode execution.
  base::SmallVector<RegisterInit, 16> register_inits;
};

// Builder class for creating query plans.
//
// QueryPlans contain the bytecode instructions and interpreter configuration
// needed to execute a query.
class QueryPlanBuilder {
 public:
  // Represents register types for holding indices.
  using IndicesReg = std::variant<interpreter::RwHandle<Range>,
                                  interpreter::RwHandle<Span<uint32_t>>>;

  static base::StatusOr<QueryPlanImpl> Build(
      uint32_t row_count,
      const std::vector<std::shared_ptr<Column>>& columns,
      const std::vector<Index>& indexes,
      std::vector<FilterSpec>& specs,
      const std::vector<DistinctSpec>& distinct,
      const std::vector<SortSpec>& sort_specs,
      const LimitSpec& limit_spec,
      uint64_t cols_used);

  // Applies filter constraints to an existing BytecodeBuilder.
  // This is useful for callers (like TreeTransformer) that want to reuse
  // the filtering logic without building a full query plan.
  //
  // Parameters:
  //   builder: The BytecodeBuilder to emit bytecode into
  //   cache: Register cache for column/index register caching
  //   input_indices: Input indices to filter
  //   df: The dataframe to filter
  //   specs: Filter specifications (may be reordered)
  //
  // Returns a FilterResult containing:
  //   - The filtered indices register
  //   - RegisterInit specs needed to initialize storage registers
  // Cost tracking is done internally and discarded at end of call.
  static base::StatusOr<FilterResult> Filter(
      interpreter::BytecodeBuilder& builder,
      DataframeRegisterCache& cache,
      IndicesReg input_indices,
      const Dataframe& df,
      std::vector<FilterSpec>& specs);

 private:
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

  // Constructs a builder for the given indices and columns.
  // cache is used for caching column/index registers.
  QueryPlanBuilder(interpreter::BytecodeBuilder& builder,
                   DataframeRegisterCache& cache,
                   IndicesReg indices,
                   uint32_t row_count,
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
  QueryPlanImpl Build() &&;

  // Processes non-string filter constraints.
  void NonStringConstraint(
      const FilterSpec& c,
      const interpreter::NonStringType& type,
      const interpreter::NonStringOp& op,
      const interpreter::ReadHandle<interpreter::CastFilterValueResult>&
          result);

  // Processes string filter constraints.
  base::Status StringConstraint(
      const FilterSpec& c,
      const interpreter::StringOp& op,
      const interpreter::ReadHandle<interpreter::CastFilterValueResult>&
          result);

  // Processes null filter constraints.
  void NullConstraint(const interpreter::NullOp&, FilterSpec&);

  // Processes constraints which can be handled with an index.
  void IndexConstraints(std::vector<FilterSpec>&,
                        std::vector<uint8_t>& specs_handled,
                        uint32_t,
                        const std::vector<uint32_t>&);

  // Attempts to apply optimized filtering on sorted data.
  // Returns true if the optimization was applied.
  bool TrySortedConstraint(FilterSpec& fs,
                           const StorageType& ct,
                           const interpreter::NonNullOp& op);

  // Given a list of indices, prunes any indices that point to null rows
  // in the given column. The indices are pruned in-place, and the
  // `indices_register` is updated to contain only non-null indices.
  void PruneNullIndices(uint32_t col,
                        interpreter::RwHandle<Span<uint32_t>> indices);

  // Given a list of table indices pointing to *only* non-null rows,
  // if necessary, translates them to the storage indices for the given column.
  // If no translation is needed, the indices are returned as-is.
  // If translation *is* needed, the value of `in_place` determines
  // whether the translation is done in-place or whether the data is stored
  // in the scratch register.
  //
  // Returns a register handle to the translated indices (either
  // `indices_register` or the scratch register).
  interpreter::RwHandle<Span<uint32_t>> TranslateNonNullIndices(
      uint32_t col,
      interpreter::RwHandle<Span<uint32_t>> indices_register,
      bool in_place);

  // Ensures indices are stored in a Slab, converting from Range if necessary.
  PERFETTO_NO_INLINE interpreter::RwHandle<Span<uint32_t>>
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
  T& AddOpcode(uint32_t option, RowCountModifier rc, interpreter::Cost cost) {
    return static_cast<T&>(AddRawOpcode(option, rc, cost));
  }

  PERFETTO_NO_INLINE interpreter::Bytecode&
  AddRawOpcode(uint32_t option, RowCountModifier rc, interpreter::Cost cost);

  // Sets the result to an empty set. Use when a filter guarantees no matches.
  void SetGuaranteedToBeEmpty();

  // Returns the prefix popcount register for the given column.
  interpreter::ReadHandle<Slab<uint32_t>> PrefixPopcountRegisterFor(
      uint32_t col);

  // Allocates a register for column data pointer and adds RegisterInit entry.
  // Returns a HandleBase that can be assigned to typed data_register fields.
  interpreter::RwHandle<interpreter::StoragePtr> StorageRegisterFor(
      uint32_t col,
      StorageType storage_type);

  // Returns the index register for the given position.
  interpreter::RwHandle<Span<uint32_t>> IndexRegisterFor(uint32_t pos);

  // Returns the null bitvector register for the given column.
  interpreter::ReadHandle<const BitVector*> NullBitvectorRegisterFor(
      uint32_t col);

  // Returns the SmallValueEq bitvector register for the given column.
  interpreter::ReadHandle<const BitVector*> SmallValueEqBvRegisterFor(
      uint32_t col);

  // Returns the SmallValueEq popcount register for the given column.
  interpreter::ReadHandle<Span<const uint32_t>> SmallValueEqPopcountRegisterFor(
      uint32_t col);

  interpreter::ReadHandle<interpreter::CastFilterValueResult> CastFilterValue(
      FilterSpec& c,
      const StorageType& ct,
      interpreter::NonNullOp non_null_op);

  interpreter::RwHandle<Span<uint32_t>> GetOrCreateScratchSpanRegister(
      uint32_t size);

  // Alias for scratch register type.
  using Scratch = interpreter::BytecodeBuilder::ScratchRegisters;

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

  interpreter::RwHandle<Slab<uint8_t>> CopyToRowLayout(
      uint16_t row_stride,
      interpreter::RwHandle<Span<uint32_t>> indices,
      interpreter::ReadHandle<interpreter::StringIdToRankMap> rank_map,
      const std::vector<RowLayoutParams>& row_layout_params);

  void MaybeReleaseScratchSpanRegister();

  void AddLinearFilterEqBytecode(
      const FilterSpec&,
      const interpreter::ReadHandle<interpreter::CastFilterValueResult>&,
      const interpreter::NonIdStorageType&);

  bool CanUseMinMaxOptimization(const std::vector<SortSpec>&, const LimitSpec&);

  const Column& GetColumn(uint32_t idx) { return *columns_[idx]; }

  // Reference to the columns being queried.
  const std::vector<std::shared_ptr<Column>>& columns_;

  // Reference to the indexes available.
  const std::vector<Index>& indexes_;

  // The query plan being built.
  QueryPlanImpl plan_;

  // Current register holding the set of matching indices.
  IndicesReg indices_reg_;

  // Low-level bytecode builder for register allocation, bytecode storage,
  // and scratch management.
  interpreter::BytecodeBuilder& builder_;

  // Register cache for caching column/index registers across Filter() calls.
  DataframeRegisterCache& cache_;

  // Last scratch registers returned by GetOrCreateScratchSpanRegister.
  std::optional<Scratch> scratch_;
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_QUERY_PLAN_H_
