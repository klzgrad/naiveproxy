/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/db/runtime_table.h"

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
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/db/column.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/id_storage.h"
#include "src/trace_processor/db/column/null_overlay.h"
#include "src/trace_processor/db/column/numeric_storage.h"
#include "src/trace_processor/db/column/overlay_layer.h"
#include "src/trace_processor/db/column/selector_overlay.h"
#include "src/trace_processor/db/column/storage_layer.h"
#include "src/trace_processor/db/column/string_storage.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/column_storage.h"
#include "src/trace_processor/db/column_storage_overlay.h"

namespace perfetto::trace_processor {
namespace {

template <typename T, typename U>
T Fill(uint32_t leading_nulls, U value) {
  T res;
  for (uint32_t i = 0; i < leading_nulls; ++i) {
    res.Append(value);
  }
  return res;
}

bool IsPerfectlyRepresentableAsDouble(int64_t res) {
  static constexpr int64_t kMaxDoubleRepresentible = 1ull << 53;
  return res >= -kMaxDoubleRepresentible && res <= kMaxDoubleRepresentible;
}

void CreateNonNullableIntsColumn(
    uint32_t col_idx,
    const char* col_name,
    ColumnStorage<int64_t>* ints_storage,
    std::vector<RefPtr<column::StorageLayer>>& storage_layers,
    std::vector<RefPtr<column::OverlayLayer>>& overlay_layers,
    std::vector<ColumnLegacy>& legacy_columns,
    std::vector<ColumnStorageOverlay>& legacy_overlays) {
  const std::vector<int64_t>& values = ints_storage->vector();

  // Looking for the iterator to the first value that is less or equal to the
  // previous value. The values before are therefore strictly monotonic - each
  // is greater than the previous one.
  bool is_monotonic = true;
  bool is_sorted = true;
  for (uint32_t i = 1; i < values.size() && is_sorted; i++) {
    is_monotonic = is_monotonic && values[i - 1] < values[i];
    is_sorted = values[i - 1] <= values[i];
  }

  // The special treatment for Id columns makes no sense for empty or
  // single element indices. Those should be treated as standard int
  // column.

  // We expect id column to:
  // - be strictly monotonic.
  bool is_id = is_monotonic;
  // - have more than 1 element.
  is_id = is_id && values.size() > 1;
  // - have first elements smaller then 2^20, mostly to prevent timestamps
  // columns from becoming Id columns.
  is_id = is_id && values.front() < 1 << 20;
  // - have `uint32_t` values.
  is_id = is_id && values.front() >= std::numeric_limits<uint32_t>::min() &&
          values.back() < std::numeric_limits<uint32_t>::max();
  // - have on average more than 1 set bit per int64_t (over 1/64 density)
  is_id = is_id && static_cast<uint32_t>(values.back()) < 64 * values.size();

  if (is_id) {
    // The column is an Id column.
    storage_layers[col_idx].reset(new column::IdStorage());

    // If the id is dense (i.e. the start is zero and the size equals the last
    // value) then there's no need for an overlay.
    bool is_dense = values.front() == 0 &&
                    static_cast<uint32_t>(values.back()) == values.size() - 1;
    if (is_dense) {
      legacy_columns.push_back(
          ColumnLegacy::IdColumn(col_idx, 0, col_name, ColumnLegacy::kIdFlags));
    } else {
      legacy_overlays.emplace_back(BitVector::FromSortedIndexVector(values));
      overlay_layers.emplace_back().reset(new column::SelectorOverlay(
          legacy_overlays.back().row_map().GetIfBitVector()));
      legacy_columns.push_back(ColumnLegacy::IdColumn(
          col_idx, static_cast<uint32_t>(legacy_overlays.size() - 1), col_name,
          ColumnLegacy::kIdFlags));
    }
    return;
  }

  uint32_t flags =
      is_sorted ? ColumnLegacy::Flag::kNonNull | ColumnLegacy::Flag::kSorted
                : ColumnLegacy::Flag::kNonNull;

  legacy_columns.emplace_back(col_name, ints_storage, flags, col_idx, 0);
  storage_layers[col_idx].reset(new column::NumericStorage<int64_t>(
      &values, ColumnType::kInt64, is_sorted));
}

}  // namespace

RuntimeTable::RuntimeTable(
    StringPool* pool,
    uint32_t row_count,
    std::vector<ColumnLegacy> columns,
    std::vector<ColumnStorageOverlay> overlays,
    std::vector<RefPtr<column::StorageLayer>> storage_layers,
    std::vector<RefPtr<column::OverlayLayer>> null_layers,
    std::vector<RefPtr<column::OverlayLayer>> overlay_layers)
    : Table(pool, row_count, std::move(columns), std::move(overlays)) {
  OnConstructionCompleted(std::move(storage_layers), std::move(null_layers),
                          std::move(overlay_layers));
}

RuntimeTable::~RuntimeTable() = default;

RuntimeTable::Builder::Builder(StringPool* pool,
                               const std::vector<std::string>& col_names)
    : Builder(pool,
              col_names,
              std::vector<BuilderColumnType>(col_names.size(), kNull)) {}

RuntimeTable::Builder::Builder(StringPool* pool,
                               const std::vector<std::string>& col_names,
                               const std::vector<BuilderColumnType>& col_types)
    : string_pool_(pool), col_names_(col_names) {
  for (BuilderColumnType type : col_types) {
    switch (type) {
      case kNull:
        storage_.emplace_back(std::make_unique<VariantStorage>());
        break;
      case kInt:
        storage_.emplace_back(std::make_unique<VariantStorage>(IntStorage()));
        break;
      case kNullInt:
        storage_.emplace_back(
            std::make_unique<VariantStorage>(NullIntStorage()));
        break;
      case kDouble:
        storage_.emplace_back(
            std::make_unique<VariantStorage>(DoubleStorage()));
        break;
      case kNullDouble:
        storage_.emplace_back(
            std::make_unique<VariantStorage>(NullDoubleStorage()));
        break;
      case kString:
        storage_.emplace_back(
            std::make_unique<VariantStorage>(StringStorage()));
        break;
    }
  }
}

base::Status RuntimeTable::Builder::AddNull(uint32_t idx) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls = std::get_if<uint32_t>(col)) {
    (*leading_nulls)++;
  } else if (auto* ints = std::get_if<NullIntStorage>(col)) {
    ints->Append(std::nullopt);
  } else if (auto* strings = std::get_if<StringStorage>(col)) {
    strings->Append(StringPool::Id::Null());
  } else if (auto* doubles = std::get_if<NullDoubleStorage>(col)) {
    doubles->Append(std::nullopt);
  } else {
    PERFETTO_FATAL("Unexpected column type");
  }
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddInteger(uint32_t idx, int64_t res) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<NullIntStorage>(*leading_nulls_ptr, std::nullopt);
  }
  if (auto* doubles = std::get_if<NullDoubleStorage>(col)) {
    if (!IsPerfectlyRepresentableAsDouble(res)) {
      return base::ErrStatus("Column %s contains %" PRId64
                             " which cannot be represented as a double",
                             col_names_[idx].c_str(), res);
    }
    doubles->Append(static_cast<double>(res));
    return base::OkStatus();
  }
  auto* ints = std::get_if<NullIntStorage>(col);
  if (!ints) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  ints->Append(res);
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddFloat(uint32_t idx, double res) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<NullDoubleStorage>(*leading_nulls_ptr, std::nullopt);
  }
  if (auto* ints = std::get_if<NullIntStorage>(col)) {
    NullDoubleStorage storage;
    for (uint32_t i = 0; i < ints->size(); ++i) {
      std::optional<int64_t> int_val = ints->Get(i);
      if (!int_val) {
        storage.Append(std::nullopt);
        continue;
      }
      if (int_val && !IsPerfectlyRepresentableAsDouble(*int_val)) {
        return base::ErrStatus("Column %s contains %" PRId64
                               " which cannot be represented as a double",
                               col_names_[idx].c_str(), *int_val);
      }
      storage.Append(static_cast<double>(*int_val));
    }
    *col = std::move(storage);
  }
  auto* doubles = std::get_if<NullDoubleStorage>(col);
  if (!doubles) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  doubles->Append(res);
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddText(uint32_t idx, const char* ptr) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<StringStorage>(*leading_nulls_ptr, StringPool::Id::Null());
  }
  auto* strings = std::get_if<StringStorage>(col);
  if (!strings) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  strings->Append(string_pool_->InternString(ptr));
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddIntegers(uint32_t idx,
                                                int64_t val,
                                                uint32_t count) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<NullIntStorage>(*leading_nulls_ptr, std::nullopt);
  }
  if (auto* doubles = std::get_if<NullDoubleStorage>(col)) {
    if (!IsPerfectlyRepresentableAsDouble(val)) {
      return base::ErrStatus("Column %s contains %" PRId64
                             " which cannot be represented as a double",
                             col_names_[idx].c_str(), val);
    }
    doubles->AppendMultiple(static_cast<double>(val), count);
    return base::OkStatus();
  }
  if (auto* null_ints = std::get_if<NullIntStorage>(col)) {
    null_ints->AppendMultiple(val, count);
    return base::OkStatus();
  }
  auto* ints = std::get_if<IntStorage>(col);
  if (!ints) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  ints->AppendMultiple(val, count);
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddFloats(uint32_t idx,
                                              double res,
                                              uint32_t count) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<NullDoubleStorage>(*leading_nulls_ptr, std::nullopt);
  }
  if (auto* ints = std::get_if<NullIntStorage>(col)) {
    NullDoubleStorage storage;
    for (uint32_t i = 0; i < ints->size(); ++i) {
      std::optional<int64_t> int_val = ints->Get(i);
      if (!int_val) {
        storage.AppendMultipleNulls(count);
        continue;
      }
      if (int_val && !IsPerfectlyRepresentableAsDouble(*int_val)) {
        return base::ErrStatus("Column %s contains %" PRId64
                               " which cannot be represented as a double",
                               col_names_[idx].c_str(), *int_val);
      }
      storage.AppendMultiple(static_cast<double>(*int_val), count);
    }
    *col = std::move(storage);
  }
  auto* doubles = std::get_if<NullDoubleStorage>(col);
  if (!doubles) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  doubles->AppendMultiple(res, count);
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddTexts(uint32_t idx,
                                             const char* ptr,
                                             uint32_t count) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls_ptr = std::get_if<uint32_t>(col)) {
    *col = Fill<StringStorage>(*leading_nulls_ptr, StringPool::Id::Null());
  }
  auto* strings = std::get_if<StringStorage>(col);
  if (!strings) {
    return base::ErrStatus("Column %s does not have consistent types",
                           col_names_[idx].c_str());
  }
  strings->AppendMultiple(string_pool_->InternString(ptr), count);
  return base::OkStatus();
}

