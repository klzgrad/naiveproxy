/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/tables/macros_internal.h"

#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/containers/row_map.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/db/column.h"
#include "src/trace_processor/db/column/overlay_layer.h"
#include "src/trace_processor/db/column/selector_overlay.h"
#include "src/trace_processor/db/column/storage_layer.h"
#include "src/trace_processor/db/column_storage_overlay.h"

namespace perfetto::trace_processor::macros_internal {

PERFETTO_NO_INLINE MacroTable::MacroTable(StringPool* pool,
                                          std::vector<ColumnLegacy> columns,
                                          const MacroTable* parent)
    : Table(pool, 0u, std::move(columns), EmptyOverlaysFromParent(parent)),
      allow_inserts_(true),
      parent_(parent) {}

PERFETTO_NO_INLINE MacroTable::MacroTable(StringPool* pool,
                                          std::vector<ColumnLegacy> columns,
                                          const MacroTable& parent,
                                          const RowMap& parent_overlay)
    : Table(pool,
            parent_overlay.size(),
            std::move(columns),
            SelectedOverlaysFromParent(parent, parent_overlay)),
      allow_inserts_(false),
      parent_(&parent) {}

PERFETTO_NO_INLINE void MacroTable::UpdateOverlaysAfterParentInsert() {
  CopyLastInsertFrom(parent_->overlays());
}

PERFETTO_NO_INLINE void MacroTable::UpdateSelfOverlayAfterInsert() {
  IncrementRowCountAndAddToLastOverlay();
}

PERFETTO_NO_INLINE std::vector<ColumnLegacy>
MacroTable::CopyColumnsFromParentOrAddRootColumns(const MacroTable* parent) {
  std::vector<ColumnLegacy> columns;
  if (parent) {
    for (const ColumnLegacy& col : parent->columns()) {
      columns.emplace_back(col, col.index_in_table(), col.overlay_index());
    }
  } else {
    columns.emplace_back(ColumnLegacy::IdColumn(0, 0));
  }
  return columns;
}

PERFETTO_NO_INLINE void MacroTable::OnConstructionCompletedRegularConstructor(
    std::initializer_list<RefPtr<column::StorageLayer>> storage_layers,
    std::initializer_list<RefPtr<column::OverlayLayer>> null_layers) {
  std::vector<RefPtr<column::OverlayLayer>> overlay_layers(
      OverlayCount(parent_) + 1);
  for (uint32_t i = 0; i < overlay_layers.size() - 1; ++i) {
    PERFETTO_CHECK(overlays()[i].row_map().IsBitVector());
    overlay_layers[i].reset(
        new column::SelectorOverlay(overlays()[i].row_map().GetIfBitVector()));
  }
  Table::OnConstructionCompleted(storage_layers, null_layers,
                                 std::move(overlay_layers));
}

PERFETTO_NO_INLINE std::vector<ColumnStorageOverlay>
MacroTable::EmptyOverlaysFromParent(const MacroTable* parent) {
  std::vector<ColumnStorageOverlay> overlays(parent ? parent->overlays().size()
                                                    : 0);
  for (auto& overlay : overlays) {
    overlay = ColumnStorageOverlay(BitVector());
  }
  overlays.emplace_back();
  return overlays;
}

PERFETTO_NO_INLINE std::vector<ColumnStorageOverlay>
MacroTable::SelectedOverlaysFromParent(
    const macros_internal::MacroTable& parent,
    const RowMap& rm) {
  std::vector<ColumnStorageOverlay> overlays;
  for (const auto& overlay : parent.overlays()) {
    overlays.emplace_back(overlay.SelectRows(rm));
    PERFETTO_DCHECK(overlays.back().size() == rm.size());
  }
  overlays.emplace_back(rm.size());
  return overlays;
}

BaseConstIterator::BaseConstIterator(const MacroTable* table,
                                     Table::Iterator iterator)
    : iterator_(std::move(iterator)), table_(table) {
  static_assert(std::is_base_of<Table, MacroTable>::value,
                "Template param should be a subclass of Table.");
}

BaseConstIterator::operator bool() const {
  return bool(iterator_);
}

BaseConstIterator& BaseConstIterator::operator++() {
  ++iterator_;
  return *this;
}

BaseRowReference::BaseRowReference(const MacroTable* table, uint32_t row_number)
    : table_(table), row_number_(row_number) {}

}  // namespace perfetto::trace_processor::macros_internal
