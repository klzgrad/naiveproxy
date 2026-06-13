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

#include "src/trace_processor/importers/perf/sample_id.h"
#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_event_attr.h"
#include "src/trace_processor/importers/perf/reader.h"

namespace perfetto::trace_processor::perf_importer {

base::Status SampleId::ParseFromRecord(const Record& record) {
  PERFETTO_CHECK(record.header.type != PERF_RECORD_SAMPLE);
  if (!record.attr || !record.attr->sample_id_all()) {
    sample_type_ = 0;
    return base::OkStatus();
  }

  Reader reader(record.payload.copy());

  size_t size = record.attr->sample_id_size();
  if (size > record.payload.size()) {
    return base::ErrStatus(
        "Record is too small to hold a SampleId. Expected at least %zu bytes, "
        "but found %zu",
        size, record.payload.size());
  }

  PERFETTO_CHECK(reader.Skip(record.payload.size() - size));
  if (!ReadFrom(*record.attr, reader)) {
    return base::ErrStatus("Failed to parse SampleId");
  }
  return base::OkStatus();
}

bool SampleId::ReadFrom(const PerfEventAttr& attr, Reader& reader) {
  sample_type_ = attr.sample_type();

  if (sample_type_ & PERF_SAMPLE_TID) {
    if (!reader.Read(pid_) || !reader.Read(tid_)) {
      return false;
    }
  }
  if (sample_type_ & PERF_SAMPLE_TIME) {
    if (!reader.Read(time_)) {
      return false;
    }
  }
  if (sample_type_ & PERF_SAMPLE_ID) {
    if (!reader.Read(id_)) {
      return false;
    }
  }
  if (sample_type_ & PERF_SAMPLE_STREAM_ID) {
    if (!reader.Read(stream_id_)) {
      return false;
    }
  }
  if (sample_type_ & PERF_SAMPLE_CPU) {
    if (!reader.Read(cpu_) || !reader.Skip(sizeof(uint32_t))) {
      return false;
    }
  }
  if (sample_type_ & PERF_SAMPLE_IDENTIFIER) {
    if (!reader.Read(id_)) {
      return false;
    }
  }
  return true;
}

}  // namespace perfetto::trace_processor::perf_importer
