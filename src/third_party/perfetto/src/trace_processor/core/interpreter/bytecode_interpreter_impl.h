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
  NullBitvector& nbv =
      state.ReadFromRegister(popcount.arg<B::null_bv_register>());
  if (nbv.popcount.size() > 0) {
    return;
  }
  nbv.popcount = nbv.bv->PrefixPopcount();
}

void AllocateRowLayoutBuffer(InterpreterState& state,
                             const struct AllocateRowLayoutBuffer& bytecode);

void LimitOffsetIndices(InterpreterState& state,
                        const LimitOffsetIndices& bytecode);

void CopySpanIntersectingRange(InterpreterState& state,
                               const CopySpanIntersectingRange& bytecode);

void InitRankMap(InterpreterState& state, const InitRankMap& bytecode);

void CollectIdIntoRankMap(InterpreterState& state,
                          const CollectIdIntoRankMap& bytecode);

void FinalizeRanksInMap(InterpreterState& state,
                        const FinalizeRanksInMap& bytecode);

void Distinct(InterpreterState& state, const Distinct& bytecode);

void SortRowLayout(InterpreterState& state, const SortRowLayout& bytecode);

void TranslateSparseNullIndices(InterpreterState& state,
                                const TranslateSparseNullIndices& bytecode);

void StrideTranslateAndCopySparseNullIndices(
    InterpreterState& state,
    const StrideTranslateAndCopySparseNullIndices& bytecode);

void StrideCopyDenseNullIndices(InterpreterState& state,
                                const StrideCopyDenseNullIndices& bytecode);

