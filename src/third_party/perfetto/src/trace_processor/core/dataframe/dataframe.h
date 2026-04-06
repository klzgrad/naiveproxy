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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/cursor.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/util/bit_vector.h"

namespace perfetto::trace_processor::core::dataframe {

struct QueryPlanImpl;

// Dataframe is a columnar data structure for efficient querying and filtering
// of tabular data. It provides:
//
// - Type-specialized storage and filtering optimized for common trace data
//   patterns
// - Efficient query execution with optimized bytecode generation
// - Support for serializable query plans that separate planning from execution
// - Memory-efficient storage with support for specialized column types
class Dataframe {
 public:
  // QueryPlan encapsulates an executable, serializable representation of a
  // dataframe query operation. It contains the bytecode instructions and
  // metadata needed to execute a query.
  class QueryPlan {
   public:
    // Default constructor for an empty query plan.
    QueryPlan() = default;

    // Serializes the query plan to a string.
    std::string Serialize() const;

    // Deserializes a query plan from a string previously produced by
    // `Serialize()`.
    static QueryPlan Deserialize(std::string_view serialized);

    // Returns the underlying implementation for testing purposes.
    const QueryPlanImpl& GetImplForTesting() const;

    // The maximum number of rows it's possible for this query plan to return.
    uint32_t max_row_count() const;

    // The number of rows this query plan estimates it will return.
    uint32_t estimated_row_count() const;

    // Returns the bytecode instructions of the query plan as a vector of
    // strings, where each string represents a single bytecode instruction.
    std::vector<std::string> BytecodeToString() const;

    // An estimate for the cost of executing the query plan.
    double estimated_cost() const;

   private:
    friend class Dataframe;
    // Constructs a QueryPlan from its implementation.
    explicit QueryPlan(QueryPlanImpl plan) : plan_(std::move(plan)) {}
    // The underlying query plan implementation.
    QueryPlanImpl plan_;
  };

  // Constructs a Dataframe with the specified column names and types.
  Dataframe(StringPool* string_pool,
            uint32_t column_count,
            const char* const* column_names,
            const ColumnSpec* column_specs);

  // Creates a dataframe from a typed spec object.
  //
  // The spec specifies the column names and types of the dataframe.
  template <typename S>
  static Dataframe CreateFromTypedSpec(const S& spec, StringPool* pool) {
    static_assert(S::kColumnCount > 0,
                  "Dataframe must have at least one column type");
    return Dataframe(pool, S::kColumnCount, spec.column_names.data(),
                     spec.column_specs.data());
  }

  // Concatenates two dataframes horizontally by combining their columns.
  //
  // Both dataframes must have the same row count. The resulting dataframe
  // contains all columns from `left` followed by all columns from `right`,
  // excluding the `_auto_id` column from both (a new `_auto_id` is created).
  //
  // Args:
  //   left: The first dataframe. Ownership is taken via move.
  //   right: The second dataframe. Ownership is taken via move.
  //
  // Returns:
  //   A new dataframe containing columns from both inputs, or an error if
  //   row counts don't match.
  static base::StatusOr<Dataframe> HorizontalConcat(Dataframe&& left,
                                                    Dataframe&& right);

  // Selects rows at the given indices from this dataframe.
  //
  // Returns a new dataframe containing only the rows at the specified indices.
  // The indices must be valid (less than row_count()).
  Dataframe SelectRows(const uint32_t* indices, uint32_t count) &&;

  // Movable
  Dataframe(Dataframe&&) = default;
  Dataframe& operator=(Dataframe&&) = default;

