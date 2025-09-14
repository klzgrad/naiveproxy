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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_ADHOC_DATAFRAME_BUILDER_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_ADHOC_DATAFRAME_BUILDER_H_

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/impl/bit_vector.h"
#include "src/trace_processor/dataframe/impl/flex_vector.h"
#include "src/trace_processor/dataframe/impl/slab.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"

namespace perfetto::trace_processor::dataframe {

// Builds a `Dataframe` on an adhoc basis by allowing users to append
// values column by column.
//
// This class provides a flexible way to construct a `Dataframe` when data is
// in a partially columar format but still needs to be checked for typing and
// sorting/duplicates.
//
// If the data is purely in a row-oriented format, consider using
// `RuntimeDataframeBuilder` instead, which is optimized for that use case.
//
// Usage:
// 1. Construct an `AdhocDataframeBuilder` with column names and an optional
//    `StringPool` for string interning. Column types can be provided or
//    will be inferred from the first non-null value added to each column.
// 2. Append data to columns using `PushNonNull`, `PushNonNullUnchecked`, or
//    `PushNull`. These methods add values to the end of the respective columns.
//    Conceptually, ensure that each "row" has a value (or null) for every
//    column before moving to the next "row".
// 3. Call `Build()` to finalize the `Dataframe`. This method consumes the
//    builder and returns a `StatusOr<Dataframe>`. `Build()` analyzes the
//    collected data to optimize storage types (e.g., downcasting integers),
//    determine nullability overlays, and infer sort states.
//
// Example:
// ```
// StringPool pool;
// AdhocDataframeBuilder builder({"col_a", "col_b"}, &pool);
// builder.PushNonNull(0, 10);   // col_a, row 0
// builder.PushNonNull(1, "foo"); // col_b, row 0
// builder.PushNonNull(0, 20);   // col_a, row 1
// builder.PushNull(1);        // col_b, row 1 (null)
// auto df_status = std::move(builder).Build();
// if (df_status.ok()) {
//   Dataframe df = std::move(df_status.value());
//   // ... use df
// }
// ```
//
// Note:
// - The `StringPool` instance must remain valid for the lifetime of the
//   builder and the resulting `Dataframe`.
// - If `PushNonNull` returns false (e.g. due to type mismatch), the error
//   status can be retrieved using the `status()` method. The `Build()`
//   method will also propagate this error.
// - The builder is movable but not copyable.
class AdhocDataframeBuilder {
 public:
  enum class ColumnType : uint8_t {
    kInt64,
    kDouble,
    kString,
  };

  // Constructs a AdhocDataframeBuilder.
  //
  // Args:
  //   names: A vector of strings representing the names of the columns
  //          to be built. The order determines the column order as well.
  //   pool: A pointer to a `StringPool` instance used for interning
  //         string values encountered during row addition. Must remain
  //         valid for the lifetime of the builder and the resulting
  //         Dataframe.
  //  types: An optional vector of `ColumnType` specifying the types
  //         of the columns. If empty, types are inferred from the first
  //         non-null value added to each column. If provided, must match
  //         the size of `names`.
  AdhocDataframeBuilder(std::vector<std::string> names,
                        StringPool* pool,
                        const std::vector<ColumnType>& types = {})
      : string_pool_(pool), did_declare_types_(!types.empty()) {
    PERFETTO_DCHECK(types.empty() || types.size() == names.size());
    for (uint32_t i = 0; i < names.size(); ++i) {
      if (types.empty()) {
        column_states_.emplace_back();
      } else {
        switch (types[i]) {
          case ColumnType::kInt64:
            column_states_.emplace_back(
                ColumnState{impl::FlexVector<int64_t>(), {}});
            break;
          case ColumnType::kDouble:
            column_states_.emplace_back(
                ColumnState{impl::FlexVector<double>(), {}});
            break;
          case ColumnType::kString:
            column_states_.emplace_back(
                ColumnState{impl::FlexVector<StringPool::Id>(), {}});
            break;
        }
      }
    }
    for (auto& name : names) {
      column_names_.emplace_back(std::move(name));
    }
  }
  ~AdhocDataframeBuilder() = default;

