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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_IMPL_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_IMPL_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <type_traits>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/endian.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/null_types.h"
#include "src/trace_processor/core/common/op_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/common/tree_types.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter_state.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/flex_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::interpreter {
namespace comparators {

// Returns an appropriate comparator functor for the given integer/double type
// and operation. Currently only supports equality comparison.
template <typename T, typename Op>
auto IntegerOrDoubleComparator() {
  if constexpr (std::is_same_v<Op, Eq>) {
    return std::equal_to<T>();
  } else if constexpr (std::is_same_v<Op, Ne>) {
    return std::not_equal_to<T>();
  } else if constexpr (std::is_same_v<Op, Lt>) {
    return std::less<T>();
  } else if constexpr (std::is_same_v<Op, Le>) {
    return std::less_equal<T>();
  } else if constexpr (std::is_same_v<Op, Gt>) {
    return std::greater<T>();
  } else if constexpr (std::is_same_v<Op, Ge>) {
    return std::greater_equal<T>();
  } else {
    static_assert(std::is_same_v<Op, Eq>, "Unsupported op");
  }
}

template <typename T>
struct StringComparator {
  bool operator()(StringPool::Id lhs, NullTermStringView rhs) const {
    if constexpr (std::is_same_v<T, Lt>) {
      return pool_->Get(lhs) < rhs;
    } else if constexpr (std::is_same_v<T, Le>) {
      return pool_->Get(lhs) <= rhs;
    } else if constexpr (std::is_same_v<T, Gt>) {
      return pool_->Get(lhs) > rhs;
    } else if constexpr (std::is_same_v<T, Ge>) {
      return pool_->Get(lhs) >= rhs;
    } else {
      static_assert(std::is_same_v<T, Lt>, "Unsupported op");
    }
  }
  const StringPool* pool_;
};
struct StringLessInvert {
  bool operator()(NullTermStringView lhs, StringPool::Id rhs) const {
    return lhs < pool_->Get(rhs);
  }
  const StringPool* pool_;
};

}  // namespace comparators

namespace ops {

// Outlined implementation of SortRowLayout bytecode.
// Sorts indices based on row layout data in buffer.
void SortRowLayoutImpl(const Slab<uint8_t>& buffer,
                       uint32_t stride,
                       Span<uint32_t>& indices);

// Outlined implementation of FinalizeRanksInMap bytecode.
// Sorts string IDs and assigns ranks in the map.
void FinalizeRanksInMapImpl(
    const StringPool* string_pool,
    std::unique_ptr<base::FlatHashMap<StringPool::Id, uint32_t>>& rank_map_ptr);

// Outlined implementation of Distinct bytecode.
// Removes duplicate rows based on row layout data.
void DistinctImpl(const Slab<uint8_t>& buffer,
                  uint32_t stride,
                  Span<uint32_t>& indices);

// Outlined implementation of glob filtering for strings.
// Returns pointer past last written output index.
uint32_t* StringFilterGlobImpl(const StringPool* string_pool,
                               const StringPool::Id* data,
                               const char* pattern,
                               const uint32_t* begin,
                               const uint32_t* end,
                               uint32_t* output);

// Outlined implementation of regex filtering for strings.
// Returns pointer past last written output index.
uint32_t* StringFilterRegexImpl(const StringPool* string_pool,
                                const StringPool::Id* data,
                                const char* pattern,
                                const uint32_t* begin,
                                const uint32_t* end,
                                uint32_t* output);

// Handles invalid cast filter value results for filtering operations.
// If the cast result is invalid, updates the range or span accordingly.
//
// Returns true if the result is valid, false otherwise.
template <typename T>
PERFETTO_ALWAYS_INLINE bool HandleInvalidCastFilterValueResult(
    const CastFilterValueResult::Validity& validity,
    T& update) {
  static_assert(std::is_same_v<T, Range> || std::is_same_v<T, Span<uint32_t>>);
  if (PERFETTO_UNLIKELY(validity != CastFilterValueResult::kValid)) {
    if (validity == CastFilterValueResult::kNoneMatch) {
      update.e = update.b;
    }
    return false;
  }
  return true;
}

// Filters an existing index buffer in-place, based on data comparisons
// performed using a separate set of source indices.
//
// This function iterates synchronously through two sets of indices:
// 1. Source Indices: Provided by [begin, end), pointed to by `it`. These
//    indices are used *only* to look up data values (`data[*it]`).
// 2. Destination/Update Indices: Starting at `o_start`, pointed to by
//    `o_read` (for reading the original index) and `o_write` (for writing
//    kept indices). This buffer is modified *in-place*.
//
// For each step `i`:
//   - It retrieves the data value using the i-th source index:
//   `data[begin[i]]`.
//   - It compares this data value against the provided `value`.
//   - It reads the i-th *original* index from the destination buffer:
//   `o_read[i]`.
//   - If the comparison is true, it copies the original index `o_read[i]`
//     to the current write position `*o_write` and advances `o_write`.
//
// The result is that the destination buffer `[o_start, returned_pointer)`
// contains the subset of its *original* indices for which the comparison
// (using the corresponding source index for data lookup) was true.
//
// Use Case Example (SparseNull Filter):
//   - `[begin, end)` holds translated storage indices (for correct data
//     lookup).
//   - `o_start` points to the buffer holding original table indices (that
//     was have already been filtered by `NullFilter<IsNotNull>`).
//   - This function further filters the original table indices in `o_start`
//     based on data comparisons using the translated indices.
//
// Args:
//   data: Pointer to the start of the column's data storage.
//   begin: Pointer to the first index in the source span (for data lookup).
//   end: Pointer one past the last index in the source span.
//   o_start: Pointer to the destination/update buffer (filtered in-place).
//   value: The value to compare data against.
//   comparator: Functor implementing the comparison logic.
//
// Returns:
//   A pointer one past the last index written to the destination buffer.
template <typename Comparator, typename ValueType, typename DataType>
[[nodiscard]] PERFETTO_ALWAYS_INLINE uint32_t* Filter(
    const DataType* data,
    const uint32_t* begin,
    const uint32_t* end,
    uint32_t* output,
    const ValueType& value,
    const Comparator& comparator) {
  const uint32_t* o_read = output;
  uint32_t* o_write = output;
  for (const uint32_t* it = begin; it != end; ++it, ++o_read) {
    // The choice of a branchy implemntation is intentional: this seems faster
    // than trying to do something branchless, likely because the compiler is
    // helping us with branch prediction.
    if (comparator(data[*it], value)) {
      *o_write++ = *o_read;
    }
  }
  return o_write;
}

// Similar to Filter but operates directly on the identity values
// (indices) rather than dereferencing through a data array.
template <typename Comparator, typename ValueType>
[[nodiscard]] PERFETTO_ALWAYS_INLINE uint32_t* IdentityFilter(
    const uint32_t* begin,
    const uint32_t* end,
    uint32_t* output,
    const ValueType& value,
    Comparator comparator) {
  const uint32_t* o_read = output;
  uint32_t* o_write = output;
  for (const uint32_t* it = begin; it != end; ++it, ++o_read) {
    // The choice of a branchy implemntation is intentional: this seems faster
    // than trying to do something branchless, likely because the compiler is
    // helping us with branch prediction.
    if (comparator(*it, value)) {
      *o_write++ = *o_read;
    }
  }
  return o_write;
}

template <typename T>
inline PERFETTO_ALWAYS_INLINE auto GetComparableRowLayoutReprInteger(T x) {
  // The inspiration behind this function comes from:
  // https://arrow.apache.org/blog/2022/11/07/multi-column-sorts-in-arrow-rust-part-2/
  if constexpr (std::is_same_v<T, uint32_t>) {
    return base::HostToBE32(x);
  } else if constexpr (std::is_same_v<T, int32_t>) {
    return base::HostToBE32(
        static_cast<uint32_t>(x ^ static_cast<int32_t>(0x80000000)));
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return base::HostToBE64(
        static_cast<uint64_t>(x ^ static_cast<int64_t>(0x8000000000000000)));
  } else {
    static_assert(std::is_same_v<T, uint32_t>,
                  "Unsupported type for row layout representation");
  }
}

template <typename T>
inline PERFETTO_ALWAYS_INLINE auto GetComparableRowLayoutRepr(T x) {
  // The inspiration behind this function comes from:
  // https://arrow.apache.org/blog/2022/11/07/multi-column-sorts-in-arrow-rust-part-2/
  if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t> ||
                std::is_same_v<T, int64_t>) {
    return GetComparableRowLayoutReprInteger(x);
  } else if constexpr (std::is_same_v<T, double>) {
    int64_t bits;
    memcpy(&bits, &x, sizeof(double));
    bits ^= static_cast<int64_t>(static_cast<uint64_t>(bits >> 63) >> 1);
    return GetComparableRowLayoutReprInteger(bits);
  } else {
    static_assert(std::is_same_v<T, uint32_t>,
                  "Unsupported type for row layout representation");
  }
}