template <typename NullOp>
inline PERFETTO_ALWAYS_INLINE void NullFilter(InterpreterState& state,
                                              const NullFilterBase& filter) {
  using B = NullFilterBase;
  const NullBitvector& nbv =
      state.ReadFromRegister(filter.arg<B::null_bv_register>());
  auto& update = state.ReadFromRegister(filter.arg<B::update_register>());
  static constexpr bool kInvert = std::is_same_v<NullOp, IsNull>;
  update.e = nbv.bv->template PackLeft<kInvert>(update.b, update.e, update.b);
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
IndexToStorageIndex(uint32_t index, const NullBitvector* nbv) {
  if constexpr (std::is_same_v<N, NonNull>) {
    base::ignore_result(nbv);
    return index;
  } else if constexpr (std::is_same_v<N, SparseNull>) {
    if (!nbv->bv->is_set(index)) {
      // Null values are always less than non-null values.
      return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(nbv->popcount[index / 64] +
                                 nbv->bv->count_set_bits_until_in_word(index));
  } else if constexpr (std::is_same_v<N, DenseNull>) {
    return nbv->bv->is_set(index) ? index
                                  : std::numeric_limits<uint32_t>::max();
  } else {
    static_assert(std::is_same_v<N, NonNull>, "Unsupported type");
  }
}

// Binary-searches a sorted index for all entries whose resolved storage
// value equals |target|. Returns the [lb, ub) range of matching index
// entries.
//
// |target| should be a NullTermStringView for String columns (i.e. a
// pre-resolved string_pool->Get result) or the raw value for other types.
// This avoids repeated string_pool->Get calls inside the comparators.
template <typename T, typename N, typename Resolved>
inline PERFETTO_ALWAYS_INLINE std::pair<uint32_t*, uint32_t*> IndexEqualRange(
    uint32_t* begin,
    uint32_t* end,
    const Resolved& target,
    const typename T::cpp_type* data,
    const NullBitvector* nbv,
    const StringPool* string_pool) {
  auto* lb =
      std::lower_bound(begin, end, target, [&](uint32_t idx, const Resolved&) {
        uint32_t si = IndexToStorageIndex<N>(idx, nbv);
        if (si == std::numeric_limits<uint32_t>::max())
          return true;
        if constexpr (std::is_same_v<T, String>) {
          return string_pool->Get(data[si]) < target;
        } else {
          return data[si] < target;
        }
      });
  auto* ub =
      std::upper_bound(lb, end, target, [&](const Resolved&, uint32_t idx) {
        uint32_t si = IndexToStorageIndex<N>(idx, nbv);
        if (si == std::numeric_limits<uint32_t>::max())
          return false;
        if constexpr (std::is_same_v<T, String>) {
          return target < string_pool->Get(data[si]);
        } else {
          return target < data[si];
        }
      });
  return {lb, ub};
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
  const NullBitvector* nbv =
      state.MaybeReadFromRegister(bytecode.arg<B::null_bv_register>());

  std::tie(dest.b, dest.e) = IndexEqualRange<T, N>(
      source.b, source.e, value, data, nbv, state.string_pool);
  state.WriteToRegister(bytecode.arg<B::dest_register>(), dest);
}

// ============================================================================
// FilterIn: IN-clause filtering
//
// This section implements the FilterIn bytecode which handles SQL IN(...)
// clauses. The code is organized as follows:
//
//   1. Constants (thresholds for algorithm selection)
//   2. Helpers for building lookup structures (MaybeBuildBitVector,
//      MaybeBuildValueList)
//   3. CastFilterValueList: casts raw filter values into typed lookup
//      structures (HashMap, BitVector, sorted ValueList)
//   4. NonIndexedFilterInImpl: scans indices with hash/bitvector/linear lookup
//   5. IndexedFilterInBinarySearch: binary-searches a sorted index
//   6. IndexedFilterIn: dispatches between binary search and linear scan
//   7. FilterIn: the public bytecode entry point
// ============================================================================

namespace {

// Below this threshold, the non-indexed FilterIn path does a linear scan
// over the value list instead of a HashMap lookup (avoids hashing overhead).
constexpr uint32_t kFilterInKeyScanThreshold = 16;

// Above this threshold, the indexed FilterIn path switches from binary
// search to copying the index and filtering in-place with the HashMap.
//
// Benchmarks (BM_FilterIn_IndexedBinarySearch / BM_FilterIn_IndexedLinearScan,
// run with BENCHMARK_CONSTANT_SWEEPING=1) show the crossover is consistently
// between k=50 and k=200 regardless of n, because the O(m·log(m)) sort of
// matched results dominates the binary search path when k is large. A threshold
// of 64 catches the crossover conservatively.
constexpr uint32_t kFilterInIndexedBinarySearchThreshold = 64;

// value_list is needed by both the key-scan and binary-search paths.
constexpr uint32_t kFilterInValueListThreshold =
    std::max(kFilterInKeyScanThreshold, kFilterInIndexedBinarySearchThreshold);

}  // namespace

// Extracts the raw uint32_t from an Id or Uint32 key.
template <typename T>
uint32_t FilterInKeyToUint32(const T& key) {
  if constexpr (std::is_same_v<T, CastFilterValueResult::Id>) {
    return key.value;
  } else {
    static_assert(std::is_same_v<T, uint32_t>);
    return key;
  }
}

// If values are dense Id/Uint32, builds a BitVector from the HashMap.
template <typename T>
void MaybeBuildBitVector(CastFilterValueListResult& result) {
  if constexpr (std::is_same_v<T, Id> || std::is_same_v<T, Uint32>) {
    using HM = StorageType::VariantTypeAtIndex<
        T, CastFilterValueListResult::ValueHashMap>;
    auto& hm = base::unchecked_get<HM>(result.hash_map);
    uint32_t max_val = 0;
    for (auto it = hm.GetIterator(); it; ++it) {
      max_val = std::max(max_val, FilterInKeyToUint32(it.key()));
    }
    if (max_val <= static_cast<uint32_t>(hm.size()) * 16) {
      result.bit_vector.resize(max_val + 1);
      result.bit_vector.ClearAllBits();
      for (auto it = hm.GetIterator(); it; ++it) {
        result.bit_vector.set(FilterInKeyToUint32(it.key()));
      }
    }
  }
}

// Builds a value_list from the HashMap when the key count is small enough
// for the linear scan or indexed binary search paths.
template <typename T>
void MaybeBuildValueList(CastFilterValueListResult& result) {
  using HM =
      StorageType::VariantTypeAtIndex<T,
                                      CastFilterValueListResult::ValueHashMap>;
  using VL =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::ValueList>;
  auto& hm = base::unchecked_get<HM>(result.hash_map);
  if (hm.size() > kFilterInValueListThreshold) {
    return;
  }
  auto& vl = base::unchecked_get<VL>(result.value_list);
  for (auto it = hm.GetIterator(); it; ++it) {
    vl.push_back(it.key());
  }
}

// Casts raw filter values into typed lookup structures for IN-clause
// filtering. Populates a HashMap (canonical), and optionally a BitVector
// (for dense Id/Uint32) and a sorted ValueList (for small lists).
//
// Reuses existing allocations when a previous result is present in the
// register, avoiding repeated heap allocation across query iterations.
template <typename T, typename FilterValueFetcherImpl>
inline PERFETTO_ALWAYS_INLINE void CastFilterValueList(
    InterpreterState& state,
    FilterValueFetcherImpl& fetcher,
    const CastFilterValueListBase& c) {
  using B = CastFilterValueListBase;
  using ValueType =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::Value>;
  using HM =
      StorageType::VariantTypeAtIndex<T,
                                      CastFilterValueListResult::ValueHashMap>;
  FilterValueHandle handle = c.arg<B::fval_handle>();

  // Reuse existing allocation if available, otherwise allocate fresh.
  CastFilterValueListResult::Ptr* existing =
      state.MaybeReadFromRegister(c.arg<B::write_register>());
  CastFilterValueListResult::Ptr result;
  if (existing && *existing) {
    result = std::move(*existing);
    result->Clear<T>();
  } else {
    result = std::make_unique<CastFilterValueListResult>();
    result->Init<T>();
  }
  auto& hm = base::unchecked_get<HM>(result->hash_map);
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
        hm.Insert(CastFilterValueResult::Id{result_value}, true);
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
        hm.Insert(result_value, true);
      } else if (validity == CastFilterValueResult::kAllMatch) {
        all_match = true;
        break;
      }
    } else if constexpr (std::is_same_v<T, String>) {
      auto op = *c.arg<B::op>().TryDowncast<StringOp>();
      PERFETTO_CHECK(op.Is<Eq>());
      const char* result_value;
      auto validity = CastFilterValueToString<FilterValueFetcherImpl>(
          handle, filter_value_type, fetcher, op, result_value);
      if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
        auto id = state.string_pool->GetId(result_value);
        if (id) {
          hm.Insert(*id, true);
        }
      } else if (validity == CastFilterValueResult::kAllMatch) {
        all_match = true;
        break;
      }
    } else {
      static_assert(std::is_same_v<T, Id>, "Unsupported type");
    }
  }
  if (all_match) {
    result->validity = CastFilterValueResult::Validity::kAllMatch;
  } else if (hm.size() == 0) {
    result->validity = CastFilterValueResult::Validity::kNoneMatch;
  } else {
    result->validity = CastFilterValueResult::Validity::kValid;
    MaybeBuildBitVector<T>(*result);
    MaybeBuildValueList<T>(*result);
  }
  state.WriteToRegister(c.arg<B::write_register>(), std::move(result));
}