  // Movable but not copyable
  AdhocDataframeBuilder(AdhocDataframeBuilder&&) noexcept = default;
  AdhocDataframeBuilder& operator=(AdhocDataframeBuilder&&) noexcept = default;
  AdhocDataframeBuilder(const AdhocDataframeBuilder&) = delete;
  AdhocDataframeBuilder& operator=(const AdhocDataframeBuilder&) = delete;

  // Appends `count` copies of `value` to the specified column `col`.
  //
  // Returns true on success, false on failure (e.g., if the column type
  // does not match the type of `value`). The failure status *must* be
  // retrieved using `status()` method.
  PERFETTO_ALWAYS_INLINE bool PushNonNull(uint32_t col,
                                          uint32_t value,
                                          uint32_t count = 1) {
    return PushNonNullInternal(col, int64_t(value), count);
  }
  PERFETTO_ALWAYS_INLINE bool PushNonNull(uint32_t col,
                                          int64_t value,
                                          uint32_t count = 1) {
    return PushNonNullInternal(col, value, count);
  }
  PERFETTO_ALWAYS_INLINE bool PushNonNull(uint32_t col,
                                          double value,
                                          uint32_t count = 1) {
    return PushNonNullInternal(col, value, count);
  }
  PERFETTO_ALWAYS_INLINE bool PushNonNull(uint32_t col,
                                          StringPool::Id value,
                                          uint32_t count = 1) {
    return PushNonNullInternal(col, value, count);
  }

  // Appends `count` copies of `value` to the specified column `col`.
  // This method does not check if the column has the correct type or try to do
  // any conversions. It is intended for use when the caller is certain that the
  // column type matches the type of `value`.
  PERFETTO_ALWAYS_INLINE void PushNonNullUnchecked(uint32_t col,
                                                   uint32_t value,
                                                   uint32_t count = 1) {
    PushNonNullUncheckedInternal(col, int64_t(value), count);
  }
  PERFETTO_ALWAYS_INLINE void PushNonNullUnchecked(uint32_t col,
                                                   int64_t value,
                                                   uint32_t count = 1) {
    PushNonNullUncheckedInternal(col, value, count);
  }
  PERFETTO_ALWAYS_INLINE void PushNonNullUnchecked(uint32_t col,
                                                   double value,
                                                   uint32_t count = 1) {
    PushNonNullUncheckedInternal(col, value, count);
  }
  PERFETTO_ALWAYS_INLINE void PushNonNullUnchecked(uint32_t col,
                                                   StringPool::Id value,
                                                   uint32_t count = 1) {
    PushNonNullUncheckedInternal(col, value, count);
  }

  // Appends `count` null values to the specified column `col`.
  PERFETTO_ALWAYS_INLINE void PushNull(uint32_t col, uint32_t count = 1) {
    auto& state = column_states_[col];
    if (PERFETTO_UNLIKELY(!state.null_overlay)) {
      EnsureNullOverlayExists(state);
    }
    state.null_overlay->push_back_multiple(false, count);
  }

