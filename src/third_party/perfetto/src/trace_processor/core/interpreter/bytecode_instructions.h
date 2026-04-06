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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTIONS_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTIONS_H_

#include <cstdint>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/null_types.h"
#include "src/trace_processor/core/common/op_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_instruction_macros.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::interpreter {

// Bytecode instructions - each represents a specific operation for query
// execution.

// Initializes a range register with a given size.
struct InitRange : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(uint32_t,
                                     size,
                                     WriteHandle<Range>,
                                     dest_register);
};

// Allocates a slab of indices.
struct AllocateIndices : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{30};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(uint32_t,
                                     size,
                                     WriteHandle<Slab<uint32_t>>,
                                     dest_slab_register,
                                     WriteHandle<Span<uint32_t>>,
                                     dest_span_register);
};

// Fills a memory region with sequential integers (0...n-1).
struct Iota : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(ReadHandle<Range>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};

// Base class for casting filter value operations.
struct CastFilterValueBase : TemplatedBytecode1<StorageType> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(FilterValueHandle,
                                     fval_handle,
                                     WriteHandle<CastFilterValueResult>,
                                     write_register,
                                     NonNullOp,
                                     op);
};
template <typename T>
struct CastFilterValue : CastFilterValueBase {
  static_assert(TS1::Contains<T>());
};

// Casts a list of filter values.
struct CastFilterValueListBase : TemplatedBytecode1<StorageType> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{1000};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(FilterValueHandle,
                                     fval_handle,
                                     WriteHandle<CastFilterValueListResult>,
                                     write_register,
                                     NonNullOp,
                                     op);
};
template <typename T>
struct CastFilterValueList : CastFilterValueListBase {
  static_assert(TS1::Contains<T>());
};

// Base for operations on sorted data.
struct SortedFilterBase
    : TemplatedBytecode2<StorageType, EqualRangeLowerBoundUpperBound> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost EstimateCost(StorageType type) {
    if (type.Is<Id>()) {
      return FixedCost{20};
    }
    return LogPerRowCost{10};
  }
  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueResult>,
                                     val_register,
                                     RwHandle<Range>,
                                     update_register,
                                     BoundModifier,
                                     write_result_to);
};
template <typename T, typename RangeOp>
struct SortedFilter : SortedFilterBase {
  static_assert(TS1::Contains<T>());
  static_assert(TS2::Contains<RangeOp>());
};

// Specialized filter for Uint32 columns with SetIdSorted state and equality
// operation.
struct Uint32SetIdSortedEq : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{100};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueResult>,
                                     val_register,
                                     RwHandle<Range>,
                                     update_register);
};

// Equality filter for columns with a specialized storage containing
// SmallValueEq.
struct SpecializedStorageSmallValueEq : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<const BitVector*>,
                                     small_value_bv_register,
                                     ReadHandle<Span<const uint32_t>>,
                                     small_value_popcount_register,
                                     ReadHandle<CastFilterValueResult>,
                                     val_register,
                                     RwHandle<Range>,
                                     update_register);
};

// Filter operations on non-string columns.
struct NonStringFilterBase : TemplatedBytecode2<NonStringType, NonStringOp> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{5};
  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueResult>,
                                     val_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};
template <typename T, typename Op>
struct NonStringFilter : NonStringFilterBase {
  static_assert(TS1::Contains<T>());
  static_assert(TS2::Contains<Op>());
};

// Filter operations on string columns.
struct StringFilterBase : TemplatedBytecode1<StringOp> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{15};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueResult>,
                                     val_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};
template <typename Op>
struct StringFilter : StringFilterBase {
  static_assert(TS1::Contains<Op>());
};

// Copies data with a given stride.
struct StrideCopy : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{15};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register,
                                     uint32_t,
                                     stride);
};

