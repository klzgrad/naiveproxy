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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_TYPES_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/impl/bit_vector.h"
#include "src/trace_processor/dataframe/impl/flex_vector.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/type_set.h"

namespace perfetto::trace_processor::dataframe::impl {

// Type categories for column content and operations.
// These define which operations can be applied to which content types.

// Set of content types that aren't string-based.
using NonStringType = TypeSet<Id, Uint32, Int32, Int64, Double>;

// Set of content types that are numeric in nature.
using IntegerOrDoubleType = TypeSet<Uint32, Int32, Int64, Double>;

// Set of operations applicable to non-null values.
using NonNullOp = TypeSet<Eq, Ne, Lt, Le, Gt, Ge, Glob, Regex>;

// Set of operations applicable to non-string values.
using NonStringOp = TypeSet<Eq, Ne, Lt, Le, Gt, Ge>;

// Set of operations applicable to string values.
using StringOp = TypeSet<Eq, Ne, Lt, Le, Gt, Ge, Glob, Regex>;

// Set of operations applicable to only string values.
using OnlyStringOp = TypeSet<Glob, Regex>;

// Set of operations applicable to ranges.
using RangeOp = TypeSet<Eq, Lt, Le, Gt, Ge>;

// Set of inequality operations (Lt, Le, Gt, Ge).
using InequalityOp = TypeSet<Lt, Le, Gt, Ge>;

// Set of null operations (IsNotNull, IsNull).
using NullOp = TypeSet<IsNotNull, IsNull>;

// Indicates an operation applies to both bounds of a range.
struct BothBounds {};

// Indicates an operation applies to the lower bound of a range.
struct BeginBound {};

// Indicates an operation applies to the upper bound of a range.
struct EndBound {};

// Which bounds should be modified by a range operation.
using BoundModifier = TypeSet<BothBounds, BeginBound, EndBound>;

// Represents a filter operation where we are performing an equality operation
// on a sorted column.
struct EqualRange {};

// Represents a filter operation where we are performing a lower bound operation
// on a sorted column.
struct LowerBound {};

// Represents a filter operation where we are performing an upper bound
// operation on a sorted column.
struct UpperBound {};

// Set of operations that can be applied to a sorted column.
using EqualRangeLowerBoundUpperBound =
    TypeSet<EqualRange, LowerBound, UpperBound>;

// Type tag indicating nulls should be placed at the start during
// partitioning/sorting.
struct NullsAtStart {};

// Type tag indicating nulls should be placed at the end during
// partitioning/sorting.
struct NullsAtEnd {};

// TypeSet defining the possible placement locations for nulls.
using NullsLocation = TypeSet<NullsAtStart, NullsAtEnd>;

// Type tag for finding the minimum value.
struct MinOp {};

// Type tag for finding the maximum value.
struct MaxOp {};

// TypeSet combining Min and Max operations.
using MinMaxOp = TypeSet<MinOp, MaxOp>;

// TypeSet containing all the non-id storage types.
using NonIdStorageType = TypeSet<Uint32, Int32, Int64, Double, String>;

// TypeSet which collapses all of the sparse nullability types into a single
// type.
using SparseNullCollapsedNullability = TypeSet<NonNull, SparseNull, DenseNull>;

// TypeSet of all possible sparse nullability states.
using SparseNullTypes = TypeSet<SparseNull,
                                SparseNullWithPopcountAlways,
                                SparseNullWithPopcountUntilFinalization>;

// Storage implementation for column data. Provides physical storage
// for different types of column content.
class Storage {
 public:
  // Storage representation for Id columns.
  struct Id {
    uint32_t size;  // Number of rows in the column

    static const void* data() { return nullptr; }
  };
  using Uint32 = FlexVector<uint32_t>;
  using Int32 = FlexVector<int32_t>;
  using Int64 = FlexVector<int64_t>;
  using Double = FlexVector<double>;
  using String = FlexVector<StringPool::Id>;

  using DataPointer = std::variant<std::nullptr_t,
                                   const uint32_t*,
                                   const int32_t*,
                                   const int64_t*,
                                   const double*,
                                   const StringPool::Id*>;

