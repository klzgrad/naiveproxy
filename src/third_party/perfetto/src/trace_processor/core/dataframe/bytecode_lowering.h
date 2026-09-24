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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_BYTECODE_LOWERING_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_BYTECODE_LOWERING_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/dataframe/dataframe_register_cache.h"
#include "src/trace_processor/core/dataframe/logical_plan.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
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

namespace perfetto::trace_processor::core::dataframe {

// Turns a LogicalPlan into bytecode for the interpreter.
//
// This class owns everything the logical plan deliberately says nothing about:
// register allocation and caching, scratch lifetimes, materializing a range
// into an index list, pruning nulls, translating sparse-null indices, row
// layout buffers, and the cost estimate.
//
// It makes no access-path decisions of its own. Each operation arrives with
// the strategy the planner chose and the row counts it expects, and lowering
// expands that into however many bytecodes the strategy needs.
class BytecodeLowering {
 public:
  static QueryPlanImpl Lower(
      const LogicalPlan& plan,
      const std::vector<std::shared_ptr<Column>>& columns,
      const std::vector<Index>& indexes);

 private:
  // Register holding the set of matching rows, either as a contiguous range
  // or as a materialized list of indices.
  using IndicesReg = std::variant<interpreter::RwHandle<Range>,
                                  interpreter::RwHandle<Span<uint32_t>>>;

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

  // Alias for scratch register type.
  using Scratch = interpreter::BytecodeBuilder::ScratchRegisters;

  // Whether the span being pruned holds the query's result rows, or only a
  // temporary copy of them.
  enum class NullPruneScope { kResultRows, kScratchOnly };

  BytecodeLowering(const std::vector<std::shared_ptr<Column>>& columns,
                   const std::vector<Index>& indexes);

  // === One method per logical operation ===

  void LowerOperation(const logical::Operation& op);
  void LowerScan(const logical::Scan&);
  void LowerFilter(const logical::Filter&);
  void LowerIndexFilter(const logical::IndexFilter&);
  void LowerEmpty();
  void LowerDistinct(const logical::Distinct&);
  void LowerReverse();
  void LowerSort(const logical::Sort&);
  void LowerMinMax(const logical::MinMax&);
  void LowerLimit(const logical::Limit&);
  void LowerOutput(const logical::Output&);

  // === One method per filter strategy ===

  void LowerBinarySearchFilter(const logical::Filter&);
  void LowerSetIdSortedFilter(const logical::Filter&);
  void LowerSmallValueFilter(const logical::Filter&);
  void LowerRangeScanFilter(const logical::Filter&);
  void LowerIndexListScanFilter(const logical::Filter&);

  // === Filter values ===

  interpreter::ReadHandle<interpreter::CastFilterValueResult>
  EmitCastFilterValue(uint32_t value_index,
                      const StorageType& type,
                      const NonNullOp& op);

  interpreter::RwHandle<std::unique_ptr<interpreter::CastFilterValueListResult>>
  EmitCastFilterValueList(uint32_t value_index, const StorageType& type);

  // === Row counts ===
  //
  // Buffer sizes and the cost of each bytecode are derived from how many rows
  // the plan has at that point. The numbers themselves are decided by the
  // planner and carried on the operation; lowering only tracks them.

  void SetRows(const logical::RowEstimate& rows) {
    plan_.params.max_row_count = rows.max;
    plan_.params.estimated_row_count = rows.estimated;
  }
  void SetEstimatedRows(uint32_t estimated) {
    plan_.params.estimated_row_count = estimated;
  }

  // === Emission plumbing ===

  // Given a list of indices, prunes any indices that point to null rows
  // in the given column. The indices are pruned in-place.
  void PruneNullIndices(uint32_t col,
                        interpreter::RwHandle<Span<uint32_t>> indices,
                        NullPruneScope scope);

