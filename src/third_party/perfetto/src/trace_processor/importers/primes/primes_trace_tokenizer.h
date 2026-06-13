/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_TOKENIZER_H_

#include <memory>

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::primes {

/**
 * Buffers an incoming Primes trace and tokenizes it into TraceEdge messages for
 * parsing.
 */
class PrimesTraceTokenizer : public ChunkedTraceReader {
 public:
  explicit PrimesTraceTokenizer(TraceProcessorContext*);
  ~PrimesTraceTokenizer() override;
  base::Status Parse(TraceBlobView) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override {}

 private:
  util::TraceBlobViewReader reader_;
  TraceProcessorContext* const context_;
  ClockTracker::ClockId trace_file_clock_;
  std::unique_ptr<TraceSorter::Stream<TraceBlobView>> stream_;
};

}  // namespace perfetto::trace_processor::primes

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_TOKENIZER_H_