  Storage(Storage::Id data) : type_(dataframe::Id{}), data_(data) {}
  Storage(Storage::Uint32 data)
      : type_(dataframe::Uint32{}), data_(std::move(data)) {}
  Storage(Storage::Int32 data)
      : type_(dataframe::Int32{}), data_(std::move(data)) {}
  Storage(Storage::Int64 data)
      : type_(dataframe::Int64{}), data_(std::move(data)) {}
  Storage(Storage::Double data)
      : type_(dataframe::Double{}), data_(std::move(data)) {}
  Storage(Storage::String data)
      : type_(dataframe::String{}), data_(std::move(data)) {}

  // Type-safe access to storage with unchecked variant access.
  template <typename T>
  auto& unchecked_get() {
    using U = StorageType::VariantTypeAtIndex<T, Variant>;
    PERFETTO_DCHECK(std::holds_alternative<U>(data_));
    return base::unchecked_get<U>(data_);
  }

  template <typename T>
  const auto& unchecked_get() const {
    using U = StorageType::VariantTypeAtIndex<T, Variant>;
    PERFETTO_DCHECK(std::holds_alternative<U>(data_));
    return base::unchecked_get<U>(data_);
  }

  // Get raw pointer to storage data for a specific type.
  template <typename T>
  auto* unchecked_data() {
    return unchecked_get<T>().data();
  }

  template <typename T>
  const auto* unchecked_data() const {
    return unchecked_get<T>().data();
  }

  // Returns a variant containing pointer to the underlying data.
  // Returns nullptr if the storage type is Id (which has no buffer).
  DataPointer data() const {
    switch (type_.index()) {
      case StorageType::GetTypeIndex<dataframe::Id>():
        return nullptr;
      case StorageType::GetTypeIndex<dataframe::Uint32>():
        return base::unchecked_get<Storage::Uint32>(data_).data();
      case StorageType::GetTypeIndex<dataframe::Int32>():
        return base::unchecked_get<Storage::Int32>(data_).data();
      case StorageType::GetTypeIndex<dataframe::Int64>():
        return base::unchecked_get<Storage::Int64>(data_).data();
      case StorageType::GetTypeIndex<dataframe::Double>():
        return base::unchecked_get<Storage::Double>(data_).data();
      case StorageType::GetTypeIndex<dataframe::String>():
        return base::unchecked_get<Storage::String>(data_).data();
      default:
        PERFETTO_FATAL("Should not reach here");
    }
  }

  template <typename T>
  static auto* CastDataPtr(const DataPointer& ptr) {
    using U = StorageType::VariantTypeAtIndex<T, DataPointer>;
    return base::unchecked_get<U>(ptr);
  }

  StorageType type() const { return type_; }

 private:
  // Variant containing all possible storage representations.
  using Variant = std::variant<Id, Uint32, Int32, Int64, Double, String>;
  StorageType type_;
  Variant data_;
};

// Stores any information about nulls in the column.
class NullStorage {
 private:
  template <typename T>
  static constexpr uint32_t TypeIndex() {
    return base::variant_index<Variant, T>();
  }

 public:
  // Used for non-null columns which don't need any storage for nulls.
  struct NonNull {};

  // Used for nullable columns where nulls do *not* reserve a slot in `Storage`.
  struct SparseNull {
    // 1 = non-null element in storage.
    // 0 = null with no corresponding entry in storage.
    BitVector bit_vector;

    // For each word in the bit vector, this contains the indices of the
    // corresponding elements in `Storage` that are set.
    //
    // Note: this vector exists for a *very specific* usecase: when we need to
    // handle a GetCell() call on a column which is sparsely null. Note that
    // this *cannot* be used for SetCell columns because that would be O(n) and
    // very inefficient. In those cases, we need to use DenseNull and accept the
    // memory bloat.
    FlexVector<uint32_t> prefix_popcount_for_cell_get;
  };

  // Used for nullable columns where nulls reserve a slot in `Storage`.
  struct DenseNull {
    // 1 = non-null element in storage.
    // 0 = null with entry in storage with unspecified value
    BitVector bit_vector;
  };