inline PERFETTO_ALWAYS_INLINE void InitRange(InterpreterState& state,
                                             const struct InitRange& init) {
  using B = struct InitRange;
  state.WriteToRegister(init.arg<B::dest_register>(),
                        Range{0, init.arg<B::size>()});
}

inline PERFETTO_ALWAYS_INLINE void AllocateIndices(
    InterpreterState& state,
    const struct AllocateIndices& ai) {
  using B = struct AllocateIndices;

  if (auto* exist_slab =
          state.MaybeReadFromRegister(ai.arg<B::dest_slab_register>())) {
    // Ensure that the slab is at least as big as the requested size.
    PERFETTO_DCHECK(ai.arg<B::size>() <= exist_slab->size());

    // Update the span to point to the needed size of the slab.
    state.WriteToRegister(
        ai.arg<B::dest_span_register>(),
        Span<uint32_t>{exist_slab->begin(),
                       exist_slab->begin() + ai.arg<B::size>()});
  } else {
    auto slab = Slab<uint32_t>::Alloc(ai.arg<B::size>());
    Span<uint32_t> span{slab.begin(), slab.end()};
    state.WriteToRegister(ai.arg<B::dest_slab_register>(), std::move(slab));
    state.WriteToRegister(ai.arg<B::dest_span_register>(), span);
  }
}

inline PERFETTO_ALWAYS_INLINE void Iota(InterpreterState& state,
                                        const struct Iota& r) {
  using B = struct Iota;
  const auto& source = state.ReadFromRegister(r.arg<B::source_register>());
  auto& update = state.ReadFromRegister(r.arg<B::update_register>());
  PERFETTO_DCHECK(source.size() <= update.size());
  auto* end = update.b + source.size();
  std::iota(update.b, end, source.b);
  update.e = end;
}

inline PERFETTO_ALWAYS_INLINE void Reverse(InterpreterState& state,
                                           const struct Reverse& r) {
  using B = struct Reverse;
  auto& update = state.ReadFromRegister(r.arg<B::update_register>());
  std::reverse(update.b, update.e);
}

inline PERFETTO_ALWAYS_INLINE void StrideCopy(
    InterpreterState& state,
    const struct StrideCopy& stride_copy) {
  using B = struct StrideCopy;
  const auto& source =
      state.ReadFromRegister(stride_copy.arg<B::source_register>());
  auto& update = state.ReadFromRegister(stride_copy.arg<B::update_register>());
  uint32_t stride = stride_copy.arg<B::stride>();
  PERFETTO_DCHECK(source.size() * stride <= update.size());
  if (PERFETTO_LIKELY(stride == 1)) {
    memcpy(update.b, source.b, source.size() * sizeof(uint32_t));
  } else {
    uint32_t* write_ptr = update.b;
    for (const uint32_t* it = source.b; it < source.e; ++it) {
      *write_ptr = *it;
      write_ptr += stride;
    }
    PERFETTO_DCHECK(write_ptr == update.b + source.size() * stride);
  }
  update.e = update.b + source.size() * stride;
}

inline PERFETTO_ALWAYS_INLINE void PrefixPopcount(
    InterpreterState& state,
    const struct PrefixPopcount& popcount) {
  using B = struct PrefixPopcount;
  auto dest_register = popcount.arg<B::dest_register>();
  if (state.MaybeReadFromRegister(dest_register)) {
    return;
  }
  const BitVector* null_bv =
      state.ReadFromRegister(popcount.arg<B::null_bv_register>());
  state.WriteToRegister(dest_register, null_bv->PrefixPopcount());
}

inline PERFETTO_ALWAYS_INLINE void AllocateRowLayoutBuffer(
    InterpreterState& state,
    const struct AllocateRowLayoutBuffer& bytecode) {
  using B = struct AllocateRowLayoutBuffer;
  uint32_t size = bytecode.arg<B::buffer_size>();
  auto dest_reg = bytecode.arg<B::dest_buffer_register>();
  // Return early if buffer already allocated.
  if (state.MaybeReadFromRegister(dest_reg)) {
    return;
  }
  state.WriteToRegister(dest_reg, Slab<uint8_t>::Alloc(size));
}

inline PERFETTO_ALWAYS_INLINE void LimitOffsetIndices(
    InterpreterState& state,
    const LimitOffsetIndices& bytecode) {
  using B = struct LimitOffsetIndices;
  uint32_t offset_value = bytecode.arg<B::offset_value>();
  uint32_t limit_value = bytecode.arg<B::limit_value>();
  auto& span = state.ReadFromRegister(bytecode.arg<B::update_register>());

  // Apply offset
  auto original_size = static_cast<uint32_t>(span.size());
  uint32_t actual_offset = std::min(offset_value, original_size);
  span.b += actual_offset;

  // Apply limit
  auto size_after_offset = static_cast<uint32_t>(span.size());
  uint32_t actual_limit = std::min(limit_value, size_after_offset);
  span.e = span.b + actual_limit;
}

