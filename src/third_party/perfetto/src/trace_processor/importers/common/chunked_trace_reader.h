/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CHUNKED_TRACE_READER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CHUNKED_TRACE_READER_H_

#include <cstddef>

#include "perfetto/base/status.h"

namespace perfetto::trace_processor {

class TraceBlobView;

// Base interface for first stage of parsing pipeline
// (JsonTraceParser, ProtoTraceReader).
class ChunkedTraceReader {
 public:
  virtual ~ChunkedTraceReader();

  // Pushes more data into the trace parser. There is no requirement for the
  // caller to match line/protos boundaries. The parser class has to deal with
  // intermediate buffering lines/protos that span across different chunks.
  // The buffer size is guaranteed to be > 0.
  virtual base::Status Parse(TraceBlobView) = 0;

  // Called after the last Parse() call.
  [[nodiscard]] virtual base::Status NotifyEndOfFile() = 0;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CHUNKED_TRACE_READER_H_
