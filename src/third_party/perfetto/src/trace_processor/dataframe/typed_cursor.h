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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_TYPED_CURSOR_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_TYPED_CURSOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/cursor.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/value_fetcher.h"

namespace perfetto::trace_processor::dataframe {

class Dataframe;

// A typed version of `Cursor` which allows typed access and mutation of
// dataframe cells while iterating over the rows of the dataframe.
class TypedCursor {
 public:
  using FilterValue =
      std::variant<std::nullptr_t, int64_t, double, const char*>;
  struct Fetcher : ValueFetcher {
    using Type = size_t;
    static const Type kInt64 = base::variant_index<FilterValue, int64_t>();
    static const Type kDouble = base::variant_index<FilterValue, double>();
    static const Type kString = base::variant_index<FilterValue, const char*>();
    static const Type kNull =
        base::variant_index<FilterValue, std::nullptr_t>();
    int64_t GetInt64Value(uint32_t col) const {
      return base::unchecked_get<int64_t>(filter_values_[col]);
    }
    double GetDoubleValue(uint32_t col) const {
      return base::unchecked_get<double>(filter_values_[col]);
    }
    const char* GetStringValue(uint32_t col) const {
      return base::unchecked_get<const char*>(filter_values_[col]);
    }
    Type GetValueType(uint32_t col) const {
      return filter_values_[col].index();
    }
    static bool IteratorInit(uint32_t) { PERFETTO_FATAL("Unsupported"); }
    static bool IteratorNext(uint32_t) { PERFETTO_FATAL("Unsupported"); }
    FilterValue* filter_values_;
  };

  TypedCursor(const Dataframe* dataframe,
              std::vector<FilterSpec> filter_specs,
              std::vector<SortSpec> sort_specs)
      : TypedCursor(dataframe,
                    std::move(filter_specs),
                    std::move(sort_specs),
                    false) {}

  TypedCursor(Dataframe* dataframe,
              std::vector<FilterSpec> filter_specs,
              std::vector<SortSpec> sort_specs)
      : TypedCursor(dataframe,
                    std::move(filter_specs),
                    std::move(sort_specs),
                    true) {}

  TypedCursor(const TypedCursor&) = delete;
  TypedCursor& operator=(const TypedCursor&) = delete;

  TypedCursor(TypedCursor&&) = delete;
  TypedCursor& operator=(TypedCursor&&) = delete;

  // Sets the filter value at the given index for the current query plan.
  PERFETTO_ALWAYS_INLINE void SetFilterValueUnchecked(uint32_t index,
                                                      uint32_t value) {
    SetFilterValueInternal(index, int64_t(value));
  }
  PERFETTO_ALWAYS_INLINE void SetFilterValueUnchecked(uint32_t index,
                                                      int64_t value) {
    SetFilterValueInternal(index, value);
  }
  PERFETTO_ALWAYS_INLINE void SetFilterValueUnchecked(uint32_t index,
                                                      double value) {
    SetFilterValueInternal(index, value);
  }
  PERFETTO_ALWAYS_INLINE void SetFilterValueUnchecked(uint32_t index,
                                                      const char* value) {
    SetFilterValueInternal(index, value);
  }

  // Executes the current query plan against the specified filter values and
  // populates the cursor with the results.
  //
  // See `SetFilterValueUnchecked` for details on how to set the filter
  // values.
  PERFETTO_ALWAYS_INLINE void ExecuteUnchecked();

  // Returns the current row index.
  PERFETTO_ALWAYS_INLINE uint32_t RowIndex() const {
    return cursor_.RowIndex();
  }

  // Advances the cursor to the next row of results.
  PERFETTO_ALWAYS_INLINE void Next() { cursor_.Next(); }

  // Returns true if the cursor has reached the end of the result set.
  PERFETTO_ALWAYS_INLINE bool Eof() const { return cursor_.Eof(); }

  // Resets the cursor to the initial state. This frees any resources the
  // cursor may have allocated and prepares it for a new query execution.
  void Reset() { PrepareCursorInternal(); }

  // Calls `Dataframe:GetCellUnchecked` for the current row and specified
  // column.
  template <size_t C, typename D>
  PERFETTO_ALWAYS_INLINE auto GetCellUnchecked(const D&) const {
    return dataframe_->GetCellUncheckedInternal<C, D>(cursor_.RowIndex());
  }

  // Calls `Dataframe:SetCellUnchecked` for the current row, specified column
  // and the given `value`.
  template <size_t C, typename D>
  PERFETTO_ALWAYS_INLINE void SetCellUnchecked(
      const D&,
      const typename D::template column_spec<C>::mutate_type& value) {
    PERFETTO_DCHECK(mutable_);
    const_cast<Dataframe*>(dataframe_)
        ->SetCellUncheckedInternal<C, D>(cursor_.RowIndex(), value);
  }

 private:
  TypedCursor(const Dataframe* dataframe,
              std::vector<FilterSpec> filter_specs,
              std::vector<SortSpec> sort_specs,
              bool mut)
      : dataframe_(dataframe),
        filter_specs_(std::move(filter_specs)),
        sort_specs_(std::move(sort_specs)),
        mutable_(mut),
        column_mutation_count_(impl::Slab<uint32_t*>::Alloc(
            filter_specs_.size() + sort_specs_.size())) {
    filter_values_.resize(filter_specs_.size());
    filter_value_mapping_.resize(filter_specs_.size(),
                                 std::numeric_limits<uint32_t>::max());
    uint32_t i = 0;
    for (const auto& spec : filter_specs_) {
      column_mutation_count_[i++] =
          &dataframe->column_ptrs_[spec.col]->mutations;
    }
    for (const auto& spec : sort_specs_) {
      column_mutation_count_[i++] =
          &dataframe->column_ptrs_[spec.col]->mutations;
    }
  }
  template <typename C>
  PERFETTO_ALWAYS_INLINE void SetFilterValueInternal(uint32_t index, C value) {
    if (PERFETTO_UNLIKELY(last_execution_mutation_count_ != GetMutations())) {
      PrepareCursorInternal();
    }
    uint32_t mapped = filter_value_mapping_[index];
    if (mapped != std::numeric_limits<uint32_t>::max()) {
      filter_values_[mapped] = value;
    }
  }

  void PrepareCursorInternal();

  uint32_t GetMutations() const {
    uint32_t mutations = dataframe_->non_column_mutations_;
    for (uint32_t* m : column_mutation_count_) {
      mutations += *m;
    }
    return mutations;
  }

  const Dataframe* dataframe_;
  std::vector<FilterValue> filter_values_;
  std::vector<uint32_t> filter_value_mapping_;
  std::vector<FilterSpec> filter_specs_;
  std::vector<SortSpec> sort_specs_;
  bool mutable_;
  Cursor<Fetcher> cursor_;

  impl::Slab<uint32_t*> column_mutation_count_;
  uint32_t last_execution_mutation_count_ =
      std::numeric_limits<uint32_t>::max();
};

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_TYPED_CURSOR_H_