// Computes the prefix popcount for the null overlay for a given column.
//
// Popcount means to compute the number of set bits in a word of a BitVector. So
// prefix popcount is a along with a prefix sum over the counts vector.
//
// Note: if `dest_register` already has a value, we'll assume that this bytecode
// has already been executed and skip the computation. This allows for caching
// the result of this bytecode across executions of the interpreter.
struct PrefixPopcount : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{20};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     WriteHandle<Slab<uint32_t>>,
                                     dest_register);
};

// Translates a set of indices into a sparse null overlay into indices into
// the underlying storage.
//
// Note that every index in the `source_register` is assumed to be a non-null
// index (i.e. the position of a set bit in the null overlay). To accomplish
// this, make sure to first apply a NullFilter with the IsNotNull operator.
//
// `popcount_register` should point to a register containing the result of the
// PrefixPopcount instruction. This is used to significantly accelerate the
// translation.
struct TranslateSparseNullIndices : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     ReadHandle<Slab<uint32_t>>,
                                     popcount_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};

// Base class for null filter operations.
struct NullFilterBase : TemplatedBytecode1<NullOp> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};

// Template specialization for a given null operator.
template <typename NullOp>
struct NullFilter : NullFilterBase {
  static_assert(TS1::Contains<NullOp>());
};

// A complex opcode which does the following:
// 1. Iterates over indices in `update_register` starting at offset 0 each
//    incrementing by `stride` each iteration.
// 2. For each such index, if it's non-null, translates it using the sparse null
//    translation logic (see TranslateSparseNullIndices) for the sparse null
//    overlay of `col`
// 3. If the index is null, replace it with UINT32_MAX (representing NULL).
// 4. Copies the result of step 2/3 into position `offset` of the current "row"
//    of indices in `update_register`.
//
// Necessary for the case where we are trying to build the output indices span
// with all the indices into the storage for each relevant column.
struct StrideTranslateAndCopySparseNullIndices : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_5(ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     ReadHandle<Slab<uint32_t>>,
                                     popcount_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register,
                                     uint32_t,
                                     offset,
                                     uint32_t,
                                     stride);
};

// A complex opcode which does the following:
// 1. Iterates over indices in `read_register` starting at offset 0 each
//    incrementing by `stride` each iteration.
// 2. For each such index, if it's non-null, just use it as is in step 4.
// 3. If the index is null, replace it with UINT32_MAX (representing NULL).
// 4. Copies the result of step 2/3 into position `offset` of the current "row"
//    of indices in `update_register`.
//
// Necessary for the case where we are trying to build the output indices span
// with all the indices into the storage for each relevant column.
struct StrideCopyDenseNullIndices : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register,
                                     uint32_t,
                                     offset,
                                     uint32_t,
                                     stride);
};

// Allocates a buffer for row layout storage.
struct AllocateRowLayoutBuffer : Bytecode {
  static constexpr Cost kCost = FixedCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(uint32_t,
                                     buffer_size,
                                     WriteHandle<Slab<uint8_t>>,
                                     dest_buffer_register);
};

// Copies data for a non-null column into the row layout buffer.
struct CopyToRowLayoutBase
    : TemplatedBytecode2<StorageType, SparseNullCollapsedNullability> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_9(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_indices_register,
                                     RwHandle<Slab<uint8_t>>,
                                     dest_buffer_register,
                                     uint16_t,
                                     row_layout_offset,
                                     uint16_t,
                                     row_layout_stride,
                                     uint32_t,
                                     invert_copied_bits,
                                     ReadHandle<Slab<uint32_t>>,
                                     popcount_register,
                                     ReadHandle<StringIdToRankMap>,
                                     rank_map_register);
};
template <typename T, typename Nullability>
struct CopyToRowLayout : CopyToRowLayoutBase {
  static_assert(TS1::Contains<T>());
  static_assert(TS2::Contains<Nullability>());
};

// Performs distinct operation on row layout buffer using opaque byte
// comparison.
struct Distinct : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{7};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<Slab<uint8_t>>,
                                     buffer_register,
                                     uint32_t,
                                     total_row_stride,
                                     RwHandle<Span<uint32_t>>,
                                     indices_register);
};

