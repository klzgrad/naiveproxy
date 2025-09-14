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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_IMPL_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_IMPL_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/endian.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/impl/bit_vector.h"
#include "src/trace_processor/dataframe/impl/bytecode_instructions.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter_state.h"
#include "src/trace_processor/dataframe/impl/bytecode_registers.h"
#include "src/trace_processor/dataframe/impl/flex_vector.h"
#include "src/trace_processor/dataframe/impl/slab.h"
#include "src/trace_processor/dataframe/impl/sort.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/types.h"
#include "src/trace_processor/dataframe/value_fetcher.h"
#include "src/trace_processor/util/glob.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto::trace_processor::dataframe::impl::bytecode {
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
struct Glob {
  bool operator()(StringPool::Id lhs, const util::GlobMatcher& matcher) const {
    return matcher.Matches(pool_->Get(lhs));
  }
  const StringPool* pool_;
};
struct GlobFullStringPool {
  bool operator()(StringPool::Id lhs, const BitVector& matches) const {
    return matches.is_set(lhs.raw_id());
  }
};
struct Regex {
  bool operator()(StringPool::Id lhs, const regex::Regex& pattern) const {
    return pattern.Search(pool_->Get(lhs).c_str());
  }
  const StringPool* pool_;
};

}  // namespace comparators

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

// The Interpreter class implements a virtual machine that executes bytecode
// instructions for dataframe query operations. It maintains an internal
// register state, processes sequences of bytecode operations, and applies
// filter and transformation operations to data columns. The interpreter is
// designed for high-performance data filtering and manipulation, with
// specialized handling for different data types and comparison operations.
//
// This class is templated on a subclass of ValueFetcher, which is used to
// fetch filter values for each filter spec.
template <typename FilterValueFetcherImpl>
class InterpreterImpl {
 public:
  static_assert(std::is_base_of_v<ValueFetcher, FilterValueFetcherImpl>,
                "FilterValueFetcherImpl must be a subclass of ValueFetcher");

  explicit InterpreterImpl(FilterValueFetcherImpl& fetcher,
                           InterpreterState& state)
      : fetcher_(fetcher), state_(state) {}

#define PERFETTO_DATAFRAME_BYTECODE_CASE_FN(...)                            \
  case base::variant_index<bytecode::BytecodeVariant,                       \
                           bytecode::__VA_ARGS__>(): {                      \
    this->__VA_ARGS__(static_cast<const bytecode::__VA_ARGS__&>(bytecode)); \
    break;                                                                  \
  }

  // Executes the bytecode sequence, processing each bytecode instruction in
  // turn, and dispatching to the appropriate function in this class.
  PERFETTO_ALWAYS_INLINE void Execute() {
    for (const auto& bytecode : state_.bytecode) {
      switch (bytecode.option) {
        PERFETTO_DATAFRAME_BYTECODE_LIST(PERFETTO_DATAFRAME_BYTECODE_CASE_FN)
        default:
          PERFETTO_ASSUME(false);
      }
    }
  }

 private:
  PERFETTO_ALWAYS_INLINE void InitRange(const bytecode::InitRange& init) {
    using B = bytecode::InitRange;
    WriteToRegister(init.arg<B::dest_register>(),
                    Range{0, init.arg<B::size>()});
  }

  PERFETTO_ALWAYS_INLINE void AllocateIndices(
      const bytecode::AllocateIndices& ai) {
    using B = bytecode::AllocateIndices;

    if (auto* exist_slab =
            MaybeReadFromRegister(ai.arg<B::dest_slab_register>())) {
      // Ensure that the slab is at least as big as the requested size.
      PERFETTO_DCHECK(ai.arg<B::size>() <= exist_slab->size());

      // Update the span to point to the needed size of the slab.
      WriteToRegister(ai.arg<B::dest_span_register>(),
                      Span<uint32_t>{exist_slab->begin(),
                                     exist_slab->begin() + ai.arg<B::size>()});
    } else {
      auto slab = Slab<uint32_t>::Alloc(ai.arg<B::size>());
      Span<uint32_t> span{slab.begin(), slab.end()};
      WriteToRegister(ai.arg<B::dest_slab_register>(), std::move(slab));
      WriteToRegister(ai.arg<B::dest_span_register>(), span);
    }
  }

  // Fills a SlabSegment with sequential values starting from source.begin().
  PERFETTO_ALWAYS_INLINE void Iota(const bytecode::Iota& r) {
    using B = bytecode::Iota;
    const auto& source = ReadFromRegister(r.arg<B::source_register>());

    auto& update = ReadFromRegister(r.arg<B::update_register>());
    PERFETTO_DCHECK(source.size() <= update.size());
    auto* end = update.b + source.size();
    std::iota(update.b, end, source.b);
    update.e = end;
  }

