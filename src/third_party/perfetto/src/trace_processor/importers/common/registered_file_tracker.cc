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

#include "src/trace_processor/importers/common/registered_file_tracker.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/etm_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"
#include "src/trace_processor/util/elf/binary_info.h"

namespace perfetto::trace_processor {

base::Status RegisteredFileTracker::AddFile(const std::string& name,
                                            TraceBlob data) {
  StringId name_id = context_->storage->InternString(name);
  auto* it = files_by_path_.Find(name_id);
  if (it) {
    return base::ErrStatus("Duplicate file: %s", name.c_str());
  }
  auto file_id = context_->storage->mutable_file_table()
                     ->Insert({name_id, static_cast<int64_t>(data.size())})
                     .id;
  files_by_path_.Insert(name_id, file_id);

  PERFETTO_CHECK(file_content_.size() == file_id.value);
  file_content_.push_back(std::move(data));

  const auto& content = file_content_.back();
  auto bin_info = elf::GetBinaryInfo(content.data(), content.size());
  if (!bin_info) {
    return base::OkStatus();
  }
  std::optional<BuildId> build_id;
  if (bin_info->build_id) {
    build_id = BuildId::FromRaw(*bin_info->build_id);
  }

  tables::ElfFileTable::Row row;
  row.file_id = file_id;
  row.load_bias = static_cast<int64_t>(bin_info->load_bias);

  if (build_id) {
    row.build_id = context_->storage->InternString(
        BuildId::FromRaw(*bin_info->build_id).ToHex());
  }

  auto id = context_->storage->mutable_elf_file_table()->Insert(row).id;
  if (build_id) {
    files_by_build_id_.Insert(*build_id, id);
  }
  return base::OkStatus();
}
TraceBlob& RegisteredFileTracker::GetContent(tables::FileTable::Id id) {
  PERFETTO_DCHECK(id.value < file_content_.size());
  return file_content_[id.value];
}

}  // namespace perfetto::trace_processor