// BitVector membership filter for FilterIn. O(1) per row.
// Only applicable to Id/Uint32 columns with dense value ranges.
template <typename T, typename DataType>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE uint32_t* FilterInBitVector(
    const DataType* data,
    const uint32_t* source_begin,
    const uint32_t* source_end,
    uint32_t* dest,
    const BitVector& bv) {
  struct Cmp {
    PERFETTO_ALWAYS_INLINE bool operator()(uint32_t lhs,
                                           const BitVector& b) const {
      return lhs < b.size() && b.is_set(lhs);
    }
  };
  if constexpr (std::is_same_v<T, Id>) {
    base::ignore_result(data);
    return IdentityFilter(source_begin, source_end, dest, bv, Cmp());
  } else {
    return Filter(data, source_begin, source_end, dest, bv, Cmp());
  }
}

// Linear scan membership filter for FilterIn. Iterates over a small sorted
// value list for each row. Avoids HashMap hashing overhead for small lists.
template <typename T, typename DataType, typename VL>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE uint32_t* FilterInLinearScan(
    const DataType* data,
    const uint32_t* source_begin,
    const uint32_t* source_end,
    uint32_t* dest,
    const VL& vl) {
  using ValElem =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::Value>;
  if constexpr (std::is_same_v<T, Id>) {
    struct Cmp {
      PERFETTO_ALWAYS_INLINE bool operator()(
          uint32_t lhs,
          const FlexVector<ValElem>& k) const {
        for (const auto& v : k) {
          if (lhs == v.value)
            return true;
        }
        return false;
      }
    };
    return IdentityFilter(source_begin, source_end, dest, vl, Cmp());
  } else {
    using D = std::remove_cv_t<std::remove_reference_t<decltype(*data)>>;
    struct Cmp {
      PERFETTO_ALWAYS_INLINE bool operator()(
          D lhs,
          const FlexVector<ValElem>& k) const {
        for (const auto& v : k) {
          if (std::equal_to<>()(lhs, v))
            return true;
        }
        return false;
      }
    };
    return Filter(data, source_begin, source_end, dest, vl, Cmp());
  }
}