inline PERFETTO_ALWAYS_INLINE void CopySpanIntersectingRange(
    InterpreterState& state,
    const CopySpanIntersectingRange& bytecode) {
  using B = struct CopySpanIntersectingRange;
  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  const auto& source_range =
      state.ReadFromRegister(bytecode.arg<B::source_range_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  PERFETTO_DCHECK(source.size() <= update.size());
  uint32_t* write_ptr = update.b;
  for (const uint32_t* it = source.b; it != source.e; ++it) {
    *write_ptr = *it;
    write_ptr += (*it >= source_range.b && *it < source_range.e);
  }
  update.e = write_ptr;
}

inline PERFETTO_ALWAYS_INLINE void InitRankMap(InterpreterState& state,
                                               const InitRankMap& bytecode) {
  using B = struct InitRankMap;

  StringIdToRankMap* rank_map =
      state.MaybeReadFromRegister(bytecode.arg<B::dest_register>());
  if (rank_map) {
    rank_map->get()->Clear();
  } else {
    state.WriteToRegister(
        bytecode.arg<B::dest_register>(),
        std::make_unique<base::FlatHashMap<StringPool::Id, uint32_t>>());
  }
}

inline PERFETTO_ALWAYS_INLINE void CollectIdIntoRankMap(
    InterpreterState& state,
    const CollectIdIntoRankMap& bytecode) {
  using B = struct CollectIdIntoRankMap;

  StringIdToRankMap& rank_map_ptr =
      state.ReadFromRegister(bytecode.arg<B::rank_map_register>());
  PERFETTO_DCHECK(rank_map_ptr);
  auto& rank_map = *rank_map_ptr;

  const StringPool::Id* data = state.ReadStorageFromRegister<String>(
      bytecode.arg<B::storage_register>());
  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  for (const uint32_t* it = source.b; it != source.e; ++it) {
    rank_map.Insert(data[*it], 0);
  }
}

inline PERFETTO_ALWAYS_INLINE void FinalizeRanksInMap(
    InterpreterState& state,
    const FinalizeRanksInMap& bytecode) {
  using B = struct FinalizeRanksInMap;
  StringIdToRankMap& rank_map_ptr =
      state.ReadFromRegister(bytecode.arg<B::update_register>());
  FinalizeRanksInMapImpl(state.string_pool, rank_map_ptr);
}

inline PERFETTO_ALWAYS_INLINE void Distinct(InterpreterState& state,
                                            const Distinct& bytecode) {
  using B = struct Distinct;
  auto& indices = state.ReadFromRegister(bytecode.arg<B::indices_register>());
  if (indices.empty()) {
    return;
  }
  const auto& buffer =
      state.ReadFromRegister(bytecode.arg<B::buffer_register>());
  uint32_t stride = bytecode.arg<B::total_row_stride>();
  DistinctImpl(buffer, stride, indices);
}

inline PERFETTO_ALWAYS_INLINE void SortRowLayout(
    InterpreterState& state,
    const SortRowLayout& bytecode) {
  using B = struct SortRowLayout;
  auto& indices = state.ReadFromRegister(bytecode.arg<B::indices_register>());
  // Single element is always sorted.
  if (indices.size() <= 1) {
    return;
  }
  const auto& buffer =
      state.ReadFromRegister(bytecode.arg<B::buffer_register>());
  uint32_t stride = bytecode.arg<B::total_row_stride>();
  SortRowLayoutImpl(buffer, stride, indices);
}

inline PERFETTO_ALWAYS_INLINE void TranslateSparseNullIndices(
    InterpreterState& state,
    const TranslateSparseNullIndices& bytecode) {
  using B = struct TranslateSparseNullIndices;
  const BitVector* bv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  PERFETTO_DCHECK(source.size() <= update.size());

  const Slab<uint32_t>& popcnt =
      state.ReadFromRegister(bytecode.arg<B::popcount_register>());
  uint32_t* out = update.b;
  for (uint32_t* it = source.b; it != source.e; ++it, ++out) {
    uint32_t s = *it;
    *out = static_cast<uint32_t>(popcnt[s / 64] +
                                 bv->count_set_bits_until_in_word(s));
  }
  update.e = out;
}

inline PERFETTO_ALWAYS_INLINE void StrideTranslateAndCopySparseNullIndices(
    InterpreterState& state,
    const StrideTranslateAndCopySparseNullIndices& bytecode) {
  using B = struct StrideTranslateAndCopySparseNullIndices;
  const BitVector* bv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  uint32_t stride = bytecode.arg<B::stride>();
  uint32_t offset = bytecode.arg<B::offset>();
  const Slab<uint32_t>& popcnt =
      state.ReadFromRegister(bytecode.arg<B::popcount_register>());
  for (uint32_t* it = update.b; it != update.e; it += stride) {
    uint32_t index = *it;
    if (bv->is_set(index)) {
      it[offset] = static_cast<uint32_t>(
          popcnt[index / 64] + bv->count_set_bits_until_in_word(index));
    } else {
      it[offset] = std::numeric_limits<uint32_t>::max();
    }
  }
}

inline PERFETTO_ALWAYS_INLINE void StrideCopyDenseNullIndices(
    InterpreterState& state,
    const StrideCopyDenseNullIndices& bytecode) {
  using B = struct StrideCopyDenseNullIndices;
  const BitVector* bv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  uint32_t stride = bytecode.arg<B::stride>();
  uint32_t offset = bytecode.arg<B::offset>();
  for (uint32_t* it = update.b; it != update.e; it += stride) {
    it[offset] = bv->is_set(*it) ? *it : std::numeric_limits<uint32_t>::max();
  }
}

template <typename NullOp>
inline PERFETTO_ALWAYS_INLINE void NullFilter(InterpreterState& state,
                                              const NullFilterBase& filter) {
  using B = NullFilterBase;
  const BitVector* null_bv =
      state.ReadFromRegister(filter.arg<B::null_bv_register>());
  auto& update = state.ReadFromRegister(filter.arg<B::update_register>());
  static constexpr bool kInvert = std::is_same_v<NullOp, IsNull>;
  update.e = null_bv->template PackLeft<kInvert>(update.b, update.e, update.b);
}

// Handles conversion of strings or nulls to integer or double types for
// filtering operations.
template <typename FilterValueFetcherImpl>
inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastStringOrNullFilterValueToIntegerOrDouble(
    typename FilterValueFetcherImpl::Type filter_value_type,
    NonStringOp op) {
  if (filter_value_type == FilterValueFetcherImpl::kString) {
    if (op.index() == NonStringOp::GetTypeIndex<Eq>() ||
        op.index() == NonStringOp::GetTypeIndex<Ge>() ||
        op.index() == NonStringOp::GetTypeIndex<Gt>()) {
      return CastFilterValueResult::kNoneMatch;
    }
    PERFETTO_DCHECK(op.index() == NonStringOp::GetTypeIndex<Ne>() ||
                    op.index() == NonStringOp::GetTypeIndex<Le>() ||
                    op.index() == NonStringOp::GetTypeIndex<Lt>());
    return CastFilterValueResult::kAllMatch;
  }

  PERFETTO_DCHECK(filter_value_type == FilterValueFetcherImpl::kNull);

  // Nulls always compare false to any value (including other nulls),
  // regardless of the operator.
  return CastFilterValueResult::kNoneMatch;
}

// Converts a double to an integer type using the specified function
// (e.g., trunc, floor). Used as a helper for various casting operations.
template <typename T, double (*fn)(double)>
inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastDoubleToIntHelper(bool no_data, bool all_data, double d, T& out) {
  if (no_data) {
    return CastFilterValueResult::kNoneMatch;
  }
  if (all_data) {
    return CastFilterValueResult::kAllMatch;
  }
  out = static_cast<T>(fn(d));
  return CastFilterValueResult::kValid;
}

// Attempts to cast a filter value to an integer type, handling various
// edge cases such as out-of-range values and non-integer inputs.
template <typename T, typename FilterValueFetcherImpl>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastFilterValueToInteger(
    FilterValueHandle handle,
    typename FilterValueFetcherImpl::Type filter_value_type,
    FilterValueFetcherImpl& fetcher,
    NonStringOp op,
    T& out) {
  static_assert(std::is_integral_v<T>, "Unsupported type");

  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kInt64)) {
    int64_t res = fetcher.GetInt64Value(handle.index);
    bool is_small = res < std::numeric_limits<T>::min();
    bool is_big = res > std::numeric_limits<T>::max();
    if (PERFETTO_UNLIKELY(is_small || is_big)) {
      switch (op.index()) {
        case NonStringOp::GetTypeIndex<Lt>():
        case NonStringOp::GetTypeIndex<Le>():
          if (is_small) {
            return CastFilterValueResult::kNoneMatch;
          }
          break;
        case NonStringOp::GetTypeIndex<Gt>():
        case NonStringOp::GetTypeIndex<Ge>():
          if (is_big) {
            return CastFilterValueResult::kNoneMatch;
          }
          break;
        case NonStringOp::GetTypeIndex<Eq>():
          return CastFilterValueResult::kNoneMatch;
        case NonStringOp::GetTypeIndex<Ne>():
          // Do nothing.
          break;
        default:
          PERFETTO_FATAL("Invalid numeric filter op");
      }
      return CastFilterValueResult::kAllMatch;
    }
    out = static_cast<T>(res);
    return CastFilterValueResult::kValid;
  }
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kDouble)) {
    double d = fetcher.GetDoubleValue(handle.index);

    // We use the constants directly instead of using numeric_limits for
    // int64_t as the casts introduces rounding in the doubles as a double
    // cannot exactly represent int64::max().
    constexpr double kMin =
        std::is_same_v<T, int64_t>
            ? -9223372036854775808.0
            : static_cast<double>(std::numeric_limits<T>::min());
    constexpr double kMax =
        std::is_same_v<T, int64_t>
            ? 9223372036854775808.0
            : static_cast<double>(std::numeric_limits<T>::max());

    // NaNs always compare false to any value (including other NaNs),
    // regardless of the operator.
    if (PERFETTO_UNLIKELY(std::isnan(d))) {
      return CastFilterValueResult::kNoneMatch;
    }

    // The greater than or equal is intentional to account for the fact
    // that twos-complement integers are not symmetric around zero (i.e.
    // -9223372036854775808 can be represented but 9223372036854775808
    // cannot).
    bool is_big = d >= kMax;
    bool is_small = d < kMin;
    if (PERFETTO_LIKELY(d == trunc(d) && !is_small && !is_big)) {
      out = static_cast<T>(d);
      return CastFilterValueResult::kValid;
    }
    switch (op.index()) {
      case NonStringOp::GetTypeIndex<Lt>():
        return CastDoubleToIntHelper<T, std::ceil>(is_small, is_big, d, out);
      case NonStringOp::GetTypeIndex<Le>():
        return CastDoubleToIntHelper<T, std::floor>(is_small, is_big, d, out);
      case NonStringOp::GetTypeIndex<Gt>():
        return CastDoubleToIntHelper<T, std::floor>(is_big, is_small, d, out);
      case NonStringOp::GetTypeIndex<Ge>():
        return CastDoubleToIntHelper<T, std::ceil>(is_big, is_small, d, out);
      case NonStringOp::GetTypeIndex<Eq>():
        return CastFilterValueResult::kNoneMatch;
      case NonStringOp::GetTypeIndex<Ne>():
        // Do nothing.
        return CastFilterValueResult::kAllMatch;
      default:
        PERFETTO_FATAL("Invalid numeric filter op");
    }
  }
  return CastStringOrNullFilterValueToIntegerOrDouble<FilterValueFetcherImpl>(
      filter_value_type, op);
}