base::Status RuntimeTable::Builder::AddNulls(uint32_t idx, uint32_t count) {
  auto* col = storage_[idx].get();
  if (auto* leading_nulls = std::get_if<uint32_t>(col)) {
    (*leading_nulls)++;
  } else if (auto* ints = std::get_if<NullIntStorage>(col)) {
    ints->AppendMultipleNulls(count);
  } else if (auto* strings = std::get_if<StringStorage>(col)) {
    strings->AppendMultiple(StringPool::Id::Null(), count);
  } else if (auto* doubles = std::get_if<NullDoubleStorage>(col)) {
    doubles->AppendMultipleNulls(count);
  } else {
    PERFETTO_FATAL("Unexpected column type");
  }
  return base::OkStatus();
}

void RuntimeTable::Builder::AddNonNullIntegersUnchecked(
    uint32_t idx,
    const std::vector<int64_t>& res) {
  std::get<IntStorage>(*storage_[idx]).Append(res);
}

void RuntimeTable::Builder::AddNullIntegersUnchecked(
    uint32_t idx,
    const std::vector<int64_t>& res) {
  std::get<NullIntStorage>(*storage_[idx]).Append(res);
}

void RuntimeTable::Builder::AddNonNullDoublesUnchecked(
    uint32_t idx,
    const std::vector<double>& vals) {
  std::get<DoubleStorage>(*storage_[idx]).Append(vals);
}