  // Finalizes the builder and attempts to construct the Dataframe.
  // This method consumes the builder (note the && qualifier).
  //
  // Returns:
  //   StatusOr<Dataframe>: On success, contains the built `Dataframe`.
  //                        On failure (e.g., if `AddRow` previously failed),
  //                        contains an error status retrieved from `status()`.
  //
  // Implementation wise, the collected data for each column is analyzed to:
  // - Determine the final optimal storage type (e.g., downcasting int64_t to
  //   uint32_t/int32_t if possible, using Id type if applicable).
  // - Determine the final nullability overlay (NonNull or SparseNull).
  // - Determine the final sort state (IdSorted, SetIdSorted, Sorted, Unsorted)
  //   by analyzing the collected non-null values.
  // - Construct and return the final `Dataframe` instance.
  base::StatusOr<Dataframe> Build() && {
    uint64_t row_count = std::numeric_limits<uint64_t>::max();
    RETURN_IF_ERROR(current_status_);
    std::vector<std::shared_ptr<impl::Column>> columns;
    for (uint32_t i = 0; i < column_names_.size(); ++i) {
      auto& state = column_states_[i];
      size_t non_null_row_count;
      switch (state.data.index()) {
        case base::variant_index<DataVariant, std::nullopt_t>():
          non_null_row_count = 0;
          columns.emplace_back(std::make_shared<impl::Column>(impl::Column{
              impl::Storage{impl::FlexVector<uint32_t>()},
              CreateNullStorageFromBitvector(std::move(state.null_overlay)),
              Unsorted{},
              HasDuplicates{},
          }));
          break;
        case base::variant_index<DataVariant, impl::FlexVector<int64_t>>(): {
          auto& data =
              base::unchecked_get<impl::FlexVector<int64_t>>(state.data);
          non_null_row_count = data.size();
          duplicate_bit_vector_.clear();

          IntegerColumnSummary summary;
          summary.is_id_sorted = data.empty() || data[0] == 0;
          summary.is_setid_sorted = data.empty() || data[0] == 0;
          summary.is_sorted = true;
          summary.min = data.empty() ? 0 : data[0];
          summary.max = data.empty() ? 0 : data[0];
          summary.has_duplicates =
              data.empty() ? false : CheckDuplicate(data[0], data.size());
          summary.is_nullable = state.null_overlay.has_value();
          for (uint32_t j = 1; j < data.size(); ++j) {
            summary.is_id_sorted = summary.is_id_sorted && (data[j] == j);
            summary.is_setid_sorted = summary.is_setid_sorted &&
                                      (data[j] == data[j - 1] || data[j] == j);
            summary.is_sorted = summary.is_sorted && data[j - 1] <= data[j];
            summary.min = std::min(summary.min, data[j]);
            summary.max = std::max(summary.max, data[j]);
            summary.has_duplicates =
                summary.has_duplicates || CheckDuplicate(data[j], data.size());
          }
          auto integer = CreateIntegerStorage(std::move(data), summary);
          impl::SpecializedStorage specialized_storage =
              GetSpecializedStorage(integer, summary);
          columns.emplace_back(std::make_shared<impl::Column>(impl::Column{
              std::move(integer),
              CreateNullStorageFromBitvector(std::move(state.null_overlay)),
              GetIntegerSortStateFromProperties(summary),
              summary.is_nullable || summary.has_duplicates
                  ? DuplicateState{HasDuplicates{}}
                  : DuplicateState{NoDuplicates{}},
              std::move(specialized_storage),
          }));
          break;
        }
        case base::variant_index<DataVariant, impl::FlexVector<double>>(): {
          auto& data =
              base::unchecked_get<impl::FlexVector<double>>(state.data);
          non_null_row_count = data.size();

          bool is_nullable = state.null_overlay.has_value();
          bool is_sorted = true;
          for (uint32_t j = 1; j < data.size(); ++j) {
            is_sorted = is_sorted && data[j - 1] <= data[j];
          }
          columns.emplace_back(std::make_shared<impl::Column>(impl::Column{
              impl::Storage{std::move(data)},
              CreateNullStorageFromBitvector(std::move(state.null_overlay)),
              is_sorted && !is_nullable ? SortState{Sorted{}}
                                        : SortState{Unsorted{}},
              HasDuplicates{},
          }));
          break;
        }
        case base::variant_index<DataVariant,
                                 impl::FlexVector<StringPool::Id>>(): {
          auto& data =
              base::unchecked_get<impl::FlexVector<StringPool::Id>>(state.data);
          non_null_row_count = data.size();

          bool is_nullable = state.null_overlay.has_value();
          bool is_sorted = true;
          if (!data.empty()) {
            NullTermStringView prev = string_pool_->Get(data[0]);
            for (uint32_t j = 1; j < data.size(); ++j) {
              NullTermStringView curr = string_pool_->Get(data[j]);
              is_sorted = is_sorted && prev <= curr;
              prev = curr;
            }
          }
          columns.emplace_back(std::make_shared<impl::Column>(impl::Column{
              impl::Storage{std::move(data)},
              CreateNullStorageFromBitvector(std::move(state.null_overlay)),
              is_sorted && !is_nullable ? SortState{Sorted{}}
                                        : SortState{Unsorted{}},
              HasDuplicates{},
          }));
          break;
        }
        default:
          PERFETTO_FATAL("Unexpected data variant in column %u", i);
      }
      uint64_t current_row_count =
          state.null_overlay ? state.null_overlay->size() : non_null_row_count;
      if (row_count != std::numeric_limits<uint64_t>::max() &&
          current_row_count != row_count) {
        return base::ErrStatus(
            "Row count mismatch in column '%s'. Expected %" PRIu64
            ", got %" PRIu64 ".",
            column_names_[i].c_str(), row_count, current_row_count);
      }
      row_count = current_row_count;
    }
    if (row_count == std::numeric_limits<uint64_t>::max()) {
      row_count = 0;
    }
    // Create an implicit id column for acting as a primary key even if there
    // are no other id columns.
    column_names_.emplace_back("_auto_id");
    columns.emplace_back(std::make_shared<impl::Column>(impl::Column{
        impl::Storage{impl::Storage::Id{static_cast<uint32_t>(row_count)}},
        impl::NullStorage::NonNull{}, IdSorted{}, NoDuplicates{}}));
    return Dataframe(true, std::move(column_names_), std::move(columns),
                     static_cast<uint32_t>(row_count), string_pool_);
  }