// HashMap membership filter for FilterIn. O(1) per row via hash lookup.
template <typename T, typename DataType, typename HM>
[[nodiscard]] inline PERFETTO_ALWAYS_INLINE uint32_t* FilterInHashMap(
    const DataType* data,
    const uint32_t* source_begin,
    const uint32_t* source_end,
    uint32_t* dest,
    const HM& hm) {
  if constexpr (std::is_same_v<T, Id>) {
    struct Cmp {
      PERFETTO_ALWAYS_INLINE bool operator()(uint32_t lhs, const HM& h) const {
        return h.Find(CastFilterValueResult::Id{lhs}) != nullptr;
      }
    };
    return IdentityFilter(source_begin, source_end, dest, hm, Cmp());
  } else {
    using D = std::remove_cv_t<std::remove_reference_t<decltype(*data)>>;
    struct Cmp {
      PERFETTO_ALWAYS_INLINE bool operator()(D lhs, const HM& h) const {
        return h.Find(lhs) != nullptr;
      }
    };
    return Filter(data, source_begin, source_end, dest, hm, Cmp());
  }
}

// Attempts the indexed binary search path for FilterIn. Looks up the
// index register; if present and the IN list is small enough, binary-
// searches the sorted index for each value and populates dest. Returns
// true if binary search was performed, false if the caller should use
// a different strategy.
//
// TODO(lalitm): since the index is sorted, galloping search could replace
// repeated std::lower_bound for better locality.
template <typename T, typename N>
inline PERFETTO_ALWAYS_INLINE bool TryIndexedFilterInBinarySearch(
    InterpreterState& state,
    const FilterInBase& bytecode,
    const CastFilterValueListResult& cast_result,
    Span<uint32_t>& dest) {
  using B = FilterInBase;

  // Id columns have no backing storage and are never indexed.
  if constexpr (std::is_same_v<T, Id>) {
    return false;
  } else {
    const Span<uint32_t>* index =
        state.MaybeReadFromRegister(bytecode.arg<B::index_register>());
    if (!index) {
      return false;
    }

    using VL =
        StorageType::VariantTypeAtIndex<T,
                                        CastFilterValueListResult::ValueList>;
    const VL& vl = base::unchecked_get<VL>(cast_result.value_list);
    if (vl.empty() || vl.size() > kFilterInIndexedBinarySearchThreshold) {
      return false;
    }

    const auto* data =
        state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());
    const NullBitvector* nbv =
        state.MaybeReadFromRegister(bytecode.arg<B::null_bv_register>());
    const StringPool* string_pool = state.string_pool;

    uint32_t* write = dest.b;
    for (const auto& cmp_val : vl) {
      // For String columns, resolve StringPool::Id to NullTermStringView
      // once per value to avoid repeated lookups in the comparators.
      std::pair<uint32_t*, uint32_t*> range;
      if constexpr (std::is_same_v<T, String>) {
        auto target = string_pool->Get(cmp_val);
        range = IndexEqualRange<T, N>(index->b, index->e, target, data, nbv,
                                      string_pool);
      } else {
        range = IndexEqualRange<T, N>(index->b, index->e, cmp_val, data, nbv,
                                      string_pool);
      }
      auto n = static_cast<size_t>(range.second - range.first);
      memcpy(write, range.first, n * sizeof(uint32_t));
      write += n;
    }
    dest.e = write;

    // Binary search produces output in key order. Sort by raw index.
    std::sort(dest.b, dest.e);
    return true;
  }
}

// Scan path for FilterIn when no index is present. The source span and dest
// span are walked in lockstep: source[i] is the storage index for dest[i].
// Matching dest entries are compacted in-place.
template <typename T, typename DataType>
inline PERFETTO_ALWAYS_INLINE void NonIndexedFilterInScan(
    const CastFilterValueListResult& cast_result,
    const DataType* data,
    const Span<uint32_t>& source,
    Span<uint32_t>& dest) {
  using HM =
      StorageType::VariantTypeAtIndex<T,
                                      CastFilterValueListResult::ValueHashMap>;
  using VL =
      StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::ValueList>;
  if constexpr (std::is_same_v<T, Id> || std::is_same_v<T, Uint32>) {
    if (cast_result.bit_vector.size() > 0) {
      dest.e = FilterInBitVector<T>(data, source.b, source.e, dest.b,
                                    cast_result.bit_vector);
      return;
    }
  }
  const auto& vl = base::unchecked_get<VL>(cast_result.value_list);
  if (!vl.empty() && vl.size() <= kFilterInKeyScanThreshold) {
    dest.e = FilterInLinearScan<T>(data, source.b, source.e, dest.b, vl);
    return;
  }
  const auto& hm = base::unchecked_get<HM>(cast_result.hash_map);
  dest.e = FilterInHashMap<T>(data, source.b, source.e, dest.b, hm);
}