  // Attempts to cast a filter value to the specified type and stores the
  // result. Currently only supports casting to Id type.
  template <typename T>
  PERFETTO_ALWAYS_INLINE void CastFilterValue(
      const bytecode::CastFilterValueBase& f) {
    using B = bytecode::CastFilterValueBase;
    FilterValueHandle handle = f.arg<B::fval_handle>();
    typename FilterValueFetcherImpl::Type filter_value_type =
        fetcher_.GetValueType(handle.index);

    using ValueType =
        StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
    CastFilterValueResult result;
    if constexpr (std::is_same_v<T, Id>) {
      auto op = *f.arg<B::op>().TryDowncast<NonStringOp>();
      uint32_t result_value;
      result.validity = CastFilterValueToInteger(handle, filter_value_type,
                                                 fetcher_, op, result_value);
      if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
        result.value = CastFilterValueResult::Id{result_value};
      }
    } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
      auto op = *f.arg<B::op>().TryDowncast<NonStringOp>();
      ValueType result_value;
      result.validity = CastFilterValueToIntegerOrDouble(
          handle, filter_value_type, fetcher_, op, result_value);
      if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
        result.value = result_value;
      }
    } else if constexpr (std::is_same_v<T, String>) {
      static_assert(std::is_same_v<ValueType, const char*>);
      auto op = *f.arg<B::op>().TryDowncast<StringOp>();
      const char* result_value;
      result.validity = CastFilterValueToString(handle, filter_value_type,
                                                fetcher_, op, result_value);
      if (PERFETTO_LIKELY(result.validity == CastFilterValueResult::kValid)) {
        result.value = result_value;
      }
    } else {
      static_assert(std::is_same_v<T, Id>, "Unsupported type");
    }
    WriteToRegister(f.arg<B::write_register>(), result);
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE void CastFilterValueList(
      const bytecode::CastFilterValueListBase& c) {
    using B = bytecode::CastFilterValueListBase;
    FilterValueHandle handle = c.arg<B::fval_handle>();
    using ValueType =
        StorageType::VariantTypeAtIndex<T, CastFilterValueListResult::Value>;
    FlexVector<ValueType> results;
    bool all_match = false;
    for (bool has_more = fetcher_.IteratorInit(handle.index); has_more;
         has_more = fetcher_.IteratorNext(handle.index)) {
      typename FilterValueFetcherImpl::Type filter_value_type =
          fetcher_.GetValueType(handle.index);
      if constexpr (std::is_same_v<T, Id>) {
        auto op = *c.arg<B::op>().TryDowncast<NonStringOp>();
        uint32_t result_value;
        auto validity = CastFilterValueToInteger(handle, filter_value_type,
                                                 fetcher_, op, result_value);
        if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
          results.push_back(CastFilterValueResult::Id{result_value});
        } else if (validity == CastFilterValueResult::kAllMatch) {
          all_match = true;
          break;
        }
      } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
        auto op = *c.arg<B::op>().TryDowncast<NonStringOp>();
        ValueType result_value;
        auto validity = CastFilterValueToIntegerOrDouble(
            handle, filter_value_type, fetcher_, op, result_value);
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
        auto validity = CastFilterValueToString(handle, filter_value_type,
                                                fetcher_, op, result_value);
        if (PERFETTO_LIKELY(validity == CastFilterValueResult::kValid)) {
          auto id = state_.string_pool->GetId(result_value);
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
    WriteToRegister(c.arg<B::write_register>(), std::move(result));
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE void In(const bytecode::InBase& f) {
    using B = bytecode::InBase;
    const auto& value = ReadFromRegister(f.arg<B::value_list_register>());
    const Span<uint32_t>& source =
        ReadFromRegister(f.arg<B::source_register>());
    Span<uint32_t>& update = ReadFromRegister(f.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(value.validity, update)) {
      return;
    }
    using M =
        StorageType::VariantTypeAtIndex<T,
                                        CastFilterValueListResult::ValueList>;
    const M& val = base::unchecked_get<M>(value.value_list);
    const auto& col = GetColumn(f.arg<B::col>());

    // Try to use a bitvector if the value is an Id or uint32_t.
    // This is a performance optimization to avoid iterating over the
    // FlexVector for large lists of values.
    if constexpr (std::is_same_v<T, Id> || std::is_same_v<T, Uint32>) {
      const auto* data = col.storage.template unchecked_data<T>();
      if (InBitVector<T>(val, data, source, update)) {
        return;
      }
    }
    if constexpr (std::is_same_v<T, Id>) {
      struct Comparator {
        bool operator()(
            uint32_t lhs,
            const FlexVector<CastFilterValueResult::Id>& rhs) const {
          for (const auto& r : rhs) {
            if (lhs == r.value) {
              return true;
            }
          }
          return false;
        }
      };
      update.e =
          IdentityFilter(source.b, source.e, update.b, val, Comparator());
    } else {
      const auto* data = col.storage.template unchecked_data<T>();
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

  template <typename T, typename M, typename D>
  PERFETTO_ALWAYS_INLINE bool InBitVector(const FlexVector<M>& val,
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
    struct InBitVector {
      PERFETTO_ALWAYS_INLINE bool operator()(uint32_t lhs,
                                             const BitVector& bv) const {
        return lhs < bv.size() && bv.is_set(lhs);
      }
    };
    if constexpr (std::is_same_v<T, Id>) {
      base::ignore_result(data);
      update.e =
          IdentityFilter(source.b, source.e, update.b, bv, InBitVector());
    } else {
      update.e = Filter(data, source.b, source.e, update.b, bv, InBitVector());
    }
    return true;
  }

  PERFETTO_ALWAYS_INLINE void Reverse(const bytecode::Reverse& r) {
    using B = bytecode::Reverse;
    auto& update = ReadFromRegister(r.arg<B::update_register>());
    std::reverse(update.b, update.e);
  }

  template <typename T, typename RangeOp>
  PERFETTO_ALWAYS_INLINE void SortedFilter(
      const bytecode::SortedFilterBase& f) {
    using B = bytecode::SortedFilterBase;

    const auto& value = ReadFromRegister(f.arg<B::val_register>());
    Range& update = ReadFromRegister(f.arg<B::update_register>());
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
        BoundModifier bound_to_modify = f.arg<B::write_result_to>();

        uint32_t effective_val =
            inner_val + std::is_same_v<RangeOp, UpperBound>;
        bool is_begin_bound = bound_to_modify.Is<BeginBound>();

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
      BoundModifier bound_modifier = f.arg<B::write_result_to>();
      const auto* data =
          GetColumn(f.arg<B::col>()).storage.template unchecked_data<T>();
      NonIdSortedFilter<RangeOp>(data, val, bound_modifier, update);
    }
  }

  template <typename RangeOp, typename DataType, typename ValueType>
  PERFETTO_ALWAYS_INLINE void NonIdSortedFilter(const DataType* data,
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
            state_.string_pool->GetId(base::StringView(val));
        if (!id) {
          update.e = update.b;
          return;
        }
        cmp_value = *id;
      } else {
        cmp_value = val;
      }
      const DataType* eq_start =
          std::lower_bound(begin, end, val, GetLbComprarator<DataType>());
      const DataType* eq_end = eq_start;

      // Scan 16 rows: it's often the case that we have just a very small number
      // of equal rows, so we can avoid a binary search.
      const DataType* eq_end_limit = eq_start + 16;
      for (;; ++eq_end) {
        if (eq_end == end) {
          break;
        }
        if (eq_end == eq_end_limit) {
          eq_end =
              std::upper_bound(eq_start, end, val, GetUbComparator<DataType>());
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
          std::lower_bound(begin, end, val, GetLbComprarator<DataType>()) -
          data);
    } else if constexpr (std::is_same_v<RangeOp, UpperBound>) {
      auto& res = bound_modifier.Is<BeginBound>() ? update.b : update.e;
      res = static_cast<uint32_t>(
          std::upper_bound(begin, end, val, GetUbComparator<DataType>()) -
          data);
    } else {
      static_assert(std::is_same_v<RangeOp, EqualRange>, "Unsupported op");
    }
  }

  template <typename DataType>
  auto GetLbComprarator() {
    if constexpr (std::is_same_v<DataType, StringPool::Id>) {
      return comparators::StringComparator<Lt>{state_.string_pool};
    } else {
      return std::less<>();
    }
  }

  template <typename DataType>
  auto GetUbComparator() {
    if constexpr (std::is_same_v<DataType, StringPool::Id>) {
      return comparators::StringLessInvert{state_.string_pool};
    } else {
      return std::less<>();
    }
  }

  PERFETTO_ALWAYS_INLINE void Uint32SetIdSortedEq(
      const bytecode::Uint32SetIdSortedEq& bytecode) {
    using B = bytecode::Uint32SetIdSortedEq;

    const CastFilterValueResult& cast_result =
        ReadFromRegister(bytecode.arg<B::val_register>());
    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(cast_result.validity, update)) {
      return;
    }
    using ValueType =
        StorageType::VariantTypeAtIndex<Uint32, CastFilterValueResult::Value>;
    auto val = base::unchecked_get<ValueType>(cast_result.value);
    const auto& col = GetColumn(bytecode.arg<B::col>());
    const auto* storage = col.storage.template unchecked_data<Uint32>();
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

  PERFETTO_ALWAYS_INLINE void SpecializedStorageSmallValueEq(
      const bytecode::SpecializedStorageSmallValueEq& bytecode) {
    using B = bytecode::SpecializedStorageSmallValueEq;

    const CastFilterValueResult& cast_result =
        ReadFromRegister(bytecode.arg<B::val_register>());
    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(cast_result.validity, update)) {
      return;
    }
    using ValueType =
        StorageType::VariantTypeAtIndex<Uint32, CastFilterValueResult::Value>;
    auto val = base::unchecked_get<ValueType>(cast_result.value);
    const auto& col = GetColumn(bytecode.arg<B::col>());
    const auto& storage =
        col.specialized_storage
            .template unchecked_get<SpecializedStorage::SmallValueEq>();

    uint32_t k =
        val < storage.bit_vector.size() && storage.bit_vector.is_set(val)
            ? static_cast<uint32_t>(
                  storage.prefix_popcount[val / 64] +
                  storage.bit_vector.count_set_bits_until_in_word(val))
            : update.e;
    bool in_bounds = update.b <= k && k < update.e;
    update.b = in_bounds ? k : update.e;
    update.e = in_bounds ? k + 1 : update.b;
  }

  template <typename T, typename Op>
  PERFETTO_ALWAYS_INLINE void NonStringFilter(
      const bytecode::NonStringFilterBase& nf) {
    using B = bytecode::NonStringFilterBase;
    const auto& value = ReadFromRegister(nf.arg<B::val_register>());
    auto& update = ReadFromRegister(nf.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(value.validity, update)) {
      return;
    }
    const auto& source = ReadFromRegister(nf.arg<B::source_register>());
    using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
    if constexpr (std::is_same_v<T, Id>) {
      update.e = IdentityFilter(
          source.b, source.e, update.b,
          base::unchecked_get<M>(value.value).value,
          comparators::IntegerOrDoubleComparator<uint32_t, Op>());
    } else if constexpr (IntegerOrDoubleType::Contains<T>()) {
      const auto* data =
          GetColumn(nf.arg<B::col>()).storage.template unchecked_data<T>();
      update.e = Filter(data, source.b, source.e, update.b,
                        base::unchecked_get<M>(value.value),
                        comparators::IntegerOrDoubleComparator<M, Op>());
    } else {
      static_assert(std::is_same_v<T, Id>, "Unsupported type");
    }
  }

  template <typename Op>
  PERFETTO_ALWAYS_INLINE void StringFilter(
      const bytecode::StringFilterBase& sf) {
    using B = bytecode::StringFilterBase;
    const auto& filter_value = ReadFromRegister(sf.arg<B::val_register>());
    auto& update = ReadFromRegister(sf.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(filter_value.validity, update)) {
      return;
    }
    const char* val = base::unchecked_get<const char*>(filter_value.value);
    const auto& source = ReadFromRegister(sf.arg<B::source_register>());
    const StringPool::Id* ptr =
        GetColumn(sf.arg<B::col>()).storage.template unchecked_data<String>();
    update.e = FilterStringOp<Op>(ptr, source.b, source.e, update.b, val);
  }

  template <typename NullOp>
  PERFETTO_ALWAYS_INLINE void NullFilter(
      const bytecode::NullFilterBase& filter) {
    using B = bytecode::NullFilterBase;
    const auto& column = GetColumn(filter.arg<B::col>());
    const auto& overlay = column.null_storage;
    auto& update = ReadFromRegister(filter.arg<B::update_register>());
    static constexpr bool kInvert = std::is_same_v<NullOp, IsNull>;
    update.e = overlay.GetNullBitVector().template PackLeft<kInvert>(
        update.b, update.e, update.b);
  }

  PERFETTO_ALWAYS_INLINE void StrideCopy(
      const bytecode::StrideCopy& stride_copy) {
    using B = bytecode::StrideCopy;
    const auto& source =
        ReadFromRegister(stride_copy.arg<B::source_register>());
    auto& update = ReadFromRegister(stride_copy.arg<B::update_register>());
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

  PERFETTO_ALWAYS_INLINE void PrefixPopcount(
      const bytecode::PrefixPopcount& popcount) {
    using B = bytecode::PrefixPopcount;
    auto dest_register = popcount.arg<B::dest_register>();
    if (MaybeReadFromRegister<Slab<uint32_t>>(dest_register)) {
      return;
    }
    const auto& overlay = GetColumn(popcount.arg<B::col>()).null_storage;
    WriteToRegister(dest_register, overlay.GetNullBitVector().PrefixPopcount());
  }

  PERFETTO_ALWAYS_INLINE void TranslateSparseNullIndices(
      const bytecode::TranslateSparseNullIndices& bytecode) {
    using B = bytecode::TranslateSparseNullIndices;
    const auto& overlay = GetColumn(bytecode.arg<B::col>()).null_storage;
    const auto& bv = overlay.template unchecked_get<SparseNull>().bit_vector;

    const auto& source = ReadFromRegister(bytecode.arg<B::source_register>());
    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    PERFETTO_DCHECK(source.size() <= update.size());

    const Slab<uint32_t>& popcnt =
        ReadFromRegister(bytecode.arg<B::popcount_register>());
    uint32_t* out = update.b;
    for (uint32_t* it = source.b; it != source.e; ++it, ++out) {
      uint32_t s = *it;
      *out = static_cast<uint32_t>(popcnt[s / 64] +
                                   bv.count_set_bits_until_in_word(s));
    }
    update.e = out;
  }

  PERFETTO_ALWAYS_INLINE void StrideTranslateAndCopySparseNullIndices(
      const bytecode::StrideTranslateAndCopySparseNullIndices& bytecode) {
    using B = bytecode::StrideTranslateAndCopySparseNullIndices;
    const auto& overlay = GetColumn(bytecode.arg<B::col>()).null_storage;
    const auto& bv = overlay.template unchecked_get<SparseNull>().bit_vector;

    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    uint32_t stride = bytecode.arg<B::stride>();
    uint32_t offset = bytecode.arg<B::offset>();
    const Slab<uint32_t>& popcnt =
        ReadFromRegister(bytecode.arg<B::popcount_register>());
    for (uint32_t* it = update.b; it != update.e; it += stride) {
      uint32_t index = *it;
      if (bv.is_set(index)) {
        it[offset] = static_cast<uint32_t>(
            popcnt[index / 64] + bv.count_set_bits_until_in_word(index));
      } else {
        it[offset] = std::numeric_limits<uint32_t>::max();
      }
    }
  }

  PERFETTO_ALWAYS_INLINE void StrideCopyDenseNullIndices(
      const bytecode::StrideCopyDenseNullIndices& bytecode) {
    using B = bytecode::StrideCopyDenseNullIndices;
    const auto& overlay = GetColumn(bytecode.arg<B::col>()).null_storage;
    const auto& bv = overlay.template unchecked_get<DenseNull>().bit_vector;

    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    uint32_t stride = bytecode.arg<B::stride>();
    uint32_t offset = bytecode.arg<B::offset>();
    for (uint32_t* it = update.b; it != update.e; it += stride) {
      it[offset] = bv.is_set(*it) ? *it : std::numeric_limits<uint32_t>::max();
    }
  }

  PERFETTO_ALWAYS_INLINE void AllocateRowLayoutBuffer(
      const bytecode::AllocateRowLayoutBuffer& bytecode) {
    using B = bytecode::AllocateRowLayoutBuffer;
    uint32_t size = bytecode.arg<B::buffer_size>();
    auto dest_reg = bytecode.arg<B::dest_buffer_register>();
    // Return early if buffer already allocated.
    if (MaybeReadFromRegister(dest_reg)) {
      return;
    }
    WriteToRegister(dest_reg, Slab<uint8_t>::Alloc(size));
  }

  template <typename T, typename Nullability>
  PERFETTO_ALWAYS_INLINE void CopyToRowLayout(
      const bytecode::CopyToRowLayoutBase& bytecode) {
    using B = bytecode::CopyToRowLayoutBase;

    const auto& col = GetColumn(bytecode.arg<B::col>());
    const auto& source =
        ReadFromRegister(bytecode.arg<B::source_indices_register>());
    bool invert = bytecode.arg<B::invert_copied_bits>();

    auto& dest_buffer =
        ReadFromRegister(bytecode.arg<B::dest_buffer_register>());
    uint8_t* dest = dest_buffer.data() + bytecode.arg<B::row_layout_offset>();
    uint32_t stride = bytecode.arg<B::row_layout_stride>();

    const Slab<uint32_t>* popcount_slab = MaybeReadFromRegister<Slab<uint32_t>>(
        bytecode.arg<B::popcount_register>());
    const auto* data = col.storage.template unchecked_data<T>();

    // GCC complains that these variables are not used in the NonNull branches.
    [[maybe_unused]] const reg::StringIdToRankMap* rank_map_ptr =
        MaybeReadFromRegister(bytecode.arg<B::rank_map_register>());
    [[maybe_unused]] const auto* null_bv =
        col.null_storage.MaybeGetNullBitVector();
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

  template <typename T>
  auto GetComparableRowLayoutRepr(T x) {
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
    } else if constexpr (std::is_same_v<T, double>) {
      int64_t bits;
      memcpy(&bits, &x, sizeof(double));
      bits ^= static_cast<int64_t>(static_cast<uint64_t>(bits >> 63) >> 1);
      return GetComparableRowLayoutRepr(bits);
    } else {
      static_assert(std::is_same_v<T, uint32_t>,
                    "Unsupported type for row layout representation");
    }
  }

  PERFETTO_ALWAYS_INLINE void Distinct(const bytecode::Distinct& bytecode) {
    using B = bytecode::Distinct;
    auto& indices = ReadFromRegister(bytecode.arg<B::indices_register>());
    if (indices.empty()) {
      return;
    }

    const auto& buffer = ReadFromRegister(bytecode.arg<B::buffer_register>());
    uint32_t stride = bytecode.arg<B::total_row_stride>();
    const uint8_t* row_ptr = buffer.data();

    std::unordered_set<std::string_view> seen_rows;
    seen_rows.reserve(indices.size());
    uint32_t* write_ptr = indices.b;
    for (const uint32_t* it = indices.b; it != indices.e; ++it) {
      std::string_view row_view(reinterpret_cast<const char*>(row_ptr), stride);
      *write_ptr = *it;
      write_ptr += seen_rows.insert(row_view).second;
      row_ptr += stride;
    }
    indices.e = write_ptr;
  }

  PERFETTO_ALWAYS_INLINE void LimitOffsetIndices(
      const bytecode::LimitOffsetIndices& bytecode) {
    using B = bytecode::LimitOffsetIndices;
    uint32_t offset_value = bytecode.arg<B::offset_value>();
    uint32_t limit_value = bytecode.arg<B::limit_value>();
    auto& span = ReadFromRegister(bytecode.arg<B::update_register>());

    // Apply offset
    auto original_size = static_cast<uint32_t>(span.size());
    uint32_t actual_offset = std::min(offset_value, original_size);
    span.b += actual_offset;

    // Apply limit
    auto size_after_offset = static_cast<uint32_t>(span.size());
    uint32_t actual_limit = std::min(limit_value, size_after_offset);
    span.e = span.b + actual_limit;
  }

  PERFETTO_ALWAYS_INLINE void IndexPermutationVectorToSpan(
      const bytecode::IndexPermutationVectorToSpan& bytecode) {
    using B = bytecode::IndexPermutationVectorToSpan;
    const dataframe::Index& index = state_.indexes[bytecode.arg<B::index>()];
    WriteToRegister(bytecode.arg<B::write_register>(),
                    Span<uint32_t>(index.permutation_vector()->data(),
                                   index.permutation_vector()->data() +
                                       index.permutation_vector()->size()));
  }

  template <typename T, typename N>
  PERFETTO_ALWAYS_INLINE void IndexedFilterEq(
      const bytecode::IndexedFilterEqBase& bytecode) {
    using B = bytecode::IndexedFilterEqBase;
    const auto& filter_value =
        ReadFromRegister(bytecode.arg<B::filter_value_reg>());
    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    if (!HandleInvalidCastFilterValueResult(filter_value.validity, update)) {
      return;
    }
    using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
    const auto& value = base::unchecked_get<M>(filter_value.value);
    const Column& column = GetColumn(bytecode.arg<B::col>());
    const auto* data = column.storage.unchecked_data<T>();
    const Slab<uint32_t>* popcnt =
        MaybeReadFromRegister(bytecode.arg<B::popcount_register>());
    update.b = std::lower_bound(
        update.b, update.e, value, [&](uint32_t index, const M& value) {
          uint32_t storage_idx = IndexToStorageIndex<N>(index, column, popcnt);
          if (storage_idx == std::numeric_limits<uint32_t>::max()) {
            return true;
          }
          if constexpr (std::is_same_v<T, String>) {
            return state_.string_pool->Get(data[storage_idx]) < value;
          } else {
            return data[storage_idx] < value;
          }
        });
    update.e = std::upper_bound(
        update.b, update.e, value, [&](const M& value, uint32_t index) {
          uint32_t storage_idx = IndexToStorageIndex<N>(index, column, popcnt);
          if (storage_idx == std::numeric_limits<uint32_t>::max()) {
            return false;
          }
          if constexpr (std::is_same_v<T, String>) {
            return value < state_.string_pool->Get(data[storage_idx]);
          } else {
            return value < data[storage_idx];
          }
        });
  }

  PERFETTO_ALWAYS_INLINE void CopySpanIntersectingRange(
      const bytecode::CopySpanIntersectingRange& bytecode) {
    using B = bytecode::CopySpanIntersectingRange;
    const auto& source = ReadFromRegister(bytecode.arg<B::source_register>());
    const auto& source_range =
        ReadFromRegister(bytecode.arg<B::source_range_register>());
    auto& update = ReadFromRegister(bytecode.arg<B::update_register>());
    PERFETTO_DCHECK(source.size() <= update.size());
    uint32_t* write_ptr = update.b;
    for (const uint32_t* it = source.b; it != source.e; ++it) {
      *write_ptr = *it;
      write_ptr += (*it >= source_range.b && *it < source_range.e);
    }
    update.e = write_ptr;
  }

  PERFETTO_ALWAYS_INLINE void InitRankMap(
      const bytecode::InitRankMap& bytecode) {
    using B = bytecode::InitRankMap;

    reg::StringIdToRankMap* rank_map =
        MaybeReadFromRegister(bytecode.arg<B::dest_register>());
    if (rank_map) {
      rank_map->get()->Clear();
    } else {
      WriteToRegister(
          bytecode.arg<B::dest_register>(),
          std::make_unique<base::FlatHashMap<StringPool::Id, uint32_t>>());
    }
  }

  PERFETTO_ALWAYS_INLINE void CollectIdIntoRankMap(
      const bytecode::CollectIdIntoRankMap& bytecode) {
    using B = bytecode::CollectIdIntoRankMap;

    reg::StringIdToRankMap& rank_map_ptr =
        ReadFromRegister(bytecode.arg<B::rank_map_register>());
    PERFETTO_DCHECK(rank_map_ptr);
    auto& rank_map = *rank_map_ptr;

    const auto& column = GetColumn(bytecode.arg<B::col>());
    PERFETTO_DCHECK(column.storage.type().template Is<String>());

    const auto* data = column.storage.template unchecked_data<String>();
    const auto& source = ReadFromRegister(bytecode.arg<B::source_register>());
    for (const uint32_t* it = source.b; it != source.e; ++it) {
      rank_map.Insert(data[*it], 0);
    }
  }

  PERFETTO_ALWAYS_INLINE void FinalizeRanksInMap(
      const bytecode::FinalizeRanksInMap& bytecode) {
    using B = bytecode::FinalizeRanksInMap;

    reg::StringIdToRankMap& rank_map_ptr =
        ReadFromRegister(bytecode.arg<B::update_register>());
    PERFETTO_DCHECK(rank_map_ptr && rank_map_ptr.get());
    auto& rank_map = *rank_map_ptr;

    struct SortToken {
      std::string_view str_view;
      StringPool::Id id;
      PERFETTO_ALWAYS_INLINE bool operator<(const SortToken& other) const {
        return str_view < other.str_view;
      }
    };
    // Initally do *not* default initialize the array for performance.
    std::unique_ptr<SortToken[]> ids_to_sort(new SortToken[rank_map.size()]);
    std::unique_ptr<SortToken[]> scratch(new SortToken[rank_map.size()]);
    uint32_t i = 0;
    for (auto it = rank_map.GetIterator(); it; ++it) {
      base::StringView str_view = state_.string_pool->Get(it.key());
      ids_to_sort[i++] = SortToken{
          std::string_view(str_view.data(), str_view.size()),
          it.key(),
      };
    }
    auto* sorted = base::MsdRadixSort(
        ids_to_sort.get(), ids_to_sort.get() + rank_map.size(), scratch.get(),
        [](const SortToken& token) { return token.str_view; });
    for (uint32_t rank = 0; rank < rank_map.size(); ++rank) {
      auto* it = rank_map.Find(sorted[rank].id);
      PERFETTO_DCHECK(it);
      *it = rank;
    }
  }

  PERFETTO_ALWAYS_INLINE void SortRowLayout(
      const bytecode::SortRowLayout& bytecode) {
    using B = bytecode::SortRowLayout;

    auto& indices = ReadFromRegister(bytecode.arg<B::indices_register>());
    auto num_indices = static_cast<size_t>(indices.e - indices.b);

    // Single element is always sorted.
    if (num_indices <= 1) {
      return;
    }

    const auto& buffer_slab =
        ReadFromRegister(bytecode.arg<B::buffer_register>());
    const uint8_t* buf = buffer_slab.data();

    uint32_t stride = bytecode.arg<B::total_row_stride>();

    struct SortToken {
      uint32_t index;
      uint32_t buf_offset;
    };

    // Initally do *not* default initialize the array for performance.
    std::unique_ptr<SortToken[]> p(new SortToken[num_indices]);
    std::unique_ptr<SortToken[]> q;
    for (uint32_t i = 0; i < num_indices; ++i) {
      p[i] = {indices.b[i], i * stride};
    }

    // Crossover point where our custom RadixSort starts becoming faster that
    // std::stable_sort.
    //
    // Empirically chosen by looking at the crossover point of benchmarks
    // BM_DataframeSortLsdRadix and BM_DataframeSortLsdStd.
    static constexpr uint32_t kStableSortCutoff = 4096;
    SortToken* res;
    if (num_indices < kStableSortCutoff) {
      std::stable_sort(p.get(), p.get() + num_indices,
                       [buf, stride](const SortToken& a, const SortToken& b) {
                         return memcmp(buf + a.buf_offset, buf + b.buf_offset,
                                       stride) < 0;
                       });
      res = p.get();
    } else {
      // We declare q above and populate it here because res might point to q
      // so we need to make sure that q outlives the end of this block.
      // Initally do *not* default initialize the arrays for performance.
      q.reset(new SortToken[num_indices]);
      std::unique_ptr<uint32_t[]> counts(new uint32_t[1 << 16]);
      res = base::RadixSort(
          p.get(), p.get() + num_indices, q.get(), counts.get(), stride,
          [buf](const SortToken& token) { return buf + token.buf_offset; });
    }

    for (uint32_t i = 0; i < num_indices; ++i) {
      indices.b[i] = res[i].index;
    }
  }

  template <typename N>
  PERFETTO_ALWAYS_INLINE uint32_t
  IndexToStorageIndex(uint32_t index,
                      const Column& column,
                      const Slab<uint32_t>* popcnt) {
    if constexpr (std::is_same_v<N, NonNull>) {
      base::ignore_result(popcnt);
      return index;
    } else if constexpr (std::is_same_v<N, SparseNull>) {
      const auto& null_storage =
          column.null_storage.unchecked_get<dataframe::SparseNull>();
      const BitVector& bv = null_storage.bit_vector;
      if (!bv.is_set(index)) {
        // Null values are always less than non-null values.
        return std::numeric_limits<uint32_t>::max();
      }
      return static_cast<uint32_t>((*popcnt)[index / 64] +
                                   bv.count_set_bits_until_in_word(index));
    } else if constexpr (std::is_same_v<N, DenseNull>) {
      base::ignore_result(popcnt);
      const auto& null_storage =
          column.null_storage.unchecked_get<dataframe::DenseNull>();
      return null_storage.bit_vector.is_set(index)
                 ? index
                 : std::numeric_limits<uint32_t>::max();
    } else {
      static_assert(std::is_same_v<N, NonNull>, "Unsupported type");
    }
  }

  template <typename T, typename Op>
  PERFETTO_ALWAYS_INLINE void FindMinMaxIndex(
      const bytecode::FindMinMaxIndex<T, Op>& bytecode) {
    using B = bytecode::FindMinMaxIndexBase;  // Use base for arg names
    uint32_t col = bytecode.template arg<B::col>();
    auto& indices =
        ReadFromRegister(bytecode.template arg<B::update_register>());
    if (indices.empty()) {
      return;
    }

    const auto* data = GetColumn(col).storage.template unchecked_data<T>();
    auto get_value = [&](uint32_t idx) {
      if constexpr (std::is_same_v<T, Id>) {
        base::ignore_result(data);
        return idx;
      } else if constexpr (std::is_same_v<T, String>) {
        return state_.string_pool->Get(data[idx]);
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

  template <typename T>
  PERFETTO_ALWAYS_INLINE void LinearFilterEq(
      const bytecode::LinearFilterEqBase& leq) {
    using B = bytecode::LinearFilterEqBase;

    Span<uint32_t>& span = ReadFromRegister(leq.arg<B::update_register>());
    Range range = ReadFromRegister(leq.arg<B::source_register>());
    PERFETTO_DCHECK(range.size() <= span.size());

    const auto& res = ReadFromRegister(leq.arg<B::filter_value_reg>());
    if (!HandleInvalidCastFilterValueResult(res.validity, range)) {
      std::iota(span.b, span.b + range.size(), range.b);
      span.e = span.b + range.size();
      return;
    }

    const Column& column = GetColumn(leq.arg<B::col>());
    const auto* data = column.storage.template unchecked_data<T>();

    using Compare = std::remove_cv_t<std::remove_reference_t<decltype(*data)>>;
    using M = StorageType::VariantTypeAtIndex<T, CastFilterValueResult::Value>;
    const auto& value = base::unchecked_get<M>(res.value);
    Compare to_compare;
    if constexpr (std::is_same_v<T, String>) {
      auto id = state_.string_pool->GetId(value);
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

  template <typename Op>
  PERFETTO_ALWAYS_INLINE uint32_t* FilterStringOp(const StringPool::Id* data,
                                                  const uint32_t* begin,
                                                  const uint32_t* end,
                                                  uint32_t* output,
                                                  const char* val) {
    if constexpr (std::is_same_v<Op, Eq>) {
      return StringFilterEq(data, begin, end, output, val);
    } else if constexpr (std::is_same_v<Op, Ne>) {
      return StringFilterNe(data, begin, end, output, val);
    } else if constexpr (std::is_same_v<Op, Glob>) {
      return StringFilterGlob(data, begin, end, output, val);
    } else if constexpr (std::is_same_v<Op, Regex>) {
      return StringFilterRegex(data, begin, end, output, val);
    } else {
      return Filter(data, begin, end, output, NullTermStringView(val),
                    comparators::StringComparator<Op>{state_.string_pool});
    }
  }

  PERFETTO_ALWAYS_INLINE uint32_t* StringFilterEq(const StringPool::Id* data,
                                                  const uint32_t* begin,
                                                  const uint32_t* end,
                                                  uint32_t* output,
                                                  const char* val) {
    std::optional<StringPool::Id> id =
        state_.string_pool->GetId(base::StringView(val));
    if (!id) {
      return output;
    }
    static_assert(sizeof(StringPool::Id) == 4, "Id should be 4 bytes");
    return Filter(reinterpret_cast<const uint32_t*>(data), begin, end, output,
                  id->raw_id(), std::equal_to<>());
  }

  PERFETTO_ALWAYS_INLINE uint32_t* StringFilterNe(const StringPool::Id* data,
                                                  const uint32_t* begin,
                                                  const uint32_t* end,
                                                  uint32_t* output,
                                                  const char* val) {
    std::optional<StringPool::Id> id =
        state_.string_pool->GetId(base::StringView(val));
    if (!id) {
      memcpy(output, begin, size_t(end - begin));
      return output + (end - begin);
    }
    static_assert(sizeof(StringPool::Id) == 4, "Id should be 4 bytes");
    return Filter(reinterpret_cast<const uint32_t*>(data), begin, end, output,
                  id->raw_id(), std::not_equal_to<>());
  }

  PERFETTO_ALWAYS_INLINE uint32_t* StringFilterGlob(const StringPool::Id* data,
                                                    const uint32_t* begin,
                                                    const uint32_t* end,
                                                    uint32_t* output,
                                                    const char* val) {
    auto matcher = util::GlobMatcher::FromPattern(val);
    // If glob pattern doesn't involve any special characters, the function
    // called should be equality.
    if (matcher.IsEquality()) {
      return StringFilterEq(data, begin, end, output, val);
    }
    // For very big string pools (or small ranges) or pools with large
    // strings run a standard glob function.
    if (size_t(end - begin) < state_.string_pool->size() ||
        state_.string_pool->HasLargeString()) {
      return Filter(data, begin, end, output, matcher,
                    comparators::Glob{state_.string_pool});
    }
    // TODO(lalitm): the BitVector can be placed in a register removing to
    // need to allocate every time.
    auto matches = BitVector::CreateWithSize(
        state_.string_pool->MaxSmallStringId().raw_id());
    PERFETTO_DCHECK(!state_.string_pool->HasLargeString());
    for (auto it = state_.string_pool->CreateSmallStringIterator(); it; ++it) {
      auto id = it.StringId();
      matches.change_assume_unset(id.raw_id(),
                                  matcher.Matches(state_.string_pool->Get(id)));
    }
    return Filter(data, begin, end, output, matches,
                  comparators::GlobFullStringPool{});
  }

  PERFETTO_ALWAYS_INLINE uint32_t* StringFilterRegex(const StringPool::Id* data,
                                                     const uint32_t* begin,
                                                     const uint32_t* end,
                                                     uint32_t* output,
                                                     const char* val) {
    auto regex = regex::Regex::Create(val);
    if (!regex.ok()) {
      return output;
    }
    return Filter(data, begin, end, output, regex.value(),
                  comparators::Regex{state_.string_pool});
  }

  // Filters an existing index buffer in-place, based on data comparisons
  // performed using a separate set of source indices.
  //
  // This function iterates synchronously through two sets of indices:
  // 1. Source Indices: Provided by [begin, end), pointed to by `it`. These
  //    indices are used *only* to look up data values (`data[*it]`).
  // 2. Destination/Update Indices: Starting at `o_start`, pointed to by
  //    `o_read` (for reading the original index) and `o_write` (for writing
  //    (for reading the original index) and `o_write` (for writing kept
  //    indices). This buffer is modified *in-place*.
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
  //   - This function further filters the original table indices in
  //   `o_start`
  //     based on data comparisons using the translated indices.
  //
  // Args:
  //   data: Pointer to the start of the column's data storage.
  //   begin: Pointer to the first index in the source span (for data
  //   lookup). end: Pointer one past the last index in the source span.
  //   o_start: Pointer to the destination/update buffer (filtered
  //   in-place). value: The value to compare data against. comparator:
  //   Functor implementing the comparison logic.
  //
  // Returns:
  //   A pointer one past the last index written to the destination buffer.
  template <typename Comparator, typename ValueType, typename DataType>
  [[nodiscard]] PERFETTO_ALWAYS_INLINE static uint32_t* Filter(
      const DataType* data,
      const uint32_t* begin,
      const uint32_t* end,
      uint32_t* o_start,
      const ValueType& value,
      const Comparator& comparator) {
    const uint32_t* o_read = o_start;
    uint32_t* o_write = o_start;
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
  [[nodiscard]] PERFETTO_ALWAYS_INLINE static uint32_t* IdentityFilter(
      const uint32_t* begin,
      const uint32_t* end,
      uint32_t* o_start,
      const ValueType& value,
      Comparator comparator) {
    const uint32_t* o_read = o_start;
    uint32_t* o_write = o_start;
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

  // Attempts to cast a filter value to a numeric type, dispatching to the
  // appropriate type-specific conversion function.
  template <typename T>
  [[nodiscard]] PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
  CastFilterValueToIntegerOrDouble(
      FilterValueHandle handle,
      typename FilterValueFetcherImpl::Type filter_value_type,
      FilterValueFetcherImpl& fetcher,
      NonStringOp op,
      T& out) {
    if constexpr (std::is_same_v<T, double>) {
      return CastFilterValueToDouble(handle, filter_value_type, fetcher, op,
                                     out);
    } else if constexpr (std::is_integral_v<T>) {
      return CastFilterValueToInteger<T>(handle, filter_value_type, fetcher, op,
                                         out);
    } else {
      static_assert(std::is_same_v<T, double>, "Unsupported type");
    }
  }

  // Attempts to cast a filter value to an integer type, handling various
  // edge cases such as out-of-range values and non-integer inputs.
  template <typename T>
  [[nodiscard]] PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
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
    return CastStringOrNullFilterValueToIntegerOrDouble(filter_value_type, op);
  }

  // Attempts to cast a filter value to a double, handling integer inputs
  // and various edge cases.
  [[nodiscard]] PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
  CastFilterValueToDouble(
      FilterValueHandle filter_value_handle,
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
          out = iad_greater_than_i
                    ? iad
                    : std::nextafter(iad,
                                     std::numeric_limits<double>::infinity());
          return CastFilterValueResult::kValid;
        case NonStringOp::GetTypeIndex<Le>():
          out = iad_less_than_i
                    ? iad
                    : std::nextafter(iad,
                                     -std::numeric_limits<double>::infinity());
          return CastFilterValueResult::kValid;
        case NonStringOp::GetTypeIndex<Gt>():
          out = iad_less_than_i
                    ? iad
                    : std::nextafter(iad,
                                     -std::numeric_limits<double>::infinity());
          return CastFilterValueResult::kValid;
        case NonStringOp::GetTypeIndex<Ge>():
          out = iad_greater_than_i
                    ? iad
                    : std::nextafter(iad,
                                     std::numeric_limits<double>::infinity());
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
    return CastStringOrNullFilterValueToIntegerOrDouble(filter_value_type, op);
  }

  // Converts a double to an integer type using the specified function
  // (e.g., trunc, floor). Used as a helper for various casting operations.
  template <typename T, double (*fn)(double)>
  PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
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

  // Handles conversion of strings or nulls to integer or double types for
  // filtering operations.
  PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
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

  PERFETTO_ALWAYS_INLINE static CastFilterValueResult::Validity
  CastFilterValueToString(
      FilterValueHandle handle,
      typename FilterValueFetcherImpl::Type filter_value_type,
      FilterValueFetcherImpl& fetcher,
      const StringOp& op,
      const char*& out) {
    if (PERFETTO_LIKELY(filter_value_type ==
                        FilterValueFetcherImpl ::kString)) {
      out = fetcher.GetStringValue(handle.index);
      return CastFilterValueResult::kValid;
    }
    if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl ::kNull)) {
      // Nulls always compare false to any value (including other nulls),
      // regardless of the operator.
      return CastFilterValueResult::kNoneMatch;
    }
    if (PERFETTO_LIKELY(filter_value_type == FilterValueFetcherImpl ::kInt64 ||
                        filter_value_type ==
                            FilterValueFetcherImpl ::kDouble)) {
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

  template <typename T>
  PERFETTO_ALWAYS_INLINE T& ReadFromRegister(reg::RwHandle<T> r) {
    return state_.ReadFromRegister(r);
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE const T& ReadFromRegister(reg::ReadHandle<T> r) const {
    return state_.ReadFromRegister(r);
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE const T* MaybeReadFromRegister(
      reg::ReadHandle<T> reg) {
    return state_.MaybeReadFromRegister(reg);
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE T* MaybeReadFromRegister(reg::WriteHandle<T> reg) {
    return state_.MaybeReadFromRegister(reg);
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE void WriteToRegister(reg::WriteHandle<T> r, T value) {
    state_.WriteToRegister(r, std::move(value));
  }

  const Column& GetColumn(uint32_t idx) const { return state_.GetColumn(idx); }

  FilterValueFetcherImpl& fetcher_;
  InterpreterState& state_;
};

template <typename FilterValueFetcherImpl>
void Interpreter<FilterValueFetcherImpl>::Execute(
    FilterValueFetcherImpl& filter_value_fetcher) {
  InterpreterImpl<FilterValueFetcherImpl> impl(filter_value_fetcher, state_);
  impl.Execute();
}

}  // namespace perfetto::trace_processor::dataframe::impl::bytecode

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_IMPL_H_
