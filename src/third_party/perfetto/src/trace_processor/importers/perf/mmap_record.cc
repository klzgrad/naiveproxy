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

#include "src/trace_processor/importers/perf/mmap_record.h"

#include <optional>

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/record.h"

namespace perfetto::trace_processor::perf_importer {

base::Status MmapRecord::Parse(const Record& record) {
  Reader reader(record.payload.copy());
  if (!reader.Read(*static_cast<CommonMmapRecordFields*>(this)) ||
      !reader.ReadCString(filename)) {
    return base::ErrStatus("Failed to parse MMAP record");
  }
  cpu_mode = record.GetCpuMode();
  return base::OkStatus();
}

base::Status Mmap2Record::Parse(const Record& record) {
  Reader reader(record.payload.copy());
  if (!reader.Read(*static_cast<BaseMmap2Record*>(this)) ||
      !reader.ReadCString(filename)) {
    return base::ErrStatus("Failed to parse MMAP record");
  }

  has_build_id = record.mmap_has_build_id();

  if (has_build_id && build_id.build_id_size >
                          BaseMmap2Record::BuildIdFields::kMaxBuildIdSize) {
    return base::ErrStatus(
        "Invalid build_id_size in MMAP2 record. Expected <= %zu but found "
        "%" PRIu8,
        BaseMmap2Record::BuildIdFields::kMaxBuildIdSize,
        build_id.build_id_size);
  }

  cpu_mode = record.GetCpuMode();

  return base::OkStatus();
}

std::optional<BuildId> Mmap2Record::GetBuildId() const {
  return has_build_id ? std::make_optional(BuildId::FromRaw(std::string(
                            build_id.build_id_buf, build_id.build_id_size)))
                      : std::nullopt;
}

}  // namespace perfetto::trace_processor::perf_importer
