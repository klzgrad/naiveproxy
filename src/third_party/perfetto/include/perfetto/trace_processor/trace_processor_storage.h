/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_STORAGE_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_STORAGE_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "perfetto/base/export.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_blob_view.h"

namespace perfetto::trace_processor {

// Coordinates the loading of traces from an arbitrary source.
class PERFETTO_EXPORT_COMPONENT TraceProcessorStorage {
 public:
  // Creates a new instance of TraceProcessorStorage.
  static std::unique_ptr<TraceProcessorStorage> CreateInstance(const Config&);

  virtual ~TraceProcessorStorage();

  // See comment on TraceProcessor::Parse.
  virtual base::Status Parse(TraceBlobView) = 0;

  // See comment on TraceProcessor::Parse.
  base::Status Parse(std::unique_ptr<uint8_t[]> buf, size_t size);

  // See comment on TraceProcessor::Flush.
  virtual void Flush() = 0;

  // See comment on TraceProcessor::NotifyEndOfFile.
  virtual base::Status NotifyEndOfFile() = 0;
};

}  // namespace perfetto::trace_processor

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_STORAGE_H_