  // Adds a new row to the dataframe with the specified values.
  //
  // Note: This function does not check the types of the values against the
  // column types. It is the caller's responsibility to ensure that the types
  // match. If the types do not match, the behavior is undefined.
  //
  // Generally, this function is only safe to call if the dataframe was
  // constructed using the public Dataframe constructor and not in other ways.
  //
  // Note: this function cannot be called on a finalized dataframe.
  //       See `Finalize()` for more details.
  template <typename D, typename... Args>
  PERFETTO_ALWAYS_INLINE void InsertUnchecked(const D&, Args... ts) {
    static_assert(
        std::is_convertible_v<std::tuple<Args...>, typename D::mutate_types>,
        "Insert types do not match the column types");
    PERFETTO_DCHECK(!finalized_);
    InsertUncheckedInternal<D>(std::make_index_sequence<sizeof...(Args)>(),
                               ts...);
  }

  // Creates an execution plan for querying the dataframe with specified filters
  // and column selection.
  //
  // Parameters:
  //   filter_specs:     Filter predicates to apply to the data.
  //   distinct_specs:   Distinct specifications to remove duplicate rows.
  //   sort_specs:       Sort specifications defining the desired row order.
  //   limit_spec:       Optional struct specifying LIMIT and OFFSET values.
  //   cols_used_bitmap: Bitmap where each bit corresponds to a column that may
  //                     be requested. Only columns with set bits can be
  //                     fetched.
  // Returns:
  //   A StatusOr containing the QueryPlan or an error status.
  base::StatusOr<QueryPlan> PlanQuery(
      std::vector<FilterSpec>& filter_specs,
      const std::vector<DistinctSpec>& distinct_specs,
      const std::vector<SortSpec>& sort_specs,
      const LimitSpec& limit_spec,
      uint64_t cols_used_bitmap) const;

  // Prepares a cursor for executing the query plan. The template parameter
  // `FilterValueFetcherImpl` is a subclass of `ValueFetcher` that defines the
  // logic for fetching filter values for each filter specs specified when
  // calling `PlanQuery`.
  //
  // Parameters:
  //   plan: The query plan to execute.
  //   c:    A reference to a std::optional that will be set to the prepared
  //         cursor.
  template <typename FilterValueFetcherImpl>
  void PrepareCursor(const QueryPlan& plan,
                     Cursor<FilterValueFetcherImpl>& c) const {
    c.Initialize(plan.plan_, static_cast<uint32_t>(column_ptrs_.size()),
                 column_ptrs_.data(), indexes_.data(), string_pool_);
  }

  // Given a typed spec, a column index and a row index, returns the value
  // stored in the dataframe at that position.
  //
  // Note: This function does not check the column type is compatible with the
  // specified spec. It is the caller's responsibility to ensure that the type
  // matches.
  //
  // Generally, this function is only safe to call if the dataframe was
  // constructed using the public Dataframe constructor and not in other ways.
  template <size_t column, typename D>
  PERFETTO_ALWAYS_INLINE auto GetCellUnchecked(const D&, uint32_t row) const {
    return GetCellUncheckedInternal<column, D>(row);
  }

  // Given a typed spec, a column index and a row index, returns the value
  // stored in the dataframe at that position.
  //
  // Note: This function does not check the column type is compatible with the
  // specified spec. It is the caller's responsibility to ensure that the type
  // matches.
  //
  // Generally, this function is only safe to call if the dataframe was
  // constructed using the public Dataframe constructor and not in other ways.
  //
  // Note: this function cannot be called on a finalized dataframe.
  //       See `Finalize()` for more details.
  template <size_t column, typename D>
  PERFETTO_ALWAYS_INLINE void SetCellUnchecked(
      const D&,
      uint32_t row,
      const typename D::template column_spec<column>::mutate_type& value) {
    SetCellUncheckedInternal<column, D>(row, value);
  }

  // Clears the dataframe, removing all rows and resetting the state.
  void Clear();

  // Makes an index which can speed up operations on this table. Note that
  // this function does *not* actually cause the index to be added or used, it
  // just returns it. Use `AddIndex` to add the index to the dataframe.
  //
  // Note that this index can be added to any dataframe with the same contents
  // (i.e. copies of this dataframe) not just the one it was created from.
  base::StatusOr<Index> BuildIndex(const uint32_t* columns_start,
                                   const uint32_t* columns_end) const;