  // Given a list of table indices pointing to *only* non-null rows,
  // if necessary, translates them to the storage indices for the given column.
  // If no translation is needed, the indices are returned as-is.
  // If translation *is* needed, the value of `in_place` determines
  // whether the translation is done in-place or whether the data is stored
  // in the scratch register.
  interpreter::RwHandle<Span<uint32_t>> TranslateNonNullIndices(
      uint32_t col,
      interpreter::RwHandle<Span<uint32_t>> indices_register,
      bool in_place);

  // Ensures indices are stored in a Slab, converting from Range if necessary.
  PERFETTO_NO_INLINE interpreter::RwHandle<Span<uint32_t>>
  EnsureIndicesAreInSlab();

  // Adds a new bytecode instruction of type T to the plan.
  template <typename T>
  T& AddOpcode();

  // Adds a new bytecode instruction of type T with the given option value.
  template <typename T>
  T& AddOpcode(uint32_t option) {
    return static_cast<T&>(AddRawOpcode(option, T::kCost));
  }

  // Adds a new bytecode instruction of type T with the given option value.
  template <typename T>
  T& AddOpcode(uint32_t option, interpreter::Cost cost) {
    return static_cast<T&>(AddRawOpcode(option, cost));
  }

  PERFETTO_NO_INLINE interpreter::Bytecode& AddRawOpcode(
      uint32_t option,
      interpreter::Cost cost);

  // Allocates a register for column data pointer and adds RegisterInit entry.
  interpreter::RwHandle<interpreter::StoragePtr> StorageRegisterFor(
      uint32_t col,
      StorageType storage_type);

  // Returns the index register for the given position.
  interpreter::RwHandle<Span<uint32_t>> IndexRegisterFor(uint32_t pos);

  // Returns the null bitvector register for the given column.
  // For NonNull columns, returns an empty handle.
  interpreter::ReadHandle<interpreter::NullBitvector> NullBitvectorRegisterFor(
      uint32_t col);

  // Returns the null bitvector register for the given column, ensuring a
  // PrefixPopcount bytecode has been emitted for SparseNull columns.
  // No-op for non-SparseNull columns or if already emitted.
  interpreter::ReadHandle<interpreter::NullBitvector> EnsurePrefixPopcountFor(
      uint32_t col);

  // Returns the SmallValueEq bitvector register for the given column.
  interpreter::ReadHandle<const BitVector*> SmallValueEqBvRegisterFor(
      uint32_t col);

  // Returns the SmallValueEq popcount register for the given column.
  interpreter::ReadHandle<Span<const uint32_t>> SmallValueEqPopcountRegisterFor(
      uint32_t col);

  interpreter::RwHandle<Span<uint32_t>> GetOrCreateScratchSpanRegister(
      uint32_t size);

  void MaybeReleaseScratchSpanRegister();

  uint16_t CalculateRowLayoutStride(
      const std::vector<RowLayoutParams>& row_layout_params);

  interpreter::RwHandle<Slab<uint8_t>> CopyToRowLayout(
      uint16_t row_stride,
      interpreter::RwHandle<Span<uint32_t>> indices,
      interpreter::ReadHandle<interpreter::StringIdToRankMap> rank_map,
      const std::vector<RowLayoutParams>& row_layout_params);

  const Column& GetColumn(uint32_t idx) { return *columns_[idx]; }

  // Reference to the columns being queried.
  const std::vector<std::shared_ptr<Column>>& columns_;

  // Reference to the indexes available.
  const std::vector<Index>& indexes_;

  // The query plan being built.
  QueryPlanImpl plan_;

  // Low-level bytecode builder for register allocation, bytecode storage,
  // and scratch management.
  interpreter::BytecodeBuilder builder_;

  // Register cache for caching column/index registers.
  DataframeRegisterCache cache_;

  // Current register holding the set of matching indices.
  IndicesReg indices_reg_;

  // Tracks which columns have had PrefixPopcount bytecode emitted.
  base::FlatHashMap<uint32_t, bool> prefix_popcount_emitted_;

  // Last scratch registers returned by GetOrCreateScratchSpanRegister.
  std::optional<Scratch> scratch_;
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_BYTECODE_LOWERING_H_