// Attempts to cast a filter value to a double, handling integer inputs
// and various edge cases.
template <typename FilterValueFetcherImpl>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastFilterValueToDouble(FilterValueHandle filter_value_handle,
                        typename FilterValueFetcherImpl::Type filter_value_type,
                        FilterValueFetcherImpl& fetcher,
                        NonStringOp op,
                        double& out) {
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kDouble)) {
    out = fetcher.GetDoubleValue(filter_value_handle.index);
    return CastFilterValueResult::kValid;
  }
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kInt64)) {
    int64_t i = fetcher.GetInt64Value(filter_value_handle.index);
    auto iad = static_cast<double>(i);
    auto iad_int = static_cast<int64_t>(iad);

    // If the integer value can be converted to a double while preserving
    // the exact integer value, then we can use the double value for
    // comparison.
    if (PERFETTO_LIKELY(i == iad_int)) {
      out = iad;
      return CastFilterValueResult::kValid;
    }

    // This can happen in cases where we round `i` up above
    // numeric_limits::max(). In that case, still consider the double
    // larger.
    bool overflow_positive_to_negative = i > 0 && iad_int < 0;
    bool iad_greater_than_i = iad_int > i || overflow_positive_to_negative;
    bool iad_less_than_i = iad_int < i && !overflow_positive_to_negative;
    switch (op.index()) {
      case NonStringOp::GetTypeIndex<Lt>():
        out =
            iad_greater_than_i
                ? iad
                : std::nextafter(iad, std::numeric_limits<double>::infinity());
        return CastFilterValueResult::kValid;
      case NonStringOp::GetTypeIndex<Le>():
        out =
            iad_less_than_i
                ? iad
                : std::nextafter(iad, -std::numeric_limits<double>::infinity());
        return CastFilterValueResult::kValid;
      case NonStringOp::GetTypeIndex<Gt>():
        out =
            iad_less_than_i
                ? iad
                : std::nextafter(iad, -std::numeric_limits<double>::infinity());
        return CastFilterValueResult::kValid;
      case NonStringOp::GetTypeIndex<Ge>():
        out =
            iad_greater_than_i
                ? iad
                : std::nextafter(iad, std::numeric_limits<double>::infinity());
        return CastFilterValueResult::kValid;
      case NonStringOp::GetTypeIndex<Eq>():
        return CastFilterValueResult::kNoneMatch;
      case NonStringOp::GetTypeIndex<Ne>():
        // Do nothing.
        return CastFilterValueResult::kAllMatch;
      default:
        PERFETTO_FATAL("Invalid numeric filter op");
    }
  }
  return CastStringOrNullFilterValueToIntegerOrDouble<FilterValueFetcherImpl>(
      filter_value_type, op);
}

// Attempts to cast a filter value to a numeric type, dispatching to the
// appropriate type-specific conversion function.
template <typename T, typename FilterValueFetcherImpl>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastFilterValueToIntegerOrDouble(
    FilterValueHandle handle,
    typename FilterValueFetcherImpl::Type filter_value_type,
    FilterValueFetcherImpl& fetcher,
    NonStringOp op,
    T& out) {
  if constexpr (std::is_same_v<T, double>) {
    return CastFilterValueToDouble(handle, filter_value_type, fetcher, op, out);
  } else if constexpr (std::is_integral_v<T>) {
    return CastFilterValueToInteger<T>(handle, filter_value_type, fetcher, op,
                                       out);
  } else {
    static_assert(std::is_same_v<T, double>, "Unsupported type");
  }
}

template <typename FilterValueFetcherImpl>
inline PERFETTO_ALWAYS_INLINE CastFilterValueResult::Validity
CastFilterValueToString(FilterValueHandle handle,
                        typename FilterValueFetcherImpl::Type filter_value_type,
                        FilterValueFetcherImpl& fetcher,
                        const StringOp& op,
                        const char*& out) {
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kString)) {
    out = fetcher.GetStringValue(handle.index);
    return CastFilterValueResult::kValid;
  }
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kNull)) {
    // Nulls always compare false to any value (including other nulls),
    // regardless of the operator.
    return CastFilterValueResult::kNoneMatch;
  }
  if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl::kInt64 ||
                      filter_value_type == FilterValueFetcherImpl::kDouble)) {
    switch (op.index()) {
      case Op::GetTypeIndex<Ge>():
      case Op::GetTypeIndex<Gt>():
      case Op::GetTypeIndex<Ne>():
        return CastFilterValueResult::kAllMatch;
      case Op::GetTypeIndex<Eq>():
      case Op::GetTypeIndex<Le>():
      case Op::GetTypeIndex<Lt>():
      case Op::GetTypeIndex<Glob>():
      case Op::GetTypeIndex<Regex>():
        return CastFilterValueResult::kNoneMatch;
      default:
        PERFETTO_FATAL("Invalid string filter op");
    }
  }
  PERFETTO_FATAL("Invalid filter spec value");
}

// Attempts to cast a filter value to the specified type and stores the
// result. Currently only supports casting to Id type.
template <typename T, typename FilterValueFetcherImpl>
inline PERFETTO_ALWAYS_INLINE void CastFilterValue(
    InterpreterState& state,
    FilterValueFetcherImpl& fetcher,
    const CastFilterValueBase& f) {
  using B = CastFilterValueBase;
  FilterValueHandle handle = f.arg<B::fval_handle>();
  typename FilterValueFetcherImpl::Type filter_value_type =
      fetcher.GetValueType(handle.index);

  using ValueType =
      StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
  CastFilterValueResult result;
  if constexpr (std::is_same_v<T, Id>) {
    auto op = *f.arg<B::op>().TryDowncast<NonStringOp>();
    uint32_t result_value;
    result.validity =
        CastFilterValueToInteger<uint32_t, FilterValueFetcherImpl>(
            handle, filter_value_type, fetcher, op, result_value);
    if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
      result.value = CastFilterValueResult::Id{result_value};
    }
  } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
    auto op = *f.arg<B::op>().TryDowncast<NonStringOp>();
    ValueType result_value;
    result.validity =
        CastFilterValueToIntegerOrDouble<ValueType, FilterValueFetcherImpl>(
            handle, filter_value_type, fetcher, op, result_value);
    if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
      result.value = result_value;
    }
  } else if constexpr (std::is_same_v<T, String>) {
    static_assert(std::is_same_v<ValueType, const char*>);
    auto op = *f.arg<B::op>().TryDowncast<StringOp>();
    const char* result_value;
    result.validity = CastFilterValueToString<FilterValueFetcherImpl>(
        handle, filter_value_type, fetcher, op, result_value);
    if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
      result.value = result_value;
    }
  } else {
    static_assert(std::is_same_v<T, Id>, "Unsupported type");
  }
  state.WriteToRegister(f.arg<B::write_register>(), result);
}

template <typename T, typename FilterValueFetcherImpl>
inline PERFETTO_ALWAYS_INLINE void CastFilterValueList(
    InterpreterState& state,
    FilterValueFetcherImpl& fetcher,
    const CastFilterValueListBase& c) {
  using B = CastFilterValueListBase;
  FilterValueHandle handle = c.arg<B::fval_handle>();
  using ValueType =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::Value>;
  FlexVector<ValueType> results;
  bool all_match = false;
  for (bool has_more = fetcher.IteratorInit(handle.index); has_more;
       has_more = fetcher.IteratorNext(handle.index)) {
    typename FilterValueFetcherImpl::Type filter_value_type =
        fetcher.GetValueType(handle.index);
    if constexpr (std::is_same_v<T, Id>) {
      auto op = *c.arg<B::op>().TryDowncast<NonStringOp>();
      uint32_t result_value;
      auto validity =
          CastFilterValueToInteger<uint32_t, FilterValueFetcherImpl>(
              handle, filter_value_type, fetcher, op, result_value);
      if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
        results.push_back(CastFilterValueResult::Id{result_value});
      } else if (validity == CastFilterValueResult::kAllMatch) {
        all_match = true;
        break;
      }
    } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
      auto op = *c.arg<B::op>().TryDowncast<NonStringOp>();
      ValueType result_value;
      auto validity =
          CastFilterValueToIntegerOrDouble<ValueType, FilterValueFetcherImpl>(
              handle, filter_value_type, fetcher, op, result_value);
      if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
        results.push_back(result_value);
      } else if (validity == CastFilterValueResult::kAllMatch) {
        all_match = true;
        break;
      }
    } else if constexpr (std::is_same_v<T, String>) {
      static_assert(std::is_same_v<ValueType, StringPool::Id>);
      auto op = *c.arg<B::op>().TryDowncast<StringOp>();
      // We only support equality checks for strings in this context. This is
      // because mapping to StringPool::Id could not possibly work for
      // non-equality checks.
      PERFETTO_CHECK(op.Is<Eq>());
      const char* result_value;
      auto validity = CastFilterValueToString<FilterValueFetcherImpl>(
          handle, filter_value_type, fetcher, op, result_value);
      if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
        auto id = state.string_pool->GetId(result_value);
        if (id) {
          results.push_back(*id);
        } else {
          // Because we only support equality, we know for sure that nothing
          // matches this value.
        }
      } else if (validity == CastFilterValueResult::kAllMatch) {
        all_match = true;
        break;
      }
    } else {
      static_assert(std::is_same_v<T, Id>, "Unsupported type");
    }
  }
  CastFilterValueListResult result;
  if (all_match) {
    result.validity = CastFilterValueResult::kAllMatch;
  } else if (results.empty()) {
    result.validity = CastFilterValueResult::kNoneMatch;
  } else {
    result.validity = CastFilterValueResult::kValid;
    result.value_list = std::move(results);
  }
  state.WriteToRegister(c.arg<B::write_register>(), std::move(result));
}