// Scan path for FilterIn when an index is present but the IN list is too
// large for binary search. The index is ignored entirely: we iterate over
// the source range [b, e), check each row against the hash map, and write
// matching row indices to dest.
template <typename T, typename N>
inline PERFETTO_ALWAYS_INLINE void IndexedFilterInRangeScan(
    InterpreterState& state,
    const FilterInBase& bytecode,
    const CastFilterValueListResult& cast_result,
    Span<uint32_t>& dest) {
  using B = FilterInBase;
  using HM =
      StorageType::VariantTypeAtIndex<T,
                                      CastFilterValueListResult::ValueHashMap>;
  const auto* data =
      state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());
  // |data| is unused for Id columns (the storage index IS the value).
  base::ignore_result(data);
  const NullBitvector* nbv =
      state.MaybeReadFromRegister(bytecode.arg<B::null_bv_register>());
  const auto& hm = base::unchecked_get<HM>(cast_result.hash_map);
  const Range& range =
      state.ReadFromRegister(bytecode.arg<B::source_range_register>());

  uint32_t* write = dest.b;
  for (uint32_t i = range.b; i < range.e; ++i) {
    uint32_t si = IndexToStorageIndex<N>(i, nbv);
    if (si == std::numeric_limits<uint32_t>::max()) {
      continue;
    }
    if constexpr (std::is_same_v<T, Id>) {
      if (hm.Find(CastFilterValueResult::Id{si}) != nullptr) {
        *write++ = i;
      }
    } else {
      if (hm.Find(data[si]) != nullptr) {
        *write++ = i;
      }
    }
  }
  dest.e = write;
}

// Unified filter-in bytecode entry point.
template <typename T, typename N>
inline PERFETTO_ALWAYS_INLINE void FilterIn(InterpreterState& state,
                                            const FilterInBase& bytecode) {
  using B = FilterInBase;
  const auto& cast_result =
      *state.ReadFromRegister(bytecode.arg<B::value_list_register>());
  auto& dest = state.ReadFromRegister(bytecode.arg<B::dest_register>());

  if (!HandleInvalidCastFilterValueResult(cast_result.validity, dest)) {
    return;
  }

  // Try indexed binary search for small IN lists on indexed columns.
  if (TryIndexedFilterInBinarySearch<T, N>(state, bytecode, cast_result,
                                           dest)) {
    return;
  }

  // Scan path: either indexed range scan or non-indexed span scan.
  const auto* source_range =
      state.MaybeReadFromRegister(bytecode.arg<B::source_range_register>());
  if (source_range) {
    // Index present but IN list too large for binary search. Ignore the
    // index entirely and scan the row range with hash lookup.
    IndexedFilterInRangeScan<T, N>(state, bytecode, cast_result, dest);
    return;
  }
  // Non-indexed path: scan source span with in-place compaction of dest.
  const auto* data =
      state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());
  const Span<uint32_t>& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  NonIndexedFilterInScan<T>(cast_result, data, source, dest);
}

// ============================================================================
// End FilterIn
// ============================================================================

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

  const auto* data =
      state.ReadStorageFromRegister<T>(bytecode.arg<B::storage_register>());

  // GCC complains that these variables are not used in the NonNull branches.
  [[maybe_unused]] const StringIdToRankMap* rank_map_ptr =
      state.MaybeReadFromRegister(bytecode.arg<B::rank_map_register>());
  [[maybe_unused]] const NullBitvector* nbv =
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
      PERFETTO_DCHECK(nbv && nbv->popcount.size() > 0);
      is_non_null = nbv->bv->is_set(table_index);
      storage_index = is_non_null
                          ? static_cast<uint32_t>(
                                nbv->popcount[*ptr / 64] +
                                nbv->bv->count_set_bits_until_in_word(*ptr))
                          : std::numeric_limits<uint32_t>::max();
      uint8_t res = is_non_null ? 0xFF : 0;
      *dest = invert ? ~res : res;
      offset = 1;
    } else if constexpr (std::is_same_v<Nullability, DenseNull>) {
      is_non_null = nbv->bv->is_set(table_index);
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

// Reparents and compacts a tree based on pre-filtered indices.
// Also compacts all column storage and null bitvectors registered in
// the TreeState, and resets the indices span to [0..new_row_count-1].
void FilterTreeState(InterpreterState& state, const struct FilterTreeState& bc);

// Propagates column values from roots toward leaves using BFS.
// For each parent→child edge, applies the aggregate operation from
// TreeState::propagate_down_specs.
void PropagateTreeDown(InterpreterState& state,
                       const struct PropagateTreeDown& bc);

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
