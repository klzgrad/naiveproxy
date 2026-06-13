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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_PARSER_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "protos/third_party/primes/primes_tracing.pbzero.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::primes {
namespace primespb = perfetto::third_party::primes::pbzero;

class PrimesTraceParser
    : public TraceSorter::Sink<TraceBlobView, PrimesTraceParser> {
 public:
  explicit PrimesTraceParser(TraceProcessorContext*);
  ~PrimesTraceParser() override;

  void Parse(int64_t ts, TraceBlobView trace_edge);

 private:
  TraceProcessorContext* const context_;
  base::FlatHashMap<int64_t, int64_t> edge_to_executor_map_;

  void HandleSliceBegin(int64_t ts, primespb::TraceEdge::Decoder& edge_decoder);
  void HandleSliceEnd(int64_t ts, primespb::TraceEdge::Decoder& edge_decoder);
  void HandleMark(int64_t ts, primespb::TraceEdge::Decoder& edge_decoder);
  void HandleFlows(
      TrackId track_id,
      const primespb::TraceEdge::TraceEntityDetails::Decoder& details);
  StringPool::Id edge_id_string_;
  StringPool::Id parent_id_string_;
  StringPool::Id debug_edge_id_;
};

}  // namespace perfetto::trace_processor::primes

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PRIMES_PRIMES_TRACE_PARSER_H_
