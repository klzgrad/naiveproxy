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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_TOKENIZER_H_

#include <cstdint>
#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_parser.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::simpleperf_proto_importer {

class SimpleperfProtoTokenizer : public ChunkedTraceReader {
 public:
  explicit SimpleperfProtoTokenizer(TraceProcessorContext*);
  ~SimpleperfProtoTokenizer() override;

  // ChunkedTraceReader implementation.
  base::Status Parse(TraceBlobView) override;
  base::Status NotifyEndOfFile() override;

 private:
  enum class State : uint8_t {
    kExpectingMagic,
    kExpectingVersion,
    kExpectingRecordSize,
    kExpectingRecord,
    kFinished
  };

  base::Status ParseMagic();
  base::Status ParseVersion();
  base::Status ParseRecordSize();
  base::Status ParseRecord();

  TraceProcessorContext* const context_;
  util::TraceBlobViewReader reader_;
  State state_ = State::kExpectingMagic;
  uint32_t current_record_size_ = 0;
  int64_t last_seen_timestamp_ = 0;
  std::unique_ptr<TraceSorter::Stream<SimpleperfProtoEvent>> stream_;

  // Tracker for simpleperf metadata (symbols, mappings, event types)
  SimpleperfProtoTracker tracker_;
};

}  // namespace perfetto::trace_processor::simpleperf_proto_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_SIMPLEPERF_PROTO_SIMPLEPERF_PROTO_TOKENIZER_H_
