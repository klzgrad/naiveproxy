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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_PARSER_H_

#include <cstdint>

#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::simpleperf_proto_importer {

struct SimpleperfProtoEvent {
  int64_t ts = 0;
  TraceBlobView record_data;
};

class SimpleperfProtoParser
    : public TraceSorter::Sink<SimpleperfProtoEvent, SimpleperfProtoParser> {
 public:
  SimpleperfProtoParser(TraceProcessorContext* context,
                        SimpleperfProtoTracker* tracker);
  ~SimpleperfProtoParser() override;

  void Parse(int64_t ts, const SimpleperfProtoEvent& event);

 private:
  TraceProcessorContext* const context_;
  SimpleperfProtoTracker* const tracker_;
};

}  // namespace perfetto::trace_processor::simpleperf_proto_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_PARSER_H_
