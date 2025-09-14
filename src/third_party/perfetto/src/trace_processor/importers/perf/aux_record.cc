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

#include "src/trace_processor/importers/perf/aux_record.h"
#include <cstdint>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/record.h"
#include "src/trace_processor/importers/perf/sample_id.h"
#include "src/trace_processor/importers/perf/util.h"

namespace perfetto::trace_processor::perf_importer {
// static
base::Status AuxRecord::Parse(const Record& record) {
  attr = record.attr;
  Reader reader(record.payload.copy());
  if (!reader.Read(offset) || !reader.Read(size) || !reader.Read(flags)) {
    return base::ErrStatus("Failed to parse AUX record");
  }

  if (offset > std::numeric_limits<decltype(offset)>::max() - size) {
    return base::ErrStatus("AUX record overflows");
  }

  if (!record.has_trailing_sample_id()) {
    sample_id.reset();
    return base::OkStatus();
  }

  sample_id.emplace();
  return sample_id->ParseFromRecord(record);
}

}  // namespace perfetto::trace_processor::perf_importer