// Applies an offset to the indices span and limits the rows.
// Modifies the span referenced by `update_register` in place.
//
// Note: `limit_value` = UINT32_MAX means no limit.
struct LimitOffsetIndices : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = PostOperationLinearPerRowCost{2};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(uint32_t,
                                     offset_value,
                                     uint32_t,
                                     limit_value,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};

// Finds the min/max for a single column.
struct FindMinMaxIndexBase : TemplatedBytecode2<StorageType, MinMaxOp> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{2};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_2(ReadHandle<StoragePtr>,
                                     storage_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};
template <typename T, typename Op>
struct FindMinMaxIndex : FindMinMaxIndexBase {
  static_assert(TS1::Contains<T>());
  static_assert(TS2::Contains<Op>());
};

// Filters a column which is sorted by the given index. `source_register`
// contains the span of permutation vector to consider (read-only).
// `dest_register` receives the filtered result (write-only).
struct IndexedFilterEqBase
    : TemplatedBytecode2<NonIdStorageType, SparseNullCollapsedNullability> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LogPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_6(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<const BitVector*>,
                                     null_bv_register,
                                     ReadHandle<CastFilterValueResult>,
                                     filter_value_reg,
                                     ReadHandle<Slab<uint32_t>>,
                                     popcount_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     WriteHandle<Span<uint32_t>>,
                                     dest_register);
};
template <typename T, typename N>
struct IndexedFilterEq : IndexedFilterEqBase {
  static_assert(TS1::Contains<T>());
  static_assert(TS2::Contains<N>());
};

// Given a source span and a source range, copies all indices in the span which
// are in bounds in then range to the destiation span. The destination span must
// be large enough to hold all the indices in the source span.
struct CopySpanIntersectingRange : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     ReadHandle<Range>,
                                     source_range_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};

// Initializes a new StringIdToRankMap in a destination register.
struct InitRankMap : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = FixedCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_1(WriteHandle<StringIdToRankMap>,
                                     dest_register);
};

// Collects unique StringPool::Ids from a string column into a
// StringIdToRankMap. Ranks are all initialized to 0.
struct CollectIdIntoRankMap : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<StringIdToRankMap>,
                                     rank_map_register);
};

// Takes a RankMap (populated with unique StringPool::Ids and placeholder
// ranks), sorts the IDs lexicographically, and updates the map in-place with
// the final ranks.
struct FinalizeRanksInMap : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LogLinearPerRowCost{20};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_1(RwHandle<StringIdToRankMap>,
                                     update_register);
};

// Performs a stable sort on indices based on a pre-built row layout buffer.
struct SortRowLayout : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LogLinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<Slab<uint8_t>>,
                                     buffer_register,
                                     uint32_t,
                                     total_row_stride,
                                     RwHandle<Span<uint32_t>>,
                                     indices_register);
};

// Filters a column with a scan over a linear range of indices. Useful for the
// first equality check of a query where we can scan a column without
// materializing a large set of indices and then using
// NonStringFilter/StringFilter to cut it down.
struct LinearFilterEqBase : TemplatedBytecode1<NonIdStorageType> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{7};
  PERFETTO_DATAFRAME_BYTECODE_IMPL_5(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueResult>,
                                     filter_value_reg,
                                     ReadHandle<Slab<uint32_t>>,
                                     popcount_register,
                                     ReadHandle<Range>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};
template <typename T>
struct LinearFilterEq : LinearFilterEqBase {
  static_assert(TS1::Contains<T>());
};

// Filters rows based on a list of values (IN operator).
struct InBase : TemplatedBytecode1<StorageType> {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{10};
  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<StoragePtr>,
                                     storage_register,
                                     ReadHandle<CastFilterValueListResult>,
                                     value_list_register,
                                     RwHandle<Span<uint32_t>>,
                                     source_register,
                                     RwHandle<Span<uint32_t>>,
                                     update_register);
};
template <typename T>
struct In : InBase {
  static_assert(TS1::Contains<T>());
};

