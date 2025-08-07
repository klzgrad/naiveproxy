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

#include "src/trace_processor/importers/perf/itrace_start_record.h"

#include "src/trace_processor/importers/perf/reader.h"

namespace perfetto::trace_processor::perf_importer {

base::Status ItraceStartRecord::Parse(const Record& record) {
  attr = record.attr;
  Reader reader(record.payload.copy());

  if (!reader.Read(pid) || !reader.Read(tid)) {
    return base::ErrStatus("Failed to parse PERF_RECORD_ITRACE_START");
  }

  if (!record.has_trailing_sample_id()) {
    sample_id.reset();
    return base::OkStatus();
  }

  sample_id.emplace();
  return sample_id->ParseFromRecord(record);
}

}  // namespace perfetto::trace_processor::perf_importer