template <typename T, typename Op>
inline PERFETTO_ALWAYS_INLINE void NonStringFilter(
    InterpreterState& state,
    const ::perfetto::trace_processor::core::interpreter::NonStringFilter<T,
                                                                          Op>&
        nf) {
  using B =
      ::perfetto::trace_processor::core::interpreter::NonStringFilter<T, Op>;
  const auto& value =
      state.ReadFromRegister(nf.template arg<B::val_register>());
  auto& update = state.ReadFromRegister(nf.template arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(value.validity, update)) {
    return;
  }
  const auto& source =
      state.ReadFromRegister(nf.template arg<B::source_register>());
  using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
  if constexpr (std::is_same_v<T, Id>) {
    update.e = IdentityFilter(
        source.b, source.e, update.b, base::unchecked_get<M>(value.value).value,
        comparators::IntegerOrDoubleComparator<uint32_t, Op>());
  } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
    const auto* data = state.ReadStorageFromRegister<T>(
        nf.template arg<B::storage_register>());
    update.e = Filter(data, source.b, source.e, update.b,
                      base::unchecked_get<M>(value.value),
                      comparators::IntegerOrDoubleComparator<M, Op>());
  } else {
    static_assert(std::is_same_v<T, Id>, "Unsupported type");
  }
}

inline PERFETTO_ALWAYS_INLINE uint32_t* StringFilterEq(
    const InterpreterState& state,
    const StringPool::Id* data,
    const uint32_t* begin,
    const uint32_t* end,
    uint32_t* output,
    const char* val) {
  std::optional<StringPool::Id> id =
      state.string_pool->GetId(base::StringView(val));
  if (!id) {
    return output;
  }
  static_assert(sizeof(StringPool::Id) == 4, "Id should be 4 bytes");
  return Filter(reinterpret_cast<const uint32_t*>(data), begin, end, output,
                id->raw_id(), std::equal_to<>());
}

inline PERFETTO_ALWAYS_INLINE uint32_t* StringFilterNe(
    const InterpreterState& state,
    const StringPool::Id* data,
    const uint32_t* begin,
    const uint32_t* end,
    uint32_t* output,
    const char* val) {
  std::optional<StringPool::Id> id =
      state.string_pool->GetId(base::StringView(val));
  if (!id) {
    return output + (end - begin);
  }
  static_assert(sizeof(StringPool::Id) == 4, "Id should be 4 bytes");
  return Filter(reinterpret_cast<const uint32_t*>(data), begin, end, output,
                id->raw_id(), std::not_equal_to<>());
}

template <typename Op>
inline PERFETTO_ALWAYS_INLINE uint32_t* FilterStringOp(
    const InterpreterState& state,
    const StringPool::Id* data,
    const uint32_t* begin,
    const uint32_t* end,
    uint32_t* output,
    const char* val) {
  if constexpr (std::is_same_v<Op, Eq>) {
    return StringFilterEq(state, data, begin, end, output, val);
  } else if constexpr (std::is_same_v<Op, Ne>) {
    return StringFilterNe(state, data, begin, end, output, val);
  } else if constexpr (std::is_same_v<Op, Glob>) {
    return StringFilterGlobImpl(state.string_pool, data, val, begin, end,
                                output);
  } else if constexpr (std::is_same_v<Op, Regex>) {
    return StringFilterRegexImpl(state.string_pool, data, val, begin, end,
                                 output);
  } else {
    return Filter(data, begin, end, output, NullTermStringView(val),
                  comparators::StringComparator<Op>{state.string_pool});
  }
}

template <typename Op>
inline PERFETTO_ALWAYS_INLINE void StringFilter(InterpreterState& state,
                                                const StringFilterBase& sf) {
  using B = StringFilterBase;
  const auto& filter_value = state.ReadFromRegister(sf.arg<B::val_register>());
  auto& update = state.ReadFromRegister(sf.arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(filter_value.validity, update)) {
    return;
  }
  const char* val = base::unchecked_get<const char*>(filter_value.value);
  const auto& source = state.ReadFromRegister(sf.arg<B::source_register>());
  const StringPool::Id* ptr =
      state.ReadStorageFromRegister<String>(sf.arg<B::storage_register>());
  update.e = FilterStringOp<Op>(state, ptr, source.b, source.e, update.b, val);
}

template <typename DataType>
inline auto GetLbComprarator(const InterpreterState& state) {
  if constexpr (std::is_same_v<DataType, StringPool::Id>) {
    return comparators::StringComparator<Lt>{state.string_pool};
  } else {
    return std::less<>();
  }
}

template <typename DataType>
inline auto GetUbComparator(const InterpreterState& state) {
  if constexpr (std::is_same_v<DataType, StringPool::Id>) {
    return comparators::StringLessInvert{state.string_pool};
  } else {
    return std::less<>();
  }
}

template <typename RangeOp, typename DataType, typename ValueType>
inline PERFETTO_ALWAYS_INLINE void NonIdSortedFilter(
    const InterpreterState& state,
    const DataType* data,
    ValueType val,
    BoundModifier bound_modifier,
    Range& update) {
  auto* begin = data + update.b;
  auto* end = data + update.e;
  if constexpr (std::is_same_v<RangeOp, EqualRange>) {
    PERFETTO_DCHECK(bound_modifier.Is<BothBounds>());
    DataType cmp_value;
    if constexpr (std::is_same_v<DataType, StringPool::Id>) {
      std::optional<StringPool::Id> id =
          state.string_pool->GetId(base::StringView(val));
      if (!id) {
        update.e = update.b;
        return;
      }
      cmp_value = *id;
    } else {
      cmp_value = val;
    }
    const DataType* eq_start =
        std::lower_bound(begin, end, val, GetLbComprarator<DataType>(state));
    const DataType* eq_end = eq_start;

    // Scan 16 rows: it's often the case that we have just a very small number
    // of equal rows, so we can avoid a binary search.
    const DataType* eq_end_limit = eq_start + 16;
    for (;; ++eq_end) {
      if (eq_end == end) {
        break;
      }
      if (eq_end == eq_end_limit) {
        eq_end = std::upper_bound(eq_start, end, val,
                                  GetUbComparator<DataType>(state));
        break;
      }
      if (std::not_equal_to<>()(*eq_end, cmp_value)) {
        break;
      }
    }
    update.b = static_cast<uint32_t>(eq_start - data);
    update.e = static_cast<uint32_t>(eq_end - data);
  } else if constexpr (std::is_same_v<RangeOp, LowerBound>) {
    auto& res = bound_modifier.Is<BeginBound>() ? update.b : update.e;
    res = static_cast<uint32_t>(
        std::lower_bound(begin, end, val, GetLbComprarator<DataType>(state)) -
        data);
  } else if constexpr (std::is_same_v<RangeOp, UpperBound>) {
    auto& res = bound_modifier.Is<BeginBound>() ? update.b : update.e;
    res = static_cast<uint32_t>(
        std::upper_bound(begin, end, val, GetUbComparator<DataType>(state)) -
        data);
  } else {
    static_assert(std::is_same_v<RangeOp, EqualRange>, "Unsupported op");
  }
}

template <typename T, typename RangeOp>
inline PERFETTO_ALWAYS_INLINE void SortedFilter(
    InterpreterState& state,
    const ::perfetto::trace_processor::core::interpreter::SortedFilter<T,
                                                                       RangeOp>&
        f) {
  using B =
      ::perfetto::trace_processor::core::interpreter::SortedFilter<T, RangeOp>;

  const auto& value = state.ReadFromRegister(f.template arg<B::val_register>());
  Range& update = state.ReadFromRegister(f.template arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(value.validity, update)) {
    return;
  }
  using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
  M val = base::unchecked_get<M>(value.value);
  if constexpr (std::is_same_v<T, Id>) {
    uint32_t inner_val = val.value;
    if constexpr (std::is_same_v<RangeOp, EqualRange>) {
      bool in_bounds = inner_val >= update.b && inner_val < update.e;
      update.b = inner_val;
      update.e = inner_val + in_bounds;
    } else if constexpr (std::is_same_v<RangeOp, LowerBound> ||
                         std::is_same_v<RangeOp, UpperBound>) {
      BoundModifier bound_to_modify = f.template arg<B::write_result_to>();

      uint32_t effective_val = inner_val + std::is_same_v<RangeOp, UpperBound>;
      bool is_begin_bound = bound_to_modify.template Is<BeginBound>();

      uint32_t new_b =
          is_begin_bound ? std::max(update.b, effective_val) : update.b;
      uint32_t new_e =
          !is_begin_bound ? std::min(update.e, effective_val) : update.e;

      update.b = new_b;
      update.e = std::max(new_b, new_e);
    } else {
      static_assert(std::is_same_v<RangeOp, EqualRange>, "Unsupported op");
    }
  } else {
    BoundModifier bound_modifier = f.template arg<B::write_result_to>();
    const auto* data =
        state.ReadStorageFromRegister<T>(f.template arg<B::storage_register>());
    NonIdSortedFilter<RangeOp>(state, data, val, bound_modifier, update);
  }
}