void RuntimeTable::Builder::AddNullDoublesUnchecked(
    uint32_t idx,
    const std::vector<double>& vals) {
  std::get<NullDoubleStorage>(*storage_[idx]).Append(vals);
}

base::StatusOr<std::unique_ptr<RuntimeTable>> RuntimeTable::Builder::Build(
    uint32_t rows) && {
  std::vector<RefPtr<column::StorageLayer>> storage_layers(col_names_.size() +
                                                           1);
  std::vector<RefPtr<column::OverlayLayer>> null_layers(col_names_.size() + 1);

  std::vector<ColumnLegacy> legacy_columns;
  std::vector<ColumnStorageOverlay> legacy_overlays;

  // |overlay_layers| might use the RowMaps used by |legacy_overlays| and access
  // them by fetching the pointer to the RowMap inside overlay. We need to make
  // sure that those pointers will not change, hence we need to make sure that
  // the vector will not resize. In the current implementation there is at most
  // one overlay per column.
  legacy_overlays.reserve(col_names_.size() + 1);
  legacy_overlays.emplace_back(rows);
  std::vector<RefPtr<column::OverlayLayer>> overlay_layers(1);

  for (uint32_t i = 0; i < col_names_.size(); ++i) {
    auto* col = storage_[i].get();
    std::unique_ptr<column::DataLayerChain> chain;
    if (auto* leading_nulls = std::get_if<uint32_t>(col)) {
      PERFETTO_CHECK(*leading_nulls == rows);
      *col = Fill<NullIntStorage>(*leading_nulls, std::nullopt);
    }

    if (auto* null_ints = std::get_if<NullIntStorage>(col)) {
      // The `ints` column
      PERFETTO_CHECK(null_ints->size() == rows);

      if (null_ints->non_null_size() == null_ints->size()) {
        // The column doesn't have any nulls so we construct a new nonnullable
        // column.
        *col = IntStorage::CreateFromAssertNonNull(std::move(*null_ints));
        CreateNonNullableIntsColumn(
            i, col_names_[i].c_str(), std::get_if<IntStorage>(col),
            storage_layers, overlay_layers, legacy_columns, legacy_overlays);
      } else {
        // Nullable ints column.
        legacy_columns.emplace_back(col_names_[i].c_str(), null_ints,
                                    ColumnLegacy::Flag::kNoFlag, i, 0);
        storage_layers[i].reset(new column::NumericStorage<int64_t>(
            &null_ints->non_null_vector(), ColumnType::kInt64, false));
        null_layers[i].reset(
            new column::NullOverlay(&null_ints->non_null_bit_vector()));
      }

    } else if (auto* ints = std::get_if<IntStorage>(col)) {
      // The `ints` column for tables where column types was provided before.
      PERFETTO_CHECK(ints->size() == rows);
      CreateNonNullableIntsColumn(
          i, col_names_[i].c_str(), std::get_if<IntStorage>(col),
          storage_layers, overlay_layers, legacy_columns, legacy_overlays);

    } else if (auto* doubles = std::get_if<DoubleStorage>(col)) {
      // The `doubles` column for tables where column types was provided before.
      PERFETTO_CHECK(doubles->size() == rows);
      bool is_sorted =
          std::is_sorted(doubles->vector().begin(), doubles->vector().end());
      uint32_t flags =
          is_sorted ? ColumnLegacy::Flag::kNonNull | ColumnLegacy::Flag::kSorted
                    : ColumnLegacy::Flag::kNonNull;
      legacy_columns.emplace_back(col_names_[i].c_str(), doubles, flags, i, 0);
      storage_layers[i].reset(new column::NumericStorage<double>(
          &doubles->vector(), ColumnType::kDouble, is_sorted));

    } else if (auto* null_doubles = std::get_if<NullDoubleStorage>(col)) {
      // The doubles column.
      PERFETTO_CHECK(null_doubles->size() == rows);
      if (null_doubles->non_null_size() == null_doubles->size()) {
        // The column is not nullable.
        *col = DoubleStorage::CreateFromAssertNonNull(std::move(*null_doubles));

        auto* non_null_doubles = std::get_if<DoubleStorage>(col);
        bool is_sorted = std::is_sorted(non_null_doubles->vector().begin(),
                                        non_null_doubles->vector().end());
        uint32_t flags = is_sorted ? ColumnLegacy::Flag::kNonNull |
                                         ColumnLegacy::Flag::kSorted
                                   : ColumnLegacy::Flag::kNonNull;
        legacy_columns.emplace_back(col_names_[i].c_str(), non_null_doubles,
                                    flags, i, 0);
        storage_layers[i].reset(new column::NumericStorage<double>(
            &non_null_doubles->vector(), ColumnType::kDouble, is_sorted));
      } else {
        // The column is nullable.
        legacy_columns.emplace_back(col_names_[i].c_str(), null_doubles,
                                    ColumnLegacy::Flag::kNoFlag, i, 0);
        storage_layers[i].reset(new column::NumericStorage<double>(
            &null_doubles->non_null_vector(), ColumnType::kDouble, false));
        null_layers[i].reset(
            new column::NullOverlay(&null_doubles->non_null_bit_vector()));
      }

    } else if (auto* strings = std::get_if<StringStorage>(col)) {
      // The `strings` column.
      PERFETTO_CHECK(strings->size() == rows);
      legacy_columns.emplace_back(col_names_[i].c_str(), strings,
                                  ColumnLegacy::Flag::kNonNull, i, 0);
      storage_layers[i].reset(
          new column::StringStorage(string_pool_, &strings->vector()));
    } else {
      PERFETTO_FATAL("Unexpected column type");
    }
  }

  legacy_columns.push_back(ColumnLegacy::IdColumn(
      static_cast<uint32_t>(legacy_columns.size()), 0, "_auto_id",
      ColumnLegacy::kIdFlags | ColumnLegacy::Flag::kHidden));
  storage_layers.back().reset(new column::IdStorage());

  auto table = std::make_unique<RuntimeTable>(
      string_pool_, rows, std::move(legacy_columns), std::move(legacy_overlays),
      std::move(storage_layers), std::move(null_layers),
      std::move(overlay_layers));
  table->storage_ = std::move(storage_);
  table->col_names_ = std::move(col_names_);

  table->schema_.columns.reserve(table->columns().size());
  for (size_t i = 0; i < table->columns().size(); ++i) {
    const auto& col = table->columns()[i];
    SqlValue::Type column_type =
        col.col_type() != ColumnType::kId &&
                col.storage_base().non_null_size() == 0
            ? SqlValue::kNull
            : ColumnLegacy::ToSqlValueType(col.col_type());
    table->schema_.columns.emplace_back(
        Schema::Column{col.name(), column_type, col.IsId(), col.IsSorted(),
                       col.IsHidden(), col.IsSetId()});
  }
  return {std::move(table)};
}

}  // namespace perfetto::trace_processor