  // Adds an index to the dataframe.
  //
  // Note: indexes can only be added to a finalized dataframe; it's
  // undefined behavior to call this on a non-finalized dataframe.
  void AddIndex(Index index);

  // Removes the index at the specified position.
  //
  // Note: indexes can only be removed from a finalized dataframe;it's
  // undefined behavior to call this on a non-finalized dataframe.
  void RemoveIndexAt(uint32_t);

  // Marks the dataframe as "finalized": a finalized dataframe cannot have any
  // more rows added to it (note this is different from being immutable as
  // indexes can be freely added and removed).
  //
  // If the dataframe is already finalized, this function does nothing.
  void Finalize();

  // Makes a copy of the dataframe which has been finalized. Unfinalized
  // dataframes *cannot* be copied, so this function will assert if not
  // finalized.
  //
  // This is a shallow copy, meaning that the contents of columns and indexes
  // are not duplicated, but the dataframe itself is a new instance.
  dataframe::Dataframe CopyFinalized() const;

  // Creates a spec object for this dataframe.
  DataframeSpec CreateSpec() const;

  // Returns whether the dataframe has been finalized.
  bool finalized() const { return finalized_; }

  // Returns the column names of the dataframe.
  const std::vector<std::string>& column_names() const { return column_names_; }

  // Returns the number of rows in the dataframe.
  uint32_t row_count() const { return row_count_; }

  // Returns the number of columns in the dataframe.
  uint32_t column_count() const {
    return static_cast<uint32_t>(column_ptrs_.size());
  }

  // Returns the total number of mutations to the dataframe. This includes
  // both non-column mutations (e.g. adding rows, adding indexes) and
  // column mutations (e.g. setting cell values).
  uint64_t mutations() const {
    uint64_t total = non_column_mutations_;
    for (const auto& col : columns_) {
      total += col->mutations;
    }
    return total;
  }

  // Gets the value of a cell at the specified row and column, calling the
  // appropriate callback method with the value.
  //
  // This method performs runtime type dispatch and is suitable for use cases
  // where the column type is not known at compile time.
  //
  // Note: for sparse null columns, this requires popcount support
  // (SparseNullWithPopcountAlways or SparseNullWithPopcountUntilFinalization).
  // Plain SparseNull columns will trigger a fatal error.
  template <typename CellCallbackImpl>
  void GetCell(uint32_t row, uint32_t col, CellCallbackImpl& callback) const {
    static_assert(std::is_base_of_v<CellCallback, CellCallbackImpl>,
                  "CellCallbackImpl must be a subclass of CellCallback");
    PERFETTO_DCHECK(row < row_count_);
    PERFETTO_DCHECK(col < column_ptrs_.size());

    const Column& column = *column_ptrs_[col];
    const Storage::DataPointer data_ptr = column.storage.data();
    const Nullability nullability = column.null_storage.nullability();

    // Handle nullability and compute storage index.
    uint32_t storage_idx;
    switch (nullability.index()) {
      case Nullability::GetTypeIndex<NonNull>():
        storage_idx = row;
        break;
      case Nullability::GetTypeIndex<DenseNull>(): {
        const auto& nulls = column.null_storage.unchecked_get<DenseNull>();
        if (!nulls.bit_vector.is_set(row)) {
          callback.OnCell(nullptr);
          return;
        }
        storage_idx = row;
        break;
      }
      case Nullability::GetTypeIndex<SparseNullWithPopcountAlways>():
      case Nullability::GetTypeIndex<
          SparseNullWithPopcountUntilFinalization>(): {
        const auto& nulls = column.null_storage.unchecked_get<SparseNull>();
        if (!nulls.bit_vector.is_set(row)) {
          callback.OnCell(nullptr);
          return;
        }
        storage_idx = static_cast<uint32_t>(
            nulls.prefix_popcount_for_cell_get[row / 64] +
            nulls.bit_vector.count_set_bits_until_in_word(row));
        break;
      }
      case Nullability::GetTypeIndex<SparseNull>():
        PERFETTO_FATAL(
            "SparseNull without popcount does not support random access");
      default:
        PERFETTO_FATAL("Unknown null storage type");
    }
    // Dispatch based on storage type.
    switch (data_ptr.index()) {
      case StorageType::GetTypeIndex<Id>():
        callback.OnCell(storage_idx);
        break;
      case StorageType::GetTypeIndex<Uint32>():
        callback.OnCell(Storage::CastDataPtr<Uint32>(data_ptr)[storage_idx]);
        break;
      case StorageType::GetTypeIndex<Int32>():
        callback.OnCell(Storage::CastDataPtr<Int32>(data_ptr)[storage_idx]);
        break;
      case StorageType::GetTypeIndex<Int64>():
        callback.OnCell(Storage::CastDataPtr<Int64>(data_ptr)[storage_idx]);
        break;
      case StorageType::GetTypeIndex<Double>():
        callback.OnCell(Storage::CastDataPtr<Double>(data_ptr)[storage_idx]);
        break;
      case StorageType::GetTypeIndex<String>(): {
        // See kStringNullLegacy in InsertUncheckedColumn.
        auto id = Storage::CastDataPtr<String>(data_ptr)[storage_idx];
        PERFETTO_DCHECK(!id.is_null());
        callback.OnCell(string_pool_->Get(id));
        break;
      }
      default:
        PERFETTO_FATAL("Invalid storage type");
    }
  }

