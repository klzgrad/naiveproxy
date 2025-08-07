/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "src/trace_processor/db/column.h"

#include <cstdint>
#include <limits>

#include "column/types.h"
#include "column_storage.h"
#include "column_storage_overlay.h"
#include "perfetto/base/logging.h"
#include "src/trace_processor/db/table.h"

namespace perfetto::trace_processor {

ColumnLegacy::ColumnLegacy(const ColumnLegacy& column,
                           uint32_t col_idx,
                           uint32_t overlay_idx,
                           const char* name)
    : ColumnLegacy(name ? name : column.name_,
                   column.type_,
                   column.flags_ & ~kNoCrossTableInheritFlags,
                   col_idx,
                   overlay_idx,
                   column.storage_) {}

ColumnLegacy::ColumnLegacy(const char* name,
                           ColumnType type,
                           uint32_t flags,
                           uint32_t index_in_table,
                           uint32_t overlay_index,
                           ColumnStorageBase* st)
    : type_(type),
      storage_(st),
      name_(name),
      flags_(flags),
      index_in_table_(index_in_table),
      overlay_index_(overlay_index) {}

ColumnLegacy ColumnLegacy::DummyColumn(const char* name,
                                       uint32_t col_idx_in_table) {
  return {name,
          ColumnType::kDummy,
          Flag::kNoFlag,
          col_idx_in_table,
          std::numeric_limits<uint32_t>::max(),
          nullptr};
}

ColumnLegacy ColumnLegacy::IdColumn(uint32_t col_idx,
                                    uint32_t overlay_idx,
                                    const char* name,
                                    uint32_t flags) {
  return {name, ColumnType::kId, flags, col_idx, overlay_idx, nullptr};
}

const ColumnStorageOverlay& ColumnLegacy::overlay() const {
  PERFETTO_DCHECK(type_ != ColumnType::kDummy);
  return table_->overlays_[overlay_index()];
}

}  // namespace perfetto::trace_processor
