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

#include "src/trace_processor/importers/perf/auxtrace_record.h"

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/record.h"

namespace perfetto::trace_processor::perf_importer {

base::Status AuxtraceRecord::Parse(const Record& record) {
  Reader reader(record.payload.copy());
  if (!reader.Read(*this)) {
    return base::ErrStatus("Failed to parse PERF_RECORD_AUXTRACE");
  }

  if (offset > std::numeric_limits<decltype(offset)>::max() - size) {
    return base::ErrStatus("AUXTRACE record overflows");
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::perf_importer
