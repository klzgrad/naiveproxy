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

#include "src/trace_processor/importers/etm/file_tracker.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"

#include "src/trace_processor/importers/etm/elf_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto::trace_processor::etm {

FileTracker::~FileTracker() = default;

base::Status FileTracker::AddFile(const std::string& name, TraceBlobView data) {
  StringId name_id = context_->storage->InternString(name);
  auto it = files_by_path_.Find(name_id);
  if (it) {
    return base::ErrStatus("Duplicate file: %s", name.c_str());
  }
  auto file_id = context_->storage->mutable_file_table()
                     ->Insert({name_id, static_cast<int64_t>(data.size())})
                     .id;
  files_by_path_.Insert(name_id, file_id);

  PERFETTO_CHECK(file_content_.size() == file_id.value);
  file_content_.push_back(std::move(data));

  IndexFileType(file_id, file_content_.back());

  return base::OkStatus();
}

void FileTracker::IndexFileType(tables::FileTable::Id file_id,
                                const TraceBlobView& content) {
  if (ElfTracker::GetOrCreate(context_)->ProcessFile(file_id, content)) {
    return;
  }
}

}  // namespace perfetto::trace_processor::etm
