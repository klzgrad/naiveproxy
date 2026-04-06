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

#include "src/trace_processor/importers/proto/winscope/winscope_transform_tracker.h"

#include <algorithm>
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor::winscope {

const tables::WinscopeTransformTable::Id&
WinscopeTransformTracker::GetOrInsertRow(geometry::TransformMatrix& matrix) {
  auto* existing_row_id = rows_.Find(matrix);
  if (existing_row_id) {
    return *existing_row_id;
  }

  tables::WinscopeTransformTable::Row row;
  row.dsdx = matrix.dsdx;
  row.dtdx = matrix.dtdx;
  row.dsdy = matrix.dsdy;
  row.dtdy = matrix.dtdy;
  row.tx = matrix.tx;
  row.ty = matrix.ty;
  auto id =
      context_->storage->mutable_winscope_transform_table()->Insert(row).id;

  return *rows_.Insert(matrix, id).first;
}

}  // namespace perfetto::trace_processor::winscope