template <typename T, typename M, typename D>
inline PERFETTO_ALWAYS_INLINE bool InBitVector(const FlexVector<M>& val,
                                               const D* data,
                                               const Span<uint32_t>& source,
                                               Span<uint32_t>& update) {
  uint32_t max = 0;
  for (size_t i = 0; i < val.size(); ++i) {
    if constexpr (std::is_same_v<T, Id>) {
      max = std::max(max, val[i].value);
    } else {
      max = std::max(max, val[i]);
    }
  }
  // If the bitvector is too sparse, don't waste memory on it.
  if (max > val.size() * 16) {
    return false;
  }
  BitVector bv = BitVector::CreateWithSize(max + 1, false);
  for (size_t i = 0; i < val.size(); ++i) {
    if constexpr (std::is_same_v<T, Id>) {
      bv.set(val[i].value);
    } else {
      bv.set(val[i]);
    }
  }
  struct InBitVectorCmp {
    PERFETTO_ALWAYS_INLINE bool operator()(uint32_t lhs,
                                           const BitVector& bv_arg) const {
      return lhs < bv_arg.size() && bv_arg.is_set(lhs);
    }
  };
  if constexpr (std::is_same_v<T, Id>) {
    base::ignore_result(data);
    update.e =
        IdentityFilter(source.b, source.e, update.b, bv, InBitVectorCmp());
  } else {
    update.e = Filter(data, source.b, source.e, update.b, bv, InBitVectorCmp());
  }
  return true;
}

template <typename T>
inline PERFETTO_ALWAYS_INLINE void In(
    InterpreterState& state,
    const ::perfetto::trace_processor::core::interpreter::In<T>& f) {
  using B = ::perfetto::trace_processor::core::interpreter::In<T>;
  const auto& value =
      state.ReadFromRegister(f.template arg<B::value_list_register>());
  const Span<uint32_t>& source =
      state.ReadFromRegister(f.template arg<B::source_register>());
  Span<uint32_t>& update =
      state.ReadFromRegister(f.template arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(value.validity, update)) {
    return;
  }
  using M =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::ValueList>;
  const M& val = base::unchecked_get<M>(value.value_list);

  // Try to use a bitvector if the value is an Id or uint32_t.
  // This is a performance optimization to avoid iterating over the
  // FlexVector for large lists of values.
  if constexpr (std::is_same_v<T, Id> || std::is_same_v<T, Uint32>) {
    const auto* data =
        state.ReadStorageFromRegister<T>(f.template arg<B::storage_register>());
    if (InBitVector<T>(val, data, source, update)) {
      return;
    }
  }
  if constexpr (std::is_same_v<T, Id>) {
    struct Comparator {
      bool operator()(uint32_t lhs,
                      const FlexVector<CastFilterValueResult::Id>& rhs) const {
        for (const auto& r : rhs) {
          if (lhs == r.value) {
            return true;
          }
        }
        return false;
      }
    };
    update.e = IdentityFilter(source.b, source.e, update.b, val, Comparator());
  } else {
    const auto* data =
        state.ReadStorageFromRegister<T>(f.template arg<B::storage_register>());
    using D = std::remove_cv_t<std::remove_reference_t<decltype(*data)>>;
    struct Comparator {
      bool operator()(D lhs, const FlexVector<D>& rhs) const {
        for (const auto& r : rhs) {
          if (std::equal_to<>()(lhs, r)) {
            return true;
          }
        }
        return false;
      }
    };
    update.e = Filter(data, source.b, source.e, update.b, val, Comparator());
  }
}

template <typename T>
inline PERFETTO_ALWAYS_INLINE void LinearFilterEq(
    InterpreterState& state,
    const ::perfetto::trace_processor::core::interpreter::LinearFilterEq<T>&
        leq) {
  using B = ::perfetto::trace_processor::core::interpreter::LinearFilterEq<T>;

  Span<uint32_t>& span =
      state.ReadFromRegister(leq.template arg<B::update_register>());
  Range range = state.ReadFromRegister(leq.template arg<B::source_register>());
  PERFETTO_DCHECK(range.size() <= span.size());

  const auto& res =
      state.ReadFromRegister(leq.template arg<B::filter_value_reg>());
  if (!HandleInvalidCastFilterValueResult(res.validity, range)) {
    std::iota(span.b, span.b + range.size(), range.b);
    span.e = span.b + range.size();
    return;
  }

  const auto* data =
      state.ReadStorageFromRegister<T>(leq.template arg<B::storage_register>());

  using Compare = std::remove_cv_t<std::remove_reference_t<decltype(*data)>>;
  using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
  const auto& value = base::unchecked_get<M>(res.value);
  Compare to_compare;
  if constexpr (std::is_same_v<T, String>) {
    auto id = state.string_pool->GetId(value);
    if (!id) {
      span.e = span.b;
      return;
    }
    to_compare = *id;
  } else {
    to_compare = value;
  }

  // Note to future readers: this can be optimized further with explicit SIMD
  // but the compiler does a pretty good job even without it. For context,
  // we're talking about query changing from 2s -> 1.6s on a 12m row table.
  uint32_t* o_write = span.b;
  for (uint32_t i = range.b; i < range.e; ++i) {
    if (std::equal_to<>()(data[i], to_compare)) {
      *o_write++ = i;
    }
  }
  span.e = o_write;
}

template <typename N>
inline PERFETTO_ALWAYS_INLINE uint32_t
IndexToStorageIndex(uint32_t index,
                    const BitVector* const* bv_ptr,
                    const Slab<uint32_t>* popcnt) {
  if constexpr (std::is_same_v<N, NonNull>) {
    base::ignore_result(bv_ptr, popcnt);
    return index;
  } else if constexpr (std::is_same_v<N, SparseNull>) {
    const BitVector* bv = *bv_ptr;
    if (!bv->is_set(index)) {
      // Null values are always less than non-null values.
      return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>((*popcnt)[index / 64] +
                                 bv->count_set_bits_until_in_word(index));
  } else if constexpr (std::is_same_v<N, DenseNull>) {
    const BitVector* bv = *bv_ptr;
    base::ignore_result(popcnt);
    return bv->is_set(index) ? index : std::numeric_limits<uint32_t>::max();
  } else {
    static_assert(std::is_same_v<N, NonNull>, "Unsupported type");
  }
}

template <typename T, typename N>
inline PERFETTO_ALWAYS_INLINE void IndexedFilterEq(
    InterpreterState& state,
    const IndexedFilterEqBase& bytecode) {
  using B = IndexedFilterEqBase;
  const auto& filter_value =
      state.ReadFromRegister(bytecode.arg<B::filter_value_reg>());
  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  Span<uint32_t> dest(source.b, source.e);
  if (!HandleInvalidCastFilterValueResult(filter_value.validity, dest)) {
    state.WriteToRegister(bytecode.arg<B::dest_register>(), dest);
    return;
  }
  using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
  const auto& value = base::unchecked_get<M>(filter_value.value);
  const auto* data =
      state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());
  const Slab<uint32_t>* popcnt =
      state.MaybeReadFromRegister(bytecode.arg<B::popcount_register>());
  const BitVector* const* null_bv =
      state.MaybeReadFromRegister(bytecode.arg<B::null_bv_register>());
  dest.b = std::lower_bound(
      source.b, source.e, value, [&](uint32_t index, const M& val_arg) {
        uint32_t storage_idx = IndexToStorageIndex<N>(index, null_bv, popcnt);
        if (storage_idx == std::numeric_limits<uint32_t>::max()) {
          return true;
        }
        if constexpr (std::is_same_v<T, String>) {
          return state.string_pool->Get(data[storage_idx]) < val_arg;
        } else {
          return data[storage_idx] < val_arg;
        }
      });
  dest.e = std::upper_bound(
      dest.b, source.e, value, [&](const M& val_arg, uint32_t index) {
        uint32_t storage_idx = IndexToStorageIndex<N>(index, null_bv, popcnt);
        if (storage_idx == std::numeric_limits<uint32_t>::max()) {
          return false;
        }
        if constexpr (std::is_same_v<T, String>) {
          return val_arg < state.string_pool->Get(data[storage_idx]);
        } else {
          return val_arg < data[storage_idx];
        }
      });
  state.WriteToRegister(bytecode.arg<B::dest_register>(), dest);
}

inline PERFETTO_ALWAYS_INLINE void Uint32SetIdSortedEq(
    InterpreterState& state,
    const Uint32SetIdSortedEq& bytecode) {
  using B = struct Uint32SetIdSortedEq;

  const CastFilterValueResult& cast_result =
      state.ReadFromRegister(bytecode.arg<B::val_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(cast_result.validity, update)) {
    return;
  }
  using ValueType =
      StorageType::VariantTypeAtIndex<Uint32, CastFilterValueResult::Value>;
  auto val = base::unchecked_get<ValueType>(cast_result.value);
  const auto* storage = state.ReadStorageFromRegister<Uint32>(
      bytecode.arg<B::storage_register>());
  const auto* start =
      std::clamp(storage + val, storage + update.b, storage + update.e);

  update.b = static_cast<uint32_t>(start - storage);
  const auto* it = start;
  for (; it != storage + update.e; ++it) {
    if (*it != val) {
      break;
    }
  }
  update.e = static_cast<uint32_t>(it - storage);
}