  // Returns the nullability of a column at the specified index.
  //
  // DO NOT USE: this function only exists for legacy reasons and should not
  // be used in new code.
  Nullability GetNullabilityLegacy(uint32_t column) const {
    return columns_[column]->null_storage.nullability();
  }

  // Sets the value of a column at the specified row to the given value.
  //
  // DO NOT USE: this function only exists for legacy reasons and should not
  // be used in new code. Use `SetCellUnchecked` instead.
  template <typename T, typename N, typename M>
  void SetCellUncheckedLegacy(uint32_t col, uint32_t row, M value) {
    SetCellUncheckedInternal<T, N, M>(row, *column_ptrs_[col], value);
  }

  // Give a column name, returns the index of the column in the
  // dataframe, or std::nullopt if the column does not exist.
  //
  // DO NOT USE: this function only exists for legacy reasons and should not
  // be used in new code.
  std::optional<uint32_t> IndexOfColumnLegacy(std::string_view name) const {
    for (uint32_t i = 0; i < column_names_.size(); ++i) {
      if (column_names_[i] == name) {
        return i;
      }
    }
    return std::nullopt;
  }

 private:
  friend class AdhocDataframeBuilder;
  friend class TypedCursor;
  friend class QueryPlanBuilder;
  friend struct QueryPlanImpl;

  // TODO(lalitm): remove this once we have a proper static builder for
  // dataframe.
  friend class DataframeBytecodeTest;

  Dataframe(bool finalized,
            std::vector<std::string> column_names,
            std::vector<std::shared_ptr<Column>> columns,
            uint32_t row_count,
            StringPool* string_pool);

  template <typename D, typename... Args, size_t... Is>
  PERFETTO_ALWAYS_INLINE void InsertUncheckedInternal(
      std::index_sequence<Is...>,
      Args... ts) {
    PERFETTO_DCHECK(column_ptrs_.size() == sizeof...(ts));
    (InsertUncheckedColumn<
         typename std::tuple_element_t<Is, typename D::columns>, Is>(ts),
     ...);
    ++row_count_;
    ++non_column_mutations_;
  }

  template <typename D, size_t I>
  PERFETTO_ALWAYS_INLINE void InsertUncheckedColumn(
      typename D::non_null_mutate_type t) {
    static_assert(std::is_same_v<typename D::null_storage_type, NonNull>);
    using type = typename D::type;
    if constexpr (std::is_same_v<type, String>) {
      PERFETTO_DCHECK(!t.is_null());
    }
    auto& storage = columns_[I]->storage;
    if constexpr (std::is_same_v<type, Id>) {
      base::ignore_result(t);
      storage.unchecked_get<type>().size++;
    } else {
      storage.unchecked_get<type>().push_back(t);
    }
  }