  NullStorage(NonNull n) : nullability_(dataframe::NonNull{}), data_(n) {}
  NullStorage(SparseNull s, dataframe::SparseNull = dataframe::SparseNull{})
      : nullability_(dataframe::SparseNull{}), data_(std::move(s)) {}
  NullStorage(SparseNull s, dataframe::SparseNullWithPopcountAlways)
      : nullability_(dataframe::SparseNullWithPopcountAlways{}),
        data_(std::move(s)) {}
  NullStorage(SparseNull s, dataframe::SparseNullWithPopcountUntilFinalization)
      : nullability_(dataframe::SparseNullWithPopcountUntilFinalization{}),
        data_(std::move(s)) {}
  NullStorage(DenseNull d)
      : nullability_(dataframe::DenseNull{}), data_(std::move(d)) {}

  // Type-safe unchecked access to variant data.
  template <typename T>
  auto& unchecked_get() {
    if constexpr (std::is_same_v<T, dataframe::NonNull>) {
      return base::unchecked_get<NonNull>(data_);
    } else if constexpr (
        std::is_same_v<T, dataframe::SparseNull> ||
        std::is_same_v<T, dataframe::SparseNullWithPopcountAlways> ||
        std::is_same_v<T, dataframe::SparseNullWithPopcountUntilFinalization>) {
      return base::unchecked_get<SparseNull>(data_);
    } else if constexpr (std::is_same_v<T, dataframe::DenseNull>) {
      return base::unchecked_get<DenseNull>(data_);
    } else {
      static_assert(!std::is_same_v<T, T>, "Invalid type");
    }
  }

  template <typename T>
  const auto& unchecked_get() const {
    if constexpr (std::is_same_v<T, dataframe::NonNull>) {
      return base::unchecked_get<NonNull>(data_);
    } else if constexpr (
        std::is_same_v<T, dataframe::SparseNull> ||
        std::is_same_v<T, dataframe::SparseNullWithPopcountAlways> ||
        std::is_same_v<T, dataframe::SparseNullWithPopcountUntilFinalization>) {
      return base::unchecked_get<SparseNull>(data_);
    } else if constexpr (std::is_same_v<T, dataframe::DenseNull>) {
      return base::unchecked_get<DenseNull>(data_);
    } else {
      static_assert(!std::is_same_v<T, T>, "Invalid type");
    }
  }

  BitVector& GetNullBitVector() {
    switch (data_.index()) {
      case TypeIndex<SparseNull>():
        return unchecked_get<dataframe::SparseNull>().bit_vector;
      case TypeIndex<DenseNull>():
        return unchecked_get<dataframe::DenseNull>().bit_vector;
      default:
        PERFETTO_FATAL("Unsupported overlay type");
    }
  }
  const BitVector& GetNullBitVector() const {
    switch (data_.index()) {
      case TypeIndex<SparseNull>():
        return unchecked_get<dataframe::SparseNull>().bit_vector;
      case TypeIndex<DenseNull>():
        return unchecked_get<dataframe::DenseNull>().bit_vector;
      default:
        PERFETTO_FATAL("Unsupported overlay type");
    }
  }

  const BitVector* MaybeGetNullBitVector() const {
    switch (data_.index()) {
      case TypeIndex<SparseNull>():
        return &unchecked_get<dataframe::SparseNull>().bit_vector;
      case TypeIndex<DenseNull>():
        return &unchecked_get<dataframe::DenseNull>().bit_vector;
      case TypeIndex<NonNull>():
        return nullptr;
      default:
        PERFETTO_FATAL("Unsupported overlay type");
    }
  }

  Nullability nullability() const { return nullability_; }

 private:
  // Variant containing all possible overlay types.
  using Variant = std::variant<NonNull, SparseNull, DenseNull>;
  Nullability nullability_;
  Variant data_;
};

// Holds a specialized alternative representation of the storage of a column.
//
// Should be used to speed up very common operations on columns that have
// specific properties.
struct SpecializedStorage {
 public:
  // Special data structure capable of giving very fast results for equality
  // constraints on sorted, non-duplicate columns with not-too large values.
  // This is very common when joining two tables together by id.
  //
  // Usable in situations where the column has all the following properties:
  //  1) It's non-null.
  //  2) It's sorted.
  //  3) It has no duplicate values.
  //  4) The max(value) is "reasonably small".
  //     - as the the memory used will be O(max(value)) *not* O(size(column)).
  struct SmallValueEq {
    // BitVector with 1s representing the presence of a value in the
    // column. The value is the index of the value in the column.
    //
    // For example, if the column has values [1, 2, 3], then the bit vector
    // will have 1s at indices 1, 2, and 3.
    BitVector bit_vector;

