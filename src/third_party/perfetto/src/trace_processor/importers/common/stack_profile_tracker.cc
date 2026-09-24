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

#include "src/trace_processor/importers/common/stack_profile_tracker.h"

#include <cstdint>
#include <optional>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

CallsiteId StackProfileTracker::InternCallsite(
    std::optional<CallsiteId> parent_callsite_id,
    FrameId frame_id,
    uint32_t depth) {
  tables::StackProfileCallsiteTable::Row row{
      depth,
      parent_callsite_id,
      frame_id,
  };
  auto [id, inserted] = callsite_unique_row_index_.Insert(row, {});
  if (!inserted) {
    return *id;
  }
  *id =
      context_->storage->mutable_stack_profile_callsite_table()->Insert(row).id;
  return *id;
}

}  // namespace perfetto::trace_processor