  // String Null Handling (kStringNullLegacy)
  // =========================================
  // For legacy reasons, trace processor has two ways to represent null strings:
  // 1. std::nullopt (the standard way for nullable columns)
  // 2. StringPool::Id::Null() (a special sentinel value)
  //
  // Ideally, we would totally remove 2) but too much of trace processor now
  // depends on having access to it, so we must handle both representations.
  //
  // The dataframe normalizes these on write and validates on read:
  // - Insert/Set: StringPool::Id::Null() is silently converted to a true null
  //   for nullable columns. For NonNull columns, passing StringPool::Id::Null()
  //   triggers a DCHECK.
  // - Get: DCHECKs that values read from storage are never
  //   StringPool::Id::Null() (since we should have converted them on write).
  //
  // This ensures internal consistency while maintaining compatibility with code
  // that uses StringPool::Id::Null() to represent nulls.
  template <typename D, size_t I>
  PERFETTO_ALWAYS_INLINE void InsertUncheckedColumn(
      std::optional<typename D::non_null_mutate_type> t) {
    using type = typename D::type;
    using null_storage_type = typename D::null_storage_type;
    static_assert(!std::is_same_v<typename D::null_storage_type, NonNull>);

    auto& nulls = columns_[I]->null_storage.unchecked_get<null_storage_type>();
    auto& storage = columns_[I]->storage;

    // See kStringNullLegacy above.
    bool has_value = t.has_value();
    if constexpr (std::is_same_v<type, String>) {
      has_value = has_value && !t->is_null();
    }

    if (has_value) {
      if constexpr (std::is_same_v<type, Id>) {
        storage.unchecked_get<type>().size++;
      } else {
        storage.unchecked_get<type>().push_back(*t);
      }
    } else {
      if constexpr (std::is_same_v<null_storage_type, DenseNull>) {
        if constexpr (std::is_same_v<type, Id>) {
          storage.unchecked_get<type>().size++;
        } else {
          storage.unchecked_get<type>().push_back({});
        }
      }
    }

    static constexpr bool kIsSparseNullWithPopcount =
        std::is_same_v<null_storage_type, SparseNullWithPopcountAlways> ||
        std::is_same_v<null_storage_type,
                       SparseNullWithPopcountUntilFinalization>;
    if constexpr (kIsSparseNullWithPopcount) {
      if (nulls.bit_vector.size() % 64 == 0) {
        uint32_t prefix_popcount;
        if (nulls.bit_vector.size() == 0) {
          prefix_popcount = 0;
        } else {
          prefix_popcount =
              static_cast<uint32_t>(nulls.prefix_popcount_for_cell_get.back() +
                                    nulls.bit_vector.count_set_bits_in_word(
                                        nulls.bit_vector.size() - 1));
        }
        nulls.prefix_popcount_for_cell_get.push_back(prefix_popcount);
      }
    }
    nulls.bit_vector.push_back(has_value);
  }

  template <size_t column, typename D>
  PERFETTO_ALWAYS_INLINE auto GetCellUncheckedInternal(uint32_t row) const {
    using ColumnSpec = std::tuple_element_t<column, typename D::columns>;
    using type = typename ColumnSpec::type;
    using null_storage_type = typename ColumnSpec::null_storage_type;
    return GetCellUncheckedInternal<type, null_storage_type>(
        row, *column_ptrs_[column]);
  }