// Reverses the order of indices in the given register.
struct Reverse : Bytecode {
  // TODO(lalitm): while the cost type is legitimate, the cost estimate inside
  // is plucked from thin air and has no real foundation. Fix this by creating
  // benchmarks and backing it up with actual data.
  static constexpr Cost kCost = LinearPerRowCost{2};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_1(RwHandle<Span<uint32_t>>, update_register);
};

// Fills pre-allocated parent and original_rows spans with data from storage.
// - Copies parent_id data into parent_span
// - Sets original_rows_span to identity (0, 1, 2, ...)
// - Updates span.e = span.b + row_count for both spans
struct MakeChildToParentTreeStructure : Bytecode {
  static constexpr Cost kCost = LinearPerRowCost{10};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_4(ReadHandle<StoragePtr>,
                                     parent_id_storage_register,
                                     uint32_t,
                                     row_count,
                                     RwHandle<Span<uint32_t>>,
                                     parent_span_register,
                                     RwHandle<Span<uint32_t>>,
                                     original_rows_span_register);
};

// Creates CSR (Compressed Sparse Row) spans from parent span.
// This enables efficient BFS traversal from roots to children.
// - offsets[i] = start index in children array for node i's children
// - children = flattened list of child indices
// - roots = list of root node indices (nodes with kNullParent)
//
// Example: For a tree with parent array [NULL, 0, 0, 1] representing:
//     0 (root)
//    / |
//   1   2
//   |
//   3
//
// Output:
//   offsets  = [0, 2, 3, 3, 3]  (node 0 has 2 children at indices 0-1,
//                                node 1 has 1 child at index 2, etc.)
//   children = [1, 2, 3]        (node 0's children: 1,2; node 1's child: 3)
//   roots    = [0]              (single root)
//
// The node count is derived from parent_span_register.size().
//
// Registers:
//   - parent_span: input span containing parent indices (kNullParent for roots)
//   - scratch: size = n, used for child_counts during two-pass algorithm
//   - offsets: size = n + 1, output span for CSR offsets
//   - children: size = n, output span for children (actual size = n -
//   root_count)
//   - roots: size = n, output span for roots (actual size = root_count)
struct MakeParentToChildTreeStructure : Bytecode {
  static constexpr Cost kCost = LinearPerRowCost{15};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_5(ReadHandle<Span<uint32_t>>,
                                     parent_span_register,
                                     ReadHandle<Span<uint32_t>>,
                                     scratch_register,
                                     RwHandle<Span<uint32_t>>,
                                     offsets_register,
                                     RwHandle<Span<uint32_t>>,
                                     children_register,
                                     RwHandle<Span<uint32_t>>,
                                     roots_register);
};

// Converts a span of indices to a BitVector with bits set at those indices.
// Used to convert filtered node indices into a bitvector for FilterTree.
struct IndexSpanToBitvector : Bytecode {
  static constexpr Cost kCost = LinearPerRowCost{5};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_3(ReadHandle<Span<uint32_t>>,
                                     indices_register,
                                     uint32_t,
                                     bitvector_size,
                                     WriteHandle<BitVector>,
                                     dest_register);
};

// Filters a tree by keeping only nodes specified in the bitvector.
// Children of removed nodes are reparented to their closest surviving ancestor.
// The parent and original_rows spans are compacted in-place to remove filtered
// nodes.
//
// Algorithm:
//   1. BFS from roots using CSR structure
//   2. For each node, track closest surviving ancestor
//   3. Build compacted parent array with reparenting
//   4. Build compacted original_rows array
//
// The node count is derived from parent_span_register.size().
// After filtering, both span.e pointers are updated to reflect the new count.
//
// Scratch registers:
//   - scratch1: size = n*2, used for surviving_ancestor (first n) and queue
//   - scratch2: size = n, used for old_to_new mapping
struct FilterTree : Bytecode {
  static constexpr Cost kCost = LinearPerRowCost{20};

