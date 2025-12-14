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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_JSON_JSON_TRACE_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_JSON_JSON_TRACE_TOKENIZER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/json/json_trace_parser.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/importers/systrace/systrace_line_tokenizer.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/util/json_parser.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

enum class ReadKeyRes {
  kFoundKey,
  kNeedsMoreData,
  kEndOfDictionary,
  kFatalError,
};

// Parses at most one JSON key and returns a pointer to the start of the value
// associated with that key.
// This is to avoid decoding the full trace in memory and reduce heap traffic.
// E.g. input:  a:1 b:{ c:2}}
//     output:    ^ return value points here, key is set to "a".
// Note: even if the whole key may be available, this method will return
// kNeedsMoreData until the first character of the value is available.
// Visible for testing.
ReadKeyRes ReadOneJsonKey(const char* start,
                          const char* end,
                          std::string* key,
                          const char** next);

enum class ReadSystemLineRes : uint8_t {
  kFoundLine,
  kNeedsMoreData,
  kEndOfSystemTrace,
  kFatalError,
};

ReadSystemLineRes ReadOneSystemTraceLine(const char* start,
                                         const char* end,
                                         std::string* line,
                                         const char** next);

// Reads a JSON trace in chunks and extracts top level json objects.
class JsonTraceTokenizer : public ChunkedTraceReader {
 public:
  explicit JsonTraceTokenizer(TraceProcessorContext*);
  ~JsonTraceTokenizer() override;

  // ChunkedTraceReader implementation.
  base::Status Parse(TraceBlobView) override;
  base::Status NotifyEndOfFile() override;

 private:
  // Enum which tracks which type of JSON trace we are dealing with.
  enum class TraceFormat : uint8_t {
    // Enum value when ther outer-most layer is a dictionary with multiple
    // key value pairs
    kOuterDictionary,

    // Enum value when we only have trace events (i.e. the outermost
    // layer is just a array of trace events).
    kOnlyTraceEvents,
  };

  // Enum which tracks our current position within the trace.
  enum class TracePosition : uint8_t {
    // This indicates that we are inside the outermost dictionary of the
    // trace and need to read the next key of the dictionary.
    // This position is only valid when the |format_| == |kOuterDictionary|.
    kDictionaryKey,

    // This indicates we are inside the systemTraceEvents string.
    // This position is only valid when the |format_| == |kOuterDictionary|.
    kInsideSystemTraceEventsString,

    // This indicates where are inside the traceEvents array.
    kInsideTraceEventsArray,

    // This indicates we cannot parse any more data in the trace.
    kEof,
  };

  base::Status ParseInternal(const char* start,
                             const char* end,
                             const char** out);

  bool ParseTraceEventContents();

  base::Status ParseV8SampleEvent(const JsonEvent& event);

  base::Status HandleTraceEvent(const char* start,
                                const char* end,
                                const char** out);

  base::Status HandleDictionaryKey(const char* start,
                                   const char* end,
                                   const char** out);

  base::Status HandleSystemTraceEvent(const char* start,
                                      const char* end,
                                      const char** out);

  TraceProcessorContext* const context_;

  JsonTraceParser parser_;
  std::unique_ptr<LegacyV8CpuProfileTracker> v8_tracker_;
  std::unique_ptr<TraceSorter::Stream<JsonEvent>> json_stream_;
  std::unique_ptr<TraceSorter::Stream<SystraceLine>> systrace_stream_;
  std::unique_ptr<TraceSorter::Stream<LegacyV8CpuProfileEvent>> v8_stream_;

  TraceFormat format_ = TraceFormat::kOuterDictionary;
  TracePosition position_ = TracePosition::kDictionaryKey;

  SystraceLineTokenizer systrace_line_tokenizer_;
  json::Iterator it_;
  json::Iterator inner_it_;

  uint64_t offset_ = 0;
  // Used to glue together JSON objects that span across two (or more)
  // Parse boundaries.
  std::vector<char> buffer_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_JSON_JSON_TRACE_TOKENIZER_H_