  template <size_t column, typename D>
  PERFETTO_ALWAYS_INLINE auto SetCellUncheckedInternal(
      uint32_t row,
      const typename D::template column_spec<column>::mutate_type& value) {
    using ColumnSpec = typename D::template column_spec<column>;
    using type = typename ColumnSpec::type;
    using null_storage_type = typename ColumnSpec::null_storage_type;
    using mutate_type = typename D::template column_spec<column>::mutate_type;

    // Changing the value of an Id column is not supported.
    static_assert(!std::is_same_v<type, Id>, "Cannot call set on Id column");
    SetCellUncheckedInternal<type, null_storage_type, mutate_type>(
        row, *column_ptrs_[column], value);
  }

  template <typename T, typename N>
  PERFETTO_ALWAYS_INLINE auto GetCellUncheckedInternal(
      uint32_t row,
      const Column& col) const {
    static constexpr bool is_sparse_null_supporting_get_always =
        std::is_same_v<N, SparseNullWithPopcountAlways>;
    static constexpr bool is_sparse_null_supporting_get_until_finalization =
        std::is_same_v<N, SparseNullWithPopcountUntilFinalization>;
    const auto& storage = col.storage.unchecked_get<T>();
    const auto& nulls = col.null_storage.unchecked_get<N>();
    // See kStringNullLegacy above.
    if constexpr (std::is_same_v<N, NonNull>) {
      auto result = GetCellUncheckedFromStorage(storage, row);
      if constexpr (std::is_same_v<T, String>) {
        PERFETTO_DCHECK(!result.is_null());
      }
      return result;
    } else if constexpr (std::is_same_v<N, DenseNull>) {
      using Ret = decltype(GetCellUncheckedFromStorage(storage, {}));
      if (nulls.bit_vector.is_set(row)) {
        auto result = GetCellUncheckedFromStorage(storage, row);
        if constexpr (std::is_same_v<T, String>) {
          PERFETTO_DCHECK(!result.is_null());
        }
        return std::make_optional(result);
      }
      return static_cast<std::optional<Ret>>(std::nullopt);
    } else if constexpr (is_sparse_null_supporting_get_always ||
                         is_sparse_null_supporting_get_until_finalization) {
      PERFETTO_DCHECK(is_sparse_null_supporting_get_always || !finalized_);
      using Ret = decltype(GetCellUncheckedFromStorage(storage, {}));
      if (nulls.bit_vector.is_set(row)) {
        auto index = static_cast<uint32_t>(
            nulls.prefix_popcount_for_cell_get[row / 64] +
            nulls.bit_vector.count_set_bits_until_in_word(row));
        auto result = GetCellUncheckedFromStorage(storage, index);
        if constexpr (std::is_same_v<T, String>) {
          PERFETTO_DCHECK(!result.is_null());
        }
        return std::make_optional(result);
      }
      return static_cast<std::optional<Ret>>(std::nullopt);
    } else if constexpr (std::is_same_v<N, SparseNull>) {
      static_assert(
          !std::is_same_v<N, N>,
          "Trying to access a column with sparse nulls but without an "
          "approach that supports it. Please use SparseNullWithPopcountAlways "
          "or SparseNullWithPopcountUntilFinalization as appropriate.");
    } else {
      static_assert(std::is_same_v<N, NonNull>,
                    "Unsupported null storage type");
    }
  }

