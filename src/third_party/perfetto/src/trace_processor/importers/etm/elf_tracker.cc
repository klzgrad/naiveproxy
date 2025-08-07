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

#include "src/trace_processor/importers/etm/elf_tracker.h"

#include <optional>

#include "src/trace_processor/importers/elf/binary_info.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto::trace_processor::etm {

ElfTracker::~ElfTracker() = default;

bool ElfTracker::ProcessFile(tables::FileTable::Id file_id,
                             const TraceBlobView& content) {
  auto bin_info = elf::GetBinaryInfo(content.data(), content.size());
  if (!bin_info) {
    return false;
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

  return true;
}

}  // namespace perfetto::trace_processor::etm
