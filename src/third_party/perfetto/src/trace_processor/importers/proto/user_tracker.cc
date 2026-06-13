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

#include "src/trace_processor/importers/proto/user_tracker.h"

#include <cstdint>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

UserTracker::UserTracker(TraceProcessorContext* context) : context_(context) {}

void UserTracker::AddOrUpdateUser(int64_t android_user_id, StringId user_type) {
  auto* it = user_rows_.Find(android_user_id);
  if (it) {
    auto row = context_->storage->mutable_user_list_table()->FindById(*it);
    row->set_type(user_type);
    return;
  }
  auto id = context_->storage->mutable_user_list_table()->Insert(
      {user_type, android_user_id});
  user_rows_.Insert(android_user_id, id.id);
}

}  // namespace perfetto::trace_processor
