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

#include "src/trace_processor/importers/perf/attrs_section_reader.h"

#include <cinttypes>
#include <cstddef>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/perf/perf_file.h"

namespace perfetto::trace_processor::perf_importer {

// static
base::StatusOr<AttrsSectionReader> AttrsSectionReader::Create(
    const PerfFile::Header& header,
    TraceBlobView section) {
  PERFETTO_CHECK(section.size() == header.attrs.size);

  if (header.attr_size == 0) {
    return base::ErrStatus("Invalid attr_size (0) in perf file header.");
  }

  if (header.attrs.size % header.attr_size != 0) {
    return base::ErrStatus("Invalid attrs section size %" PRIu64
                           " for attr_size %" PRIu64 " in perf file header.",
                           header.attrs.size, header.attr_size);
  }

  const size_t num_attr = header.attrs.size / header.attr_size;

  // Each entry is a perf_event_attr followed by a Section, but the size of
  // the perf_event_attr struct written in the file might not be the same as
  // sizeof(perf_event_attr) as this struct might grow over time (can be
  // bigger or smaller).
  static constexpr size_t kSectionSize = sizeof(PerfFile::Section);
  if (header.attr_size < kSectionSize) {
    return base::ErrStatus(
        "Invalid attr_size in file header. Expected at least %zu, found "
        "%" PRIu64,
        kSectionSize, header.attr_size);
  }
  const size_t attr_size = header.attr_size - kSectionSize;

  return AttrsSectionReader(std::move(section), num_attr, attr_size);
}

base::Status AttrsSectionReader::ReadNext(PerfFile::AttrsEntry& entry) {
  PERFETTO_CHECK(reader_.ReadPerfEventAttr(entry.attr, attr_size_));

  if (entry.attr.size != attr_size_) {
    return base::ErrStatus(
        "Invalid attr.size. Expected %zu, but found %" PRIu32, attr_size_,
        entry.attr.size);
  }

  PERFETTO_CHECK(reader_.Read(entry.ids));
  --num_attr_;
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::perf_importer