  PERFETTO_DATAFRAME_BYTECODE_IMPL_8(ReadHandle<Span<uint32_t>>,
                                     offsets_register,
                                     ReadHandle<Span<uint32_t>>,
                                     children_register,
                                     ReadHandle<Span<uint32_t>>,
                                     roots_register,
                                     ReadHandle<BitVector>,
                                     keep_bitvector_register,
                                     RwHandle<Span<uint32_t>>,
                                     parent_span_register,
                                     RwHandle<Span<uint32_t>>,
                                     original_rows_span_register,
                                     ReadHandle<Span<uint32_t>>,
                                     scratch1_register,
                                     ReadHandle<Span<uint32_t>>,
                                     scratch2_register);
};

// Bytecode ops that require FilterValueFetcher access.
#define PERFETTO_DATAFRAME_BYTECODE_FVF_LIST(X) \
  X(CastFilterValue<Id>)                        \
  X(CastFilterValue<Uint32>)                    \
  X(CastFilterValue<Int32>)                     \
  X(CastFilterValue<Int64>)                     \
  X(CastFilterValue<Double>)                    \
  X(CastFilterValue<String>)                    \
  X(CastFilterValueList<Id>)                    \
  X(CastFilterValueList<Uint32>)                \
  X(CastFilterValueList<Int32>)                 \
  X(CastFilterValueList<Int64>)                 \
  X(CastFilterValueList<Double>)                \
  X(CastFilterValueList<String>)