  // Returns the current status of the builder.
  //
  // If `AddRow` returned `false`, this method can be used to retrieve the
  // `base::Status` object containing the error details (e.g., type mismatch).
  //
  // Returns:
  //   const base::Status&: The current status. `ok()` will be true unless
  //                        an error occurred during a previous `AddRow` call.
  const base::Status& status() const { return current_status_; }

 private:
  struct Null {};
  using DataVariant = std::variant<std::nullopt_t,
                                   impl::FlexVector<int64_t>,
                                   impl::FlexVector<double>,
                                   impl::FlexVector<StringPool::Id>>;
  struct ColumnState {
    DataVariant data = std::nullopt;
    std::optional<impl::BitVector> null_overlay;
  };
  struct IntegerColumnSummary {
    bool is_id_sorted = true;
    bool is_setid_sorted = true;
    bool is_sorted = true;
    int64_t min = 0;
    int64_t max = 0;
    bool has_duplicates = false;
    bool is_nullable = false;
  };

  static constexpr bool IsPerfectlyRepresentableAsDouble(int64_t res) {
    constexpr int64_t kMaxDoubleRepresentible = 1ull << 53;
    return res >= -kMaxDoubleRepresentible && res <= kMaxDoubleRepresentible;
  }

  // Appends `count` copies of `value` to the specified column `col`.
  //
  // Returns true on success, false on failure (e.g., if the column type
  // does not match the type of `value`). The failure status *must* be
  // retrieved using `status()` method.
  template <typename T>
  PERFETTO_ALWAYS_INLINE bool PushNonNullInternal(uint32_t col,
                                                  T value,
                                                  uint32_t count = 1) {
    auto& state = column_states_[col];
    auto& data = state.data;
    switch (data.index()) {
      case base::variant_index<DataVariant, std::nullopt_t>(): {
        data = impl::FlexVector<T>();
        auto& vec = base::unchecked_get<impl::FlexVector<T>>(data);
        vec.push_back_multiple(value, count);
        break;
      }
      case base::variant_index<DataVariant, impl::FlexVector<T>>(): {
        auto& vec = base::unchecked_get<impl::FlexVector<T>>(data);
        vec.push_back_multiple(value, count);
        break;
      }
      default: {
        if constexpr (std::is_same_v<T, double>) {
          if (std::holds_alternative<impl::FlexVector<int64_t>>(data)) {
            auto& vec = base::unchecked_get<impl::FlexVector<int64_t>>(data);
            auto res = impl::FlexVector<double>::CreateWithSize(vec.size());
            for (uint32_t i = 0; i < vec.size(); ++i) {
              int64_t v = vec[i];
              if (!IsPerfectlyRepresentableAsDouble(v)) {
                current_status_ =
                    base::ErrStatus("Unable to represent %" PRId64
                                    " in column '%s' at row %u as a double.",
                                    v, column_names_[col].c_str(), i);
                return false;
              }
              res[i] = static_cast<double>(v);
            }
            res.push_back_multiple(value, count);
            data = std::move(res);
            break;
          }
        } else if constexpr (std::is_same_v<T, int64_t>) {
          if (std::holds_alternative<impl::FlexVector<double>>(data)) {
            auto& vec = base::unchecked_get<impl::FlexVector<double>>(data);
            if (!IsPerfectlyRepresentableAsDouble(value)) {
              current_status_ = base::ErrStatus(
                  "Inserting a too-large integer (%" PRId64
                  ") in column '%s' at row %" PRIu64
                  ". Column currently holds doubles.",
                  value, column_names_[col].c_str(), vec.size());
              return false;
            }
            vec.push_back_multiple(static_cast<double>(value), count);
            break;
          }
        }
        if (did_declare_types_) {
          current_status_ = base::ErrStatus(
              "column '%s' declared as %s in the schema, but %s found",
              column_names_[col].c_str(), ToString(data), ToString<T>());
        } else {
          current_status_ = base::ErrStatus(
              "column '%s' was inferred to be %s, but later received a value "
              "of type %s",
              column_names_[col].c_str(), ToString(data), ToString<T>());
        }
        return false;
      }
    }
    if (PERFETTO_UNLIKELY(state.null_overlay)) {
      state.null_overlay->push_back_multiple(true, count);
    }
    return true;
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE void PushNonNullUncheckedInternal(uint32_t col,
                                                           T value,
                                                           uint32_t count) {
    auto& state = column_states_[col];
    PERFETTO_DCHECK(std::holds_alternative<impl::FlexVector<T>>(state.data));
    auto& data = base::unchecked_get<impl::FlexVector<T>>(state.data);
    data.push_back_multiple(value, count);
    if (PERFETTO_UNLIKELY(state.null_overlay)) {
      state.null_overlay->push_back_multiple(true, count);
    }
  }

  static impl::Storage CreateIntegerStorage(
      impl::FlexVector<int64_t> data,
      const IntegerColumnSummary& summary) {
    // TODO(lalitm): `!summary.is_nullable` is an unnecesarily strong condition
    // but we impose it as query planning assumes that id columns never have an
    // index added to them.
    if (summary.is_id_sorted && !summary.is_nullable) {
      return impl::Storage{
          impl::Storage::Id{static_cast<uint32_t>(data.size())}};
    }
    if (IsRangeFullyRepresentableByType<uint32_t>(summary.min, summary.max)) {
      return impl::Storage{
          impl::Storage::Uint32{DowncastFromInt64<uint32_t>(data)}};
    }
    if (IsRangeFullyRepresentableByType<int32_t>(summary.min, summary.max)) {
      return impl::Storage{
          impl::Storage::Int32{DowncastFromInt64<int32_t>(data)}};
    }
    return impl::Storage{impl::Storage::Int64{std::move(data)}};
  }

  static impl::NullStorage CreateNullStorageFromBitvector(
      std::optional<impl::BitVector> bit_vector) {
    if (bit_vector) {
      return impl::NullStorage{
          impl::NullStorage::SparseNull{*std::move(bit_vector), {}}};
    }
    return impl::NullStorage{impl::NullStorage::NonNull{}};
  }

  template <typename T>
  static bool IsRangeFullyRepresentableByType(int64_t min, int64_t max) {
    // The <= for max is intentional because we're checking representability
    // of min/max, not looping or similar.
    PERFETTO_DCHECK(min <= max);
    return min >= std::numeric_limits<T>::min() &&
           max <= std::numeric_limits<T>::max();
  }

  template <typename T>
  PERFETTO_NO_INLINE static impl::FlexVector<T> DowncastFromInt64(
      const impl::FlexVector<int64_t>& data) {
    auto res = impl::FlexVector<T>::CreateWithSize(data.size());
    for (uint32_t i = 0; i < data.size(); ++i) {
      PERFETTO_DCHECK(IsRangeFullyRepresentableByType<T>(data[i], data[i]));
      res[i] = static_cast<T>(data[i]);
    }
    return res;
  }

  static SortState GetIntegerSortStateFromProperties(
      const IntegerColumnSummary& summary) {
    if (summary.is_nullable) {
      return SortState{Unsorted{}};
    }
    if (summary.is_id_sorted) {
      PERFETTO_DCHECK(summary.is_setid_sorted);
      PERFETTO_DCHECK(summary.is_sorted);
      return SortState{IdSorted{}};
    }
    if (summary.is_setid_sorted) {
      PERFETTO_DCHECK(summary.is_sorted);
      return SortState{SetIdSorted{}};
    }
    if (summary.is_sorted) {
      return SortState{Sorted{}};
    }
    return SortState{Unsorted{}};
  }

  static impl::SpecializedStorage GetSpecializedStorage(
      const impl::Storage& storage,
      const IntegerColumnSummary& summary) {
    // If we're already sorted or setid_sorted, we don't need specialized
    // storage.
    if (summary.is_id_sorted || summary.is_setid_sorted) {
      return impl::SpecializedStorage{};
    }

    // Check if we meet the hard conditions for small value eq.
    if (storage.type().Is<Uint32>() && summary.is_sorted &&
        !summary.is_nullable && !summary.has_duplicates) {
      const auto& vec = storage.unchecked_get<Uint32>();

      // For memory reasons, we only use small value eq if the ratio between
      // the maximum value and the number of values is "small enough".
      if (static_cast<uint32_t>(summary.max) < 16 * vec.size()) {
        return BuildSmallValueEq(vec);
      }
    }
    // Otherwise, we cannot use specialized storage.
    return impl::SpecializedStorage{};
  }

  PERFETTO_NO_INLINE static impl::SpecializedStorage::SmallValueEq
  BuildSmallValueEq(const impl::FlexVector<uint32_t>& data) {
    impl::SpecializedStorage::SmallValueEq offset_bv{
        impl::BitVector::CreateWithSize(data.empty() ? 0 : data.back() + 1,
                                        false),
        {},
    };
    for (uint32_t i : data) {
      offset_bv.bit_vector.set(i);
    }
    offset_bv.prefix_popcount = offset_bv.bit_vector.PrefixPopcount();
    return offset_bv;
  }

  PERFETTO_NO_INLINE static void EnsureNullOverlayExists(ColumnState& state) {
    uint64_t row_count;
    switch (state.data.index()) {
      case base::variant_index<DataVariant, std::nullopt_t>():
        row_count = 0;
        break;
      case base::variant_index<DataVariant, impl::FlexVector<int64_t>>():
        row_count =
            base::unchecked_get<impl::FlexVector<int64_t>>(state.data).size();
        break;
      case base::variant_index<DataVariant, impl::FlexVector<double>>():
        row_count =
            base::unchecked_get<impl::FlexVector<double>>(state.data).size();
        break;
      case base::variant_index<DataVariant, impl::FlexVector<StringPool::Id>>():
        row_count =
            base::unchecked_get<impl::FlexVector<StringPool::Id>>(state.data)
                .size();
        break;
      default:
        PERFETTO_FATAL("Unexpected data type in column state.");
    }
    state.null_overlay =
        impl::BitVector::CreateWithSize(static_cast<uint32_t>(row_count), true);
  }

  // Returns true if the value is a definite duplicate.
  PERFETTO_ALWAYS_INLINE bool CheckDuplicate(int64_t value, size_t size) {
    if (value < 0) {
      return true;
    }
    if (PERFETTO_UNLIKELY(value >=
                          static_cast<int64_t>(duplicate_bit_vector_.size()))) {
      if (value >= static_cast<int64_t>(16ll * size)) {
        return true;
      }
      duplicate_bit_vector_.push_back_multiple(
          false,
          static_cast<uint32_t>(value) - duplicate_bit_vector_.size() + 1);
    }
    if (duplicate_bit_vector_.is_set(static_cast<uint32_t>(value))) {
      return true;
    }
    duplicate_bit_vector_.set(static_cast<uint32_t>(value));
    return false;
  }

  PERFETTO_NO_INLINE static const char* ToString(const DataVariant&);

  template <typename T>
  PERFETTO_NO_INLINE static const char* ToString() {
    if constexpr (std::is_same_v<T, int64_t>) {
      return "LONG";
    } else if constexpr (std::is_same_v<T, double>) {
      return "DOUBLE";
    } else if constexpr (std::is_same_v<T, StringPool::Id>) {
      return "STRING";
    } else {
      return "unknown type";
    }
  }

  StringPool* string_pool_;
  std::vector<std::string> column_names_;
  std::vector<ColumnState> column_states_;
  bool did_declare_types_ = false;
  base::Status current_status_ = base::OkStatus();
  impl::BitVector duplicate_bit_vector_;
};

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_ADHOC_DATAFRAME_BUILDER_H_