inline PERFETTO_ALWAYS_INLINE void SpecializedStorageSmallValueEq(
    InterpreterState& state,
    const SpecializedStorageSmallValueEq& bytecode) {
  using B = struct SpecializedStorageSmallValueEq;

  const CastFilterValueResult& cast_result =
      state.ReadFromRegister(bytecode.arg<B::val_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  if (!HandleInvalidCastFilterValueResult(cast_result.validity, update)) {
    return;
  }
  using ValueType =
      StorageType::VariantTypeAtIndex<Uint32, CastFilterValueResult::Value>;
  auto val = base::unchecked_get<ValueType>(cast_result.value);
  const BitVector* bv =
      state.ReadFromRegister(bytecode.arg<B::small_value_bv_register>());
  const Span<const uint32_t>& popcount =
      state.ReadFromRegister(bytecode.arg<B::small_value_popcount_register>());

  uint32_t k =
      val < bv->size() && bv->is_set(val)
          ? static_cast<uint32_t>(popcount.b[val / 64] +
                                  bv->count_set_bits_until_in_word(val))
          : update.e;
  bool in_bounds = update.b <= k && k < update.e;
  update.b = in_bounds ? k : update.e;
  update.e = in_bounds ? k + 1 : update.b;
}

template <typename T, typename Nullability>
inline PERFETTO_ALWAYS_INLINE void CopyToRowLayout(
    InterpreterState& state,
    const CopyToRowLayoutBase& bytecode) {
  using B = CopyToRowLayoutBase;

  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_indices_register>());
  bool invert = bytecode.arg<B::invert_copied_bits>();

  auto& dest_buffer =
      state.ReadFromRegister(bytecode.arg<B::dest_buffer_register>());
  uint8_t* dest = dest_buffer.data() + bytecode.arg<B::row_layout_offset>();
  uint32_t stride = bytecode.arg<B::row_layout_stride>();

  const auto* popcount_slab = state.MaybeReadFromRegister<Slab<uint32_t>>(
      bytecode.arg<B::popcount_register>());
  const auto* data =
      state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());

  // GCC complains that these variables are not used in the NonNull branches.
  [[maybe_unused]] const StringIdToRankMap* rank_map_ptr =
      state.MaybeReadFromRegister(bytecode.arg<B::rank_map_register>());
  [[maybe_unused]] const BitVector* const* null_bv_ptr =
      state.MaybeReadFromRegister(bytecode.arg<B::null_bv_register>());
  for (uint32_t* ptr = source.b; ptr != source.e; ++ptr) {
    uint32_t table_index = *ptr;
    uint32_t storage_index;
    bool is_non_null;
    uint32_t offset;
    if constexpr (std::is_same_v<Nullability, NonNull>) {
      is_non_null = true;
      storage_index = table_index;
      offset = 0;
    } else if constexpr (std::is_same_v<Nullability, SparseNull>) {
      PERFETTO_DCHECK(popcount_slab);
      const BitVector* null_bv = *null_bv_ptr;
      is_non_null = null_bv->is_set(table_index);
      storage_index = is_non_null
                          ? static_cast<uint32_t>(
                                (*popcount_slab)[*ptr / 64] +
                                null_bv->count_set_bits_until_in_word(*ptr))
                          : std::numeric_limits<uint32_t>::max();
      uint8_t res = is_non_null ? 0xFF : 0;
      *dest = invert ? ~res : res;
      offset = 1;
    } else if constexpr (std::is_same_v<Nullability, DenseNull>) {
      const BitVector* null_bv = *null_bv_ptr;
      is_non_null = null_bv->is_set(table_index);
      storage_index = table_index;
      uint8_t res = is_non_null ? 0xFF : 0;
      *dest = invert ? ~res : res;
      offset = 1;
    } else {
      static_assert(std::is_same_v<Nullability, NonNull>,
                    "Unsupported Nullability type");
    }
    if constexpr (std::is_same_v<T, Id>) {
      if (is_non_null) {
        uint32_t res = GetComparableRowLayoutRepr(storage_index);
        res = invert ? ~res : res;
        memcpy(dest + offset, &res, sizeof(uint32_t));
      } else {
        memset(dest + offset, 0, sizeof(uint32_t));
      }
    } else if constexpr (std::is_same_v<T, String>) {
      if (is_non_null) {
        uint32_t res;
        if (rank_map_ptr) {
          auto* rank = (*rank_map_ptr)->Find(data[storage_index]);
          PERFETTO_DCHECK(rank);
          res = GetComparableRowLayoutRepr(*rank);
        } else {
          res = GetComparableRowLayoutRepr(data[storage_index].raw_id());
        }
        res = invert ? ~res : res;
        memcpy(dest + offset, &res, sizeof(uint32_t));
      } else {
        memset(dest + offset, 0, sizeof(uint32_t));
      }
    } else {
      if (is_non_null) {
        auto res = GetComparableRowLayoutRepr(data[storage_index]);
        res = invert ? ~res : res;
        memcpy(dest + offset, &res, sizeof(res));
      } else {
        memset(dest + offset, 0, sizeof(decltype(*data)));
      }
    }
    dest += stride;
  }
}

template <typename T, typename Op>
inline PERFETTO_ALWAYS_INLINE void FindMinMaxIndex(
    InterpreterState& state,
    const FindMinMaxIndex<T, Op>& bytecode) {
  using B = FindMinMaxIndexBase;
  auto& indices =
      state.ReadFromRegister(bytecode.template arg<B::update_register>());
  if (indices.empty()) {
    return;
  }

  const auto* data = state.ReadStorageFromRegister<T>(
      bytecode.template arg<B::storage_register>());
  auto get_value = [&](uint32_t idx) {
    if constexpr (std::is_same_v<T, Id>) {
      base::ignore_result(data);
      return idx;
    } else if constexpr (std::is_same_v<T, String>) {
      return state.string_pool->Get(data[idx]);
    } else {
      return data[idx];
    }
  };
  uint32_t best_idx = *indices.b;
  auto best_val = get_value(best_idx);
  for (const uint32_t* it = indices.b + 1; it != indices.e; ++it) {
    uint32_t current_idx = *it;
    auto current_val = get_value(current_idx);
    bool current_is_better;
    if constexpr (std::is_same_v<Op, MinOp>) {
      current_is_better = current_val < best_val;
    } else {
      current_is_better = current_val > best_val;
    }
    if (current_is_better) {
      best_idx = current_idx;
      best_val = current_val;
    }
  }
  *indices.b = best_idx;
  indices.e = indices.b + 1;
}

// Creates child-to-parent tree structure from parent_id column storage.
// The _tree_id column is always 0..n-1 (implicit row indices).
// The _tree_parent_id column contains parent row indices (UINT32_MAX for null).
// Fills parent_span with parent indices and original_rows_span with identity.
inline PERFETTO_ALWAYS_INLINE void MakeChildToParentTreeStructure(
    InterpreterState& state,
    const struct MakeChildToParentTreeStructure& bc) {
  using B = struct MakeChildToParentTreeStructure;

  uint32_t row_count = bc.arg<B::row_count>();
  const StoragePtr& parent_storage =
      state.ReadFromRegister(bc.arg<B::parent_id_storage_register>());
  Span<uint32_t>& parent_span =
      state.ReadFromRegister(bc.arg<B::parent_span_register>());
  Span<uint32_t>& original_rows_span =
      state.ReadFromRegister(bc.arg<B::original_rows_span_register>());

  // The parent_id storage is Uint32 type (already normalized by
  // TreeTransformer) UINT32_MAX represents null (root nodes)
  const uint32_t* parent_data =
      static_cast<const uint32_t*>(parent_storage.ptr);

  // Fill the pre-allocated spans
  memcpy(parent_span.b, parent_data, row_count * sizeof(uint32_t));
  std::iota(original_rows_span.b, original_rows_span.b + row_count, 0u);

  // Update span.e to reflect the valid element count
  parent_span.e = parent_span.b + row_count;
  original_rows_span.e = original_rows_span.b + row_count;
}