// Bytecode ops that only need InterpreterState (no FilterValueFetcher).
#define PERFETTO_DATAFRAME_BYTECODE_STATE_ONLY_LIST(X) \
  X(InitRange)                                         \
  X(AllocateIndices)                                   \
  X(Iota)                                              \
  X(SortedFilter<Id, EqualRange>)                      \
  X(SortedFilter<Id, LowerBound>)                      \
  X(SortedFilter<Id, UpperBound>)                      \
  X(SortedFilter<Uint32, EqualRange>)                  \
  X(SortedFilter<Uint32, LowerBound>)                  \
  X(SortedFilter<Uint32, UpperBound>)                  \
  X(SortedFilter<Int32, EqualRange>)                   \
  X(SortedFilter<Int32, LowerBound>)                   \
  X(SortedFilter<Int32, UpperBound>)                   \
  X(SortedFilter<Int64, EqualRange>)                   \
  X(SortedFilter<Int64, LowerBound>)                   \
  X(SortedFilter<Int64, UpperBound>)                   \
  X(SortedFilter<Double, EqualRange>)                  \
  X(SortedFilter<Double, LowerBound>)                  \
  X(SortedFilter<Double, UpperBound>)                  \
  X(SortedFilter<String, EqualRange>)                  \
  X(SortedFilter<String, LowerBound>)                  \
  X(SortedFilter<String, UpperBound>)                  \
  X(Uint32SetIdSortedEq)                               \
  X(SpecializedStorageSmallValueEq)                    \
  X(LinearFilterEq<Uint32>)                            \
  X(LinearFilterEq<Int32>)                             \
  X(LinearFilterEq<Int64>)                             \
  X(LinearFilterEq<Double>)                            \
  X(LinearFilterEq<String>)                            \
  X(NonStringFilter<Id, Eq>)                           \
  X(NonStringFilter<Id, Ne>)                           \
  X(NonStringFilter<Id, Lt>)                           \
  X(NonStringFilter<Id, Le>)                           \
  X(NonStringFilter<Id, Gt>)                           \
  X(NonStringFilter<Id, Ge>)                           \
  X(NonStringFilter<Uint32, Eq>)                       \
  X(NonStringFilter<Uint32, Ne>)                       \
  X(NonStringFilter<Uint32, Lt>)                       \
  X(NonStringFilter<Uint32, Le>)                       \
  X(NonStringFilter<Uint32, Gt>)                       \
  X(NonStringFilter<Uint32, Ge>)                       \
  X(NonStringFilter<Int32, Eq>)                        \
  X(NonStringFilter<Int32, Ne>)                        \
  X(NonStringFilter<Int32, Lt>)                        \
  X(NonStringFilter<Int32, Le>)                        \
  X(NonStringFilter<Int32, Gt>)                        \
  X(NonStringFilter<Int32, Ge>)                        \
  X(NonStringFilter<Int64, Eq>)                        \
  X(NonStringFilter<Int64, Ne>)                        \
  X(NonStringFilter<Int64, Lt>)                        \
  X(NonStringFilter<Int64, Le>)                        \
  X(NonStringFilter<Int64, Gt>)                        \
  X(NonStringFilter<Int64, Ge>)                        \
  X(NonStringFilter<Double, Eq>)                       \
  X(NonStringFilter<Double, Ne>)                       \
  X(NonStringFilter<Double, Lt>)                       \
  X(NonStringFilter<Double, Le>)                       \
  X(NonStringFilter<Double, Gt>)                       \
  X(NonStringFilter<Double, Ge>)                       \
  X(StringFilter<Eq>)                                  \
  X(StringFilter<Ne>)                                  \
  X(StringFilter<Lt>)                                  \
  X(StringFilter<Le>)                                  \
  X(StringFilter<Gt>)                                  \
  X(StringFilter<Ge>)                                  \
  X(StringFilter<Glob>)                                \
  X(StringFilter<Regex>)                               \
  X(NullFilter<IsNotNull>)                             \
  X(NullFilter<IsNull>)                                \
  X(StrideCopy)                                        \
  X(StrideTranslateAndCopySparseNullIndices)           \
  X(StrideCopyDenseNullIndices)                        \
  X(PrefixPopcount)                                    \
  X(TranslateSparseNullIndices)                        \
  X(AllocateRowLayoutBuffer)                           \
  X(CopyToRowLayout<Id, NonNull>)                      \
  X(CopyToRowLayout<Id, SparseNull>)                   \
  X(CopyToRowLayout<Id, DenseNull>)                    \
  X(CopyToRowLayout<Uint32, NonNull>)                  \
  X(CopyToRowLayout<Uint32, SparseNull>)               \
  X(CopyToRowLayout<Uint32, DenseNull>)                \
  X(CopyToRowLayout<Int32, NonNull>)                   \
  X(CopyToRowLayout<Int32, SparseNull>)                \
  X(CopyToRowLayout<Int32, DenseNull>)                 \
  X(CopyToRowLayout<Int64, NonNull>)                   \
  X(CopyToRowLayout<Int64, SparseNull>)                \
  X(CopyToRowLayout<Int64, DenseNull>)                 \
  X(CopyToRowLayout<Double, NonNull>)                  \
  X(CopyToRowLayout<Double, SparseNull>)               \
  X(CopyToRowLayout<Double, DenseNull>)                \
  X(CopyToRowLayout<String, NonNull>)                  \
  X(CopyToRowLayout<String, SparseNull>)               \
  X(CopyToRowLayout<String, DenseNull>)                \
  X(Distinct)                                          \
  X(LimitOffsetIndices)                                \
  X(FindMinMaxIndex<Id, MinOp>)                        \
  X(FindMinMaxIndex<Id, MaxOp>)                        \
  X(FindMinMaxIndex<Uint32, MinOp>)                    \
  X(FindMinMaxIndex<Uint32, MaxOp>)                    \
  X(FindMinMaxIndex<Int32, MinOp>)                     \
  X(FindMinMaxIndex<Int32, MaxOp>)                     \
  X(FindMinMaxIndex<Int64, MinOp>)                     \
  X(FindMinMaxIndex<Int64, MaxOp>)                     \
  X(FindMinMaxIndex<Double, MinOp>)                    \
  X(FindMinMaxIndex<Double, MaxOp>)                    \
  X(FindMinMaxIndex<String, MinOp>)                    \
  X(FindMinMaxIndex<String, MaxOp>)                    \
  X(IndexedFilterEq<Uint32, NonNull>)                  \
  X(IndexedFilterEq<Uint32, SparseNull>)               \
  X(IndexedFilterEq<Uint32, DenseNull>)                \
  X(IndexedFilterEq<Int32, NonNull>)                   \
  X(IndexedFilterEq<Int32, SparseNull>)                \
  X(IndexedFilterEq<Int32, DenseNull>)                 \
  X(IndexedFilterEq<Int64, NonNull>)                   \
  X(IndexedFilterEq<Int64, SparseNull>)                \
  X(IndexedFilterEq<Int64, DenseNull>)                 \
  X(IndexedFilterEq<Double, NonNull>)                  \
  X(IndexedFilterEq<Double, SparseNull>)               \
  X(IndexedFilterEq<Double, DenseNull>)                \
  X(IndexedFilterEq<String, NonNull>)                  \
  X(IndexedFilterEq<String, SparseNull>)               \
  X(IndexedFilterEq<String, DenseNull>)                \
  X(CopySpanIntersectingRange)                         \
  X(InitRankMap)                                       \
  X(CollectIdIntoRankMap)                              \
  X(FinalizeRanksInMap)                                \
  X(SortRowLayout)                                     \
  X(In<Id>)                                            \
  X(In<Uint32>)                                        \
  X(In<Int32>)                                         \
  X(In<Int64>)                                         \
  X(In<Double>)                                        \
  X(In<String>)                                        \
  X(Reverse)                                           \
  X(MakeChildToParentTreeStructure)                    \
  X(MakeParentToChildTreeStructure)                    \
  X(IndexSpanToBitvector)                              \
  X(FilterTree)

