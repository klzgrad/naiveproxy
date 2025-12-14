/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PPROF_PPROF_TRACE_READER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PPROF_PPROF_TRACE_READER_H_

#include <cstdint>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class PprofTraceReader : public ChunkedTraceReader {
 public:
  explicit PprofTraceReader(TraceProcessorContext* context);
  ~PprofTraceReader() override;

  base::Status Parse(TraceBlobView blob) override;
  base::Status NotifyEndOfFile() override;

 private:
  base::Status ParseProfile();

  TraceProcessorContext* const context_;
  std::vector<uint8_t> buffer_;

  // Constant strings interned at construction time
  const StringId unknown_string_id_;
  const StringId unknown_no_brackets_string_id_;
  const StringId count_string_id_;
  const StringId pprof_file_string_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PPROF_PPROF_TRACE_READER_H_