// Builds a CSR (Compressed Sparse Row) representation for parent-to-child
// traversal from a parent span.
inline PERFETTO_ALWAYS_INLINE void MakeParentToChildTreeStructure(
    InterpreterState& state,
    const struct MakeParentToChildTreeStructure& bc) {
  using B = struct MakeParentToChildTreeStructure;

  const Span<uint32_t>& parent_span =
      state.ReadFromRegister(bc.arg<B::parent_span_register>());
  const Span<uint32_t>& scratch =
      state.ReadFromRegister(bc.arg<B::scratch_register>());
  Span<uint32_t>& offsets =
      state.ReadFromRegister(bc.arg<B::offsets_register>());
  Span<uint32_t>& children =
      state.ReadFromRegister(bc.arg<B::children_register>());
  Span<uint32_t>& roots = state.ReadFromRegister(bc.arg<B::roots_register>());

  // Get count from parent_span.size()
  uint32_t node_count = static_cast<uint32_t>(parent_span.size());

  // Use scratch for child_counts
  uint32_t* child_counts = scratch.b;
  memset(child_counts, 0, node_count * sizeof(uint32_t));

  // First pass: count children per node and count roots
  uint32_t root_count = 0;
  for (uint32_t i = 0; i < node_count; ++i) {
    uint32_t parent = parent_span.b[i];
    if (parent == kNullParent) {
      ++root_count;
    } else {
      ++child_counts[parent];
    }
  }

  // Adjust span sizes based on actual counts
  offsets.e = offsets.b + node_count + 1;
  children.e = children.b + (node_count - root_count);
  roots.e = roots.b + root_count;

  // Compute offsets (prefix sum)
  offsets.b[0] = 0;
  for (uint32_t i = 0; i < node_count; ++i) {
    offsets.b[i + 1] = offsets.b[i] + child_counts[i];
  }

  // Second pass: fill children array and roots.
  // Reuse child_counts as write cursors by counting down from offsets[p+1].
  // This avoids needing to reset child_counts to zero.
  uint32_t root_idx = 0;
  for (uint32_t i = 0; i < node_count; ++i) {
    uint32_t parent = parent_span.b[i];
    if (parent == kNullParent) {
      roots.b[root_idx++] = i;
    } else {
      // child_counts[parent] starts at the total count and decrements.
      // offsets[parent+1] - count gives positions: offsets[parent], +1, +2, ...
      uint32_t pos = offsets.b[parent + 1] - child_counts[parent];
      children.b[pos] = i;
      --child_counts[parent];
    }
  }
}

// Converts a span of indices to a BitVector with bits set at those indices.
inline PERFETTO_ALWAYS_INLINE void IndexSpanToBitvector(
    InterpreterState& state,
    const struct IndexSpanToBitvector& bc) {
  using B = struct IndexSpanToBitvector;

  const Span<uint32_t>& indices =
      state.ReadFromRegister(bc.arg<B::indices_register>());
  uint32_t bv_size = bc.arg<B::bitvector_size>();

  BitVector* bv = state.MaybeReadFromRegister(bc.arg<B::dest_register>());
  if (bv) {
    // Reuse existing BitVector: resize and clear all bits.
    bv->resize(bv_size, false);
    bv->ClearAllBits();
  } else {
    state.WriteToRegister(bc.arg<B::dest_register>(),
                          BitVector::CreateWithSize(bv_size, false));
    bv = state.MaybeReadFromRegister(bc.arg<B::dest_register>());
  }

  for (const uint32_t* it = indices.b; it != indices.e; ++it) {
    bv->set(*it);
  }
}

// Filters a tree by keeping only nodes specified in the bitvector.
// Children of removed nodes are reparented to their closest surviving ancestor.
inline PERFETTO_ALWAYS_INLINE void FilterTree(InterpreterState& state,
                                              const struct FilterTree& bc) {
  using B = struct FilterTree;

  const Span<uint32_t>& offsets =
      state.ReadFromRegister(bc.arg<B::offsets_register>());
  const Span<uint32_t>& children =
      state.ReadFromRegister(bc.arg<B::children_register>());
  const Span<uint32_t>& roots =
      state.ReadFromRegister(bc.arg<B::roots_register>());
  const BitVector& keep_bv =
      state.ReadFromRegister(bc.arg<B::keep_bitvector_register>());
  Span<uint32_t>& parent_span =
      state.ReadFromRegister(bc.arg<B::parent_span_register>());
  Span<uint32_t>& original_rows_span =
      state.ReadFromRegister(bc.arg<B::original_rows_span_register>());
  const Span<uint32_t>& scratch1 =
      state.ReadFromRegister(bc.arg<B::scratch1_register>());
  const Span<uint32_t>& scratch2 =
      state.ReadFromRegister(bc.arg<B::scratch2_register>());

  // Get count from parent_span.size()
  auto old_count = static_cast<uint32_t>(parent_span.size());
  if (old_count == 0) {
    return;
  }

  // scratch1: first n for surviving_ancestor, remaining n for queue
  uint32_t* surviving_ancestor = scratch1.b;
  uint32_t* queue = scratch1.b + old_count;

  // scratch2: old_to_new mapping
  uint32_t* old_to_new = scratch2.b;

  // Initialize with UINT32_MAX (0xFF bytes)
  memset(surviving_ancestor, 0xFF, old_count * sizeof(uint32_t));
  memset(old_to_new, 0xFF, old_count * sizeof(uint32_t));

  // BFS to compute surviving ancestors
  uint32_t queue_end = 0;

  // Initialize with roots
  for (uint32_t i = 0; i < roots.size(); ++i) {
    uint32_t root = roots.b[i];
    if (keep_bv.is_set(root)) {
      surviving_ancestor[root] = root;
    }
    // else: surviving_ancestor[root] remains UINT32_MAX
    queue[queue_end++] = root;
  }

  // BFS traversal
  for (uint32_t queue_idx = 0; queue_idx < queue_end; ++queue_idx) {
    uint32_t node = queue[queue_idx];
    uint32_t node_ancestor = surviving_ancestor[node];

    // Process children
    uint32_t children_start = offsets.b[node];
    uint32_t children_end = offsets.b[node + 1];
    for (uint32_t ci = children_start; ci < children_end; ++ci) {
      uint32_t child = children.b[ci];
      if (keep_bv.is_set(child)) {
        surviving_ancestor[child] = child;
      } else {
        surviving_ancestor[child] = node_ancestor;
      }
      queue[queue_end++] = child;
    }
  }

  // Count surviving nodes and build old_to_new mapping
  uint32_t new_count = 0;
  for (uint32_t i = 0; i < old_count; ++i) {
    if (keep_bv.is_set(i)) {
      old_to_new[i] = new_count++;
    }
  }

  if (new_count == 0) {
    // All nodes filtered out - update span.e to reflect empty
    parent_span.e = parent_span.b;
    original_rows_span.e = original_rows_span.b;
    return;
  }

  // In-place compaction: since new_idx <= i always (we skip filtered nodes),
  // we can safely write to earlier positions without overwriting unread data.
  for (uint32_t i = 0; i < old_count; ++i) {
    if (!keep_bv.is_set(i)) {
      continue;
    }

    uint32_t new_idx = old_to_new[i];

    // Read FIRST from position i (before we potentially overwrite)
    uint32_t old_parent = parent_span.b[i];
    uint32_t old_original = original_rows_span.b[i];

    // Compute new parent by finding surviving ancestor
    uint32_t ancestor = (old_parent != kNullParent)
                            ? surviving_ancestor[old_parent]
                            : kNullParent;
    uint32_t new_parent_val =
        (ancestor != kNullParent) ? old_to_new[ancestor] : kNullParent;

    // Write SECOND to position new_idx (which is <= i, so safe)
    parent_span.b[new_idx] = new_parent_val;
    original_rows_span.b[new_idx] = old_original;
  }

  // Update span.e to reflect new_count
  parent_span.e = parent_span.b + new_count;
  original_rows_span.e = original_rows_span.b + new_count;
}

}  // namespace ops

// Macros for generating case statements that dispatch to ops:: free functions.
// Uses __VA_ARGS__ to handle template types with commas (e.g., SortedFilter<Id,
// EqualRange>).

// For ops that need state and fetcher:
#define PERFETTO_OP_CASE_FVF(...)                                \
  case base::variant_index<BytecodeVariant, __VA_ARGS__>(): {    \
    ops::__VA_ARGS__(state_, fetcher,                            \
                     static_cast<const __VA_ARGS__&>(bytecode)); \
    break;                                                       \
  }

// For ops that only need state:
#define PERFETTO_OP_CASE_STATE(...)                                      \
  case base::variant_index<BytecodeVariant, __VA_ARGS__>(): {            \
    ops::__VA_ARGS__(state_, static_cast<const __VA_ARGS__&>(bytecode)); \
    break;                                                               \
  }

// Executes all bytecode instructions using free functions.
template <typename FilterValueFetcherImpl>
PERFETTO_ALWAYS_INLINE void Interpreter<FilterValueFetcherImpl>::Execute(
    FilterValueFetcherImpl& fetcher) {
  for (const auto& bytecode : state_.bytecode) {
    switch (bytecode.option) {
      PERFETTO_DATAFRAME_BYTECODE_FVF_LIST(PERFETTO_OP_CASE_FVF)
      PERFETTO_DATAFRAME_BYTECODE_STATE_ONLY_LIST(PERFETTO_OP_CASE_STATE)
      default:
        PERFETTO_ASSUME(false);
    }
  }
}

#undef PERFETTO_OP_CASE_FVF
#undef PERFETTO_OP_CASE_STATE

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_IMPL_H_
