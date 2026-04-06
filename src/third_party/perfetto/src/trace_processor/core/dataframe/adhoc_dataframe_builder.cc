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

#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/flex_vector.h"

namespace perfetto::trace_processor::core::dataframe {

AdhocDataframeBuilder::AdhocDataframeBuilder(std::vector<std::string> names,
                                             StringPool* pool,
                                             const Options& options)
    : string_pool_(pool), did_declare_types_(!options.types.empty()) {
  PERFETTO_DCHECK(options.types.empty() ||
                  options.types.size() == names.size());
  for (uint32_t i = 0; i < names.size(); ++i) {
    ColumnState state;
    state.nullability_type = options.nullability_type;
    if (!options.types.empty()) {
      switch (options.types[i]) {
        case ColumnType::kInt64:
          state.storage = Storage{core::FlexVector<int64_t>()};
          break;
        case ColumnType::kDouble:
          state.storage = Storage{core::FlexVector<double>()};
          break;
        case ColumnType::kString:
          state.storage = Storage{core::FlexVector<StringPool::Id>()};
          break;
      }
    }
    column_states_.emplace_back(std::move(state));
  }
  for (auto& name : names) {
    column_names_.emplace_back(std::move(name));
  }
}

void AdhocDataframeBuilder::AddPlaceholderValue(uint32_t col, uint32_t count) {
  auto& state = column_states_[col];
  if (!state.storage) {
    // Column type hasn't been determined yet - nothing to push.
    return;
  }
  if (state.storage->type().Is<Int64>()) {
    state.storage->unchecked_get<Int64>().push_back_multiple(0, count);
  } else if (state.storage->type().Is<Double>()) {
    state.storage->unchecked_get<Double>().push_back_multiple(0.0, count);
  } else if (state.storage->type().Is<String>()) {
    state.storage->unchecked_get<String>().push_back_multiple(
        StringPool::Id::Null(), count);
  }
}

base::StatusOr<Dataframe> AdhocDataframeBuilder::Build() && {
  uint64_t row_count = std::numeric_limits<uint64_t>::max();
  RETURN_IF_ERROR(current_status_);
  std::vector<std::shared_ptr<Column>> columns;
  for (uint32_t i = 0; i < column_names_.size(); ++i) {
    auto& state = column_states_[i];
    size_t non_null_row_count;
    if (!state.storage) {
      non_null_row_count = 0;
      columns.emplace_back(std::make_shared<Column>(Column{
          Storage{core::FlexVector<uint32_t>()},
          CreateNullStorageFromBitvector(std::move(state.null_overlay),
                                         state.nullability_type),
          Unsorted{},
          HasDuplicates{},
      }));
    } else if (state.storage->type().Is<Int64>()) {
      auto& data = state.storage->unchecked_get<Int64>();
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
        summary.is_setid_sorted =
            summary.is_setid_sorted && (data[j] == data[j - 1] || data[j] == j);
        summary.is_sorted = summary.is_sorted && data[j - 1] <= data[j];
        summary.min = std::min(summary.min, data[j]);
        summary.max = std::max(summary.max, data[j]);
        summary.has_duplicates =
            summary.has_duplicates || CheckDuplicate(data[j], data.size());
      }
      auto integer = CreateIntegerStorage(std::move(data), summary);
      SpecializedStorage specialized_storage =
          GetSpecializedStorage(integer, summary);
      columns.emplace_back(std::make_shared<Column>(Column{
          std::move(integer),
          CreateNullStorageFromBitvector(std::move(state.null_overlay),
                                         state.nullability_type),
          GetIntegerSortStateFromProperties(summary),
          summary.is_nullable || summary.has_duplicates
              ? DuplicateState{HasDuplicates{}}
              : DuplicateState{NoDuplicates{}},
          std::move(specialized_storage),
      }));
    } else if (state.storage->type().Is<Double>()) {
      auto& data = state.storage->unchecked_get<Double>();
      non_null_row_count = data.size();

      bool is_nullable = state.null_overlay.has_value();
      bool is_sorted = true;
      for (uint32_t j = 1; j < data.size(); ++j) {
        is_sorted = is_sorted && data[j - 1] <= data[j];
      }
      columns.emplace_back(std::make_shared<Column>(Column{
          Storage{std::move(data)},
          CreateNullStorageFromBitvector(std::move(state.null_overlay),
                                         state.nullability_type),
          is_sorted && !is_nullable ? SortState{Sorted{}}
                                    : SortState{Unsorted{}},
          HasDuplicates{},
      }));
    } else if (state.storage->type().Is<String>()) {
      auto& data = state.storage->unchecked_get<String>();
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
      columns.emplace_back(std::make_shared<Column>(Column{
          Storage{std::move(data)},
          CreateNullStorageFromBitvector(std::move(state.null_overlay),
                                         state.nullability_type),
          is_sorted && !is_nullable ? SortState{Sorted{}}
                                    : SortState{Unsorted{}},
          HasDuplicates{},
      }));
    } else {
      PERFETTO_FATAL("Unexpected storage type in column %u", i);
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
  columns.emplace_back(std::make_shared<Column>(
      Column{Storage{Storage::Id{static_cast<uint32_t>(row_count)}},
             NullStorage::NonNull{}, IdSorted{}, NoDuplicates{}}));
  return Dataframe(true, std::move(column_names_), std::move(columns),
                   static_cast<uint32_t>(row_count), string_pool_);
}

Storage AdhocDataframeBuilder::CreateIntegerStorage(
    core::FlexVector<int64_t> data,
    const IntegerColumnSummary& summary) {
  // TODO(lalitm): `!summary.is_nullable` is an unnecesarily strong condition
  // but we impose it as query planning assumes that id columns never have an
  // index added to them.
  if (summary.is_id_sorted && !summary.is_nullable) {
    return Storage{Storage::Id{static_cast<uint32_t>(data.size())}};
  }
  if (IsRangeFullyRepresentableByType<uint32_t>(summary.min, summary.max)) {
    return Storage{Storage::Uint32{DowncastFromInt64<uint32_t>(data)}};
  }
  if (IsRangeFullyRepresentableByType<int32_t>(summary.min, summary.max)) {
    return Storage{Storage::Int32{DowncastFromInt64<int32_t>(data)}};
  }
  return Storage{Storage::Int64{std::move(data)}};
}

NullStorage AdhocDataframeBuilder::CreateNullStorageFromBitvector(
    std::optional<core::BitVector> bit_vector,
    NullabilityType nullability_type) {
  if (bit_vector) {
    if (nullability_type == NullabilityType::kDenseNull) {
      return NullStorage{NullStorage::DenseNull{*std::move(bit_vector)}};
    }
    if (nullability_type == NullabilityType::kSparseNullWithPopcount) {
      // Compute prefix popcount for sparse null to enable GetCell support.
      auto prefix_popcount = bit_vector->PrefixPopcountFlexVector();
      return NullStorage{
          NullStorage::SparseNull{
              *std::move(bit_vector),
              std::move(prefix_popcount),
          },
          SparseNullWithPopcountAlways{},
      };
    }
    return NullStorage{NullStorage::SparseNull{
        *std::move(bit_vector),
        {},
    }};
  }
  return NullStorage{NullStorage::NonNull{}};
}

SortState AdhocDataframeBuilder::GetIntegerSortStateFromProperties(
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

SpecializedStorage AdhocDataframeBuilder::GetSpecializedStorage(
    const Storage& storage,
    const IntegerColumnSummary& summary) {
  // If we're already sorted or setid_sorted, we don't need specialized
  // storage.
  if (summary.is_id_sorted || summary.is_setid_sorted) {
    return SpecializedStorage{};
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
  return SpecializedStorage{};
}

SpecializedStorage::SmallValueEq AdhocDataframeBuilder::BuildSmallValueEq(
    const core::FlexVector<uint32_t>& data) {
  SpecializedStorage::SmallValueEq offset_bv{
      core::BitVector::CreateWithSize(data.empty() ? 0 : data.back() + 1,
                                      false),
      {},
  };
  for (uint32_t i : data) {
    offset_bv.bit_vector.set(i);
  }
  offset_bv.prefix_popcount = offset_bv.bit_vector.PrefixPopcount();
  return offset_bv;
}

void AdhocDataframeBuilder::EnsureNullOverlayExists(ColumnState& state) {
  uint64_t row_count = 0;
  if (state.storage) {
    if (state.storage->type().Is<Int64>()) {
      row_count = state.storage->unchecked_get<Int64>().size();
    } else if (state.storage->type().Is<Double>()) {
      row_count = state.storage->unchecked_get<Double>().size();
    } else if (state.storage->type().Is<String>()) {
      row_count = state.storage->unchecked_get<String>().size();
    }
  }
  state.null_overlay =
      core::BitVector::CreateWithSize(static_cast<uint32_t>(row_count), true);
}

const char* AdhocDataframeBuilder::ToString(
    const std::optional<Storage>& storage) {
  if (!storage) {
    return "NULL";
  }
  if (storage->type().Is<Int64>()) {
    return "LONG";
  }
  if (storage->type().Is<Double>()) {
    return "DOUBLE";
  }
  if (storage->type().Is<String>()) {
    return "STRING";
  }
  PERFETTO_FATAL("Unexpected storage type.");
}

}  // namespace perfetto::trace_processor::core::dataframe