// Combined list of all bytecode instruction types.
#define PERFETTO_DATAFRAME_BYTECODE_LIST(X) \
  PERFETTO_DATAFRAME_BYTECODE_FVF_LIST(X)   \
  PERFETTO_DATAFRAME_BYTECODE_STATE_ONLY_LIST(X)

#define PERFETTO_DATAFRAME_BYTECODE_VARIANT(...) __VA_ARGS__,

// Variant type containing all possible bytecode instructions.
using BytecodeVariant = std::variant<PERFETTO_DATAFRAME_BYTECODE_LIST(
    PERFETTO_DATAFRAME_BYTECODE_VARIANT) std::monostate>;

// Gets the variant index for a specific bytecode type.
template <typename T>
constexpr uint32_t Index() {
  return base::variant_index<BytecodeVariant, T>();
}

// Gets bytecode index for a templated type with one type parameter.
template <template <typename> typename T, typename V1>
PERFETTO_ALWAYS_INLINE constexpr uint32_t Index(const V1& f) {
  using Start = T<typename V1::template GetTypeAtIndex<0>>;
  using End = T<typename V1::template GetTypeAtIndex<V1::kSize - 1>>;
  uint32_t offset = Start::OpcodeOffset(f);
  if (offset > Index<End>() - Index<Start>()) {
    PERFETTO_FATAL("Invalid opcode offset (t1: %u) %u (start: %u, end: %u)",
                   f.index(), offset, Index<Start>(), Index<End>());
  }
  return Index<Start>() + offset;
}

// Gets bytecode index for a templated type with two type parameters.
template <template <typename, typename> typename T, typename V1, typename V2>
PERFETTO_ALWAYS_INLINE constexpr uint32_t Index(const V1& f, const V2& s) {
  using Start = T<typename V1::template GetTypeAtIndex<0>,
                  typename V2::template GetTypeAtIndex<0>>;
  using End = T<typename V1::template GetTypeAtIndex<V1::kSize - 1>,
                typename V2::template GetTypeAtIndex<V2::kSize - 1>>;
  uint32_t offset = Start::OpcodeOffset(f, s);
  if (offset > Index<End>() - Index<Start>()) {
    PERFETTO_FATAL(
        "Invalid opcode offset (t1: %u t2: %u) %u (start: %u, end: %u)",
        f.index(), s.index(), offset, Index<Start>(), Index<End>());
  }
  return Index<Start>() + offset;
}

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INSTRUCTIONS_H_