    // Cumulative count of set bits in the bit vector. Key to allowing O(1)
    // equality queries.
    //
    // See BitVector::PrefixPopcount() for details.
    Slab<uint32_t> prefix_popcount;
  };

  SpecializedStorage() = default;
  SpecializedStorage(SmallValueEq data) : data_(std::move(data)) {}

  template <typename T>
  bool Is() const {
    return std::holds_alternative<T>(data_);
  }

  template <typename T>
  const T& unchecked_get() const {
    PERFETTO_DCHECK(Is<T>());
    return base::unchecked_get<T>(data_);
  }

 private:
  using Variant = std::variant<std::monostate, SmallValueEq>;
  Variant data_;
};

// Represents a complete column in the dataframe.
struct Column {
  Storage storage;
  NullStorage null_storage;
  SortState sort_state;
  DuplicateState duplicate_state;
  SpecializedStorage specialized_storage = SpecializedStorage{};
  uint32_t mutations = 0;
};

// Handle for referring to a filter value during query execution.
struct FilterValueHandle {
  uint32_t index;  // Index into the filter value array
};

// Result of casting a filter value for comparison during query execution.
struct CastFilterValueResult {
  enum Validity : uint8_t { kValid, kAllMatch, kNoneMatch };

  // Cast value for Id columns.
  struct Id {
    bool operator==(const Id& other) const { return value == other.value; }
    uint32_t value;
  };
  using Value =
      std::variant<Id, uint32_t, int32_t, int64_t, double, const char*>;

  bool operator==(const CastFilterValueResult& other) const {
    return validity == other.validity && value == other.value;
  }

  static CastFilterValueResult Valid(Value value) {
    return CastFilterValueResult{Validity::kValid, std::move(value)};
  }
  static CastFilterValueResult NoneMatch() {
    return CastFilterValueResult{Validity::kNoneMatch, Id{0}};
  }
  static CastFilterValueResult AllMatch() {
    return CastFilterValueResult{Validity::kAllMatch, Id{0}};
  }

  // Status of the casting result.
  Validity validity;

  // Variant of all possible cast value types.
  Value value;
};

// Result of an operation that yields multiple values (e.g. from an IN clause).
struct CastFilterValueListResult {
  using Value = std::variant<CastFilterValueResult::Id,
                             uint32_t,
                             int32_t,
                             int64_t,
                             double,
                             StringPool::Id>;
  using ValueList = std::variant<FlexVector<CastFilterValueResult::Id>,
                                 FlexVector<uint32_t>,
                                 FlexVector<int32_t>,
                                 FlexVector<int64_t>,
                                 FlexVector<double>,
                                 FlexVector<StringPool::Id>>;

  static CastFilterValueListResult Valid(ValueList v) {
    return CastFilterValueListResult{CastFilterValueResult::Validity::kValid,
                                     std::move(v)};
  }
  static CastFilterValueListResult NoneMatch() {
    return CastFilterValueListResult{
        CastFilterValueResult::Validity::kNoneMatch,
        FlexVector<CastFilterValueResult::Id>()};
  }
  static CastFilterValueListResult AllMatch() {
    return CastFilterValueListResult{CastFilterValueResult::Validity::kAllMatch,
                                     FlexVector<CastFilterValueResult::Id>()};
  }
  CastFilterValueResult::Validity validity;
  ValueList value_list;
};

// Represents a contiguous range of indices [b, e).
// Used for efficient representation of sequential row indices.
struct Range {
  uint32_t b;  // Beginning index (inclusive)
  uint32_t e;  // Ending index (exclusive)

  // Get the number of elements in the range.
  size_t size() const { return e - b; }
  bool empty() const { return b == e; }
};

// Represents a contiguous sequence of elements of an arbitrary type T.
// Basically a very simple backport of std::span to C++17.
template <typename T>
struct Span {
  using value_type = T;
  using const_iterator = T*;

  T* b;
  T* e;

  Span(T* _b, T* _e) : b(_b), e(_e) {}

  T* begin() const { return b; }
  T* end() const { return e; }
  size_t size() const { return static_cast<size_t>(e - b); }
  bool empty() const { return b == e; }
};

}  // namespace perfetto::trace_processor::dataframe::impl

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_TYPES_H_
