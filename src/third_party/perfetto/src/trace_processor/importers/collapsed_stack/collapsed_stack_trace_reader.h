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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COLLAPSED_STACK_COLLAPSED_STACK_TRACE_READER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COLLAPSED_STACK_COLLAPSED_STACK_TRACE_READER_H_

#include <optional>
#include <string_view>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Reader for collapsed stack format (Brendan Gregg's flamegraph format).
// Format: frame1;frame2;frame3 count
// Lines starting with # are comments.
class CollapsedStackTraceReader : public ChunkedTraceReader {
 public:
  explicit CollapsedStackTraceReader(TraceProcessorContext* context);
  ~CollapsedStackTraceReader() override;

  base::Status Parse(TraceBlobView blob) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override {}

 private:
  base::Status ParseLine(std::string_view line);

  TraceProcessorContext* const context_;
  util::TraceBlobViewReader reader_;
  DummyMemoryMapping* mapping_ = nullptr;
  std::optional<tables::AggregateProfileTable::Id> profile_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COLLAPSED_STACK_COLLAPSED_STACK_TRACE_READER_H_