  template <typename T, typename N, typename M>
  PERFETTO_ALWAYS_INLINE void SetCellUncheckedInternal(uint32_t row,
                                                       Column& col,
                                                       const M& value) {
    PERFETTO_DCHECK(!finalized_);

    // Make sure to increment the mutation count. This is important to let
    // others know that the column has been modified.
    ++col.mutations;

    auto& storage = col.storage.unchecked_get<T>();
    auto& nulls = col.null_storage.unchecked_get<N>();
    // See kStringNullLegacy above.
    if constexpr (std::is_same_v<N, NonNull>) {
      if constexpr (std::is_same_v<T, String>) {
        PERFETTO_DCHECK(!value.is_null());
      }
      storage[row] = value;
    } else if constexpr (std::is_same_v<N, DenseNull>) {
      bool has_value = value.has_value();
      if constexpr (std::is_same_v<T, String>) {
        has_value = has_value && !value->is_null();
      }
      if (has_value) {
        nulls.bit_vector.set(row);
        storage[row] = *value;
      } else {
        nulls.bit_vector.clear(row);
      }
    } else if constexpr (std::is_same_v<N, SparseNullWithPopcountAlways> ||
                         std::is_same_v<
                             N, SparseNullWithPopcountUntilFinalization>) {
      const auto& popcount = nulls.prefix_popcount_for_cell_get;
      uint32_t word = row / 64;
      auto storage_idx = static_cast<uint32_t>(
          popcount[word] + nulls.bit_vector.count_set_bits_until_in_word(row));
      const core::BitVector& bit_vector = nulls.bit_vector;
      bool has_value = value.has_value();
      if constexpr (std::is_same_v<T, String>) {
        has_value = has_value && !value->is_null();
      }
      if (has_value) {
        if (!bit_vector.is_set(row)) {
          storage.push_back({});
          memmove(storage.data() + storage_idx + 1,
                  storage.data() + storage_idx,
                  (storage.size() - storage_idx - 1) * sizeof(*storage.data()));
          for (uint32_t i = word + 1; i < popcount.size(); ++i) {
            nulls.prefix_popcount_for_cell_get[i]++;
          }
        }
        storage[storage_idx] = *value;
        nulls.bit_vector.set(row);
      } else {
        if (bit_vector.is_set(row)) {
          memmove(storage.data() + storage_idx,
                  storage.data() + storage_idx + 1,
                  (storage.size() - storage_idx - 1) * sizeof(*storage.data()));
          storage.pop_back();
          for (uint32_t i = word + 1; i < popcount.size(); ++i) {
            nulls.prefix_popcount_for_cell_get[i]--;
          }
        }
        nulls.bit_vector.clear(row);
      }
    } else if constexpr (std::is_same_v<N, SparseNull>) {
      static_assert(!std::is_same_v<N, N>,
                    "Trying to set a column with sparse nulls. This is not "
                    "supported, please use use another storage type.");
    } else {
      static_assert(std::is_same_v<N, NonNull>,
                    "Unsupported null storage type");
    }
  }

  template <typename C>
  PERFETTO_ALWAYS_INLINE auto GetCellUncheckedFromStorage(const C& column,
                                                          uint32_t row) const {
    if constexpr (std::is_same_v<C, Storage::Id>) {
      return row;
    } else {
      return column[row];
    }
  }

  static std::vector<std::shared_ptr<Column>> CreateColumnVector(
      const ColumnSpec*,
      uint32_t);

  // Private copy constructor for special methods.
  Dataframe(const Dataframe&) = default;
  Dataframe& operator=(const Dataframe&) = default;

  // The names of all columns.
  std::vector<std::string> column_names_;

  // Internal storage for columns in the dataframe.
  // Should have same size as `column_names_`.
  std::vector<std::shared_ptr<Column>> columns_;

  // Simple pointers to the columns for consumption by the cursor.
  // Should have same size as `column_names_`.
  std::vector<Column*> column_ptrs_;

  // List of indexes associated with the dataframe.
  std::vector<Index> indexes_;

  // Number of rows in the dataframe.
  uint32_t row_count_ = 0;

  // String pool for efficient string storage and interning.
  StringPool* string_pool_;

  // A count of the number of mutations to the dataframe (e.g. adding rows,
  // adding indexes). This does *not* include changes to values of the columns,
  // there is a separate mutation count for that.
  //
  // This is used to determine if the dataframe has changed since the
  // last time an external caller looked at it. This can allow invalidation of
  // external caches of things inside this dataframe.
  uint32_t non_column_mutations_ = 0;

  // Whether the dataframe is "finalized". See `Finalize()`.
  bool finalized_ = false;
};

}  // namespace perfetto::trace_processor::core::dataframe

namespace perfetto::trace_processor {

// Namespace alias for ergonomics.
namespace dataframe = core::dataframe;

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_H_
