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

#include "src/trace_processor/importers/primes/primes_trace_tokenizer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/primes/primes_trace_parser.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/util/clock_synchronizer.h"

#include "protos/third_party/primes/primes_tracing.pbzero.h"

namespace primespb = perfetto::third_party::primes::pbzero;

namespace perfetto::trace_processor::primes {

PrimesTraceTokenizer::PrimesTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx),
      trace_file_clock_(ClockId::TraceFile(ctx->trace_id().value)),
      stream_(
          ctx->sorter->CreateStream(std::make_unique<PrimesTraceParser>(ctx))) {
}

PrimesTraceTokenizer::~PrimesTraceTokenizer() = default;

namespace {

static int64_t ToNanos(int64_t seconds, int32_t nanos) {
  return seconds * 1000000000LL + nanos;
}

}  // namespace

// Uses ProtoDecoder to buffer and parse the Trace message.
// 1. Buffers all incoming TraceBlobView chunks.
// 2. On NotifyEndOfFile, extracts the trace start time and TraceEdge messages.
// 3. Pushes (timestamp, TraceBlobView) pairs to the TraceSorter stream for full
// parsing.
base::Status PrimesTraceTokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));
  return base::OkStatus();
}

base::Status PrimesTraceTokenizer::OnPushDataToSorter() {
  size_t available_bytes = reader_.avail();
  auto slice = reader_.SliceOff(reader_.start_offset(), available_bytes);
  if (!slice.has_value()) {
    return base::ErrStatus(
        "Slicing TraceBlobView for Primes trace proto unexpectedly failed.");
  }
  primespb::Trace::Decoder decoder(slice->data(), slice->size());

  // Start time needs to be extracted before the timestamp of any edge can be
  // calculated, as edge timestamps are stored as an offset to the trace start
  // time.
  if (!decoder.has_start_time()) {
    return base::ErrStatus("Primes Trace proto did not contain a start time.");
  }
  primespb::Timestamp::Decoder ts_decoder(decoder.start_time());
  int64_t start_time = ToNanos(ts_decoder.seconds(), ts_decoder.nanos());

  for (auto edge = decoder.edges(); edge; ++edge) {
    // Calculate the timestamp for this edge.
    // We need to decode the edge to find its offset from the start time.
    primespb::TraceEdge::Decoder edge_decoder(*edge);
    if (!edge_decoder.has_trace_start_offset()) {
      PERFETTO_ELOG("Edge missing trace_start_offset.");
      context_->import_logs_tracker->RecordTokenizationError(
          stats::primes_malformed_timestamp, ts_decoder.read_offset());
      continue;
    }
    primespb::Duration::Decoder offset_decoder(
        edge_decoder.trace_start_offset());
    int64_t edge_timestamp =
        start_time + ToNanos(offset_decoder.seconds(), offset_decoder.nanos());

    // Create a TraceBlobView for the edge data.
    TraceBlobView edge_slice = slice->slice_off(
        static_cast<size_t>((*edge).data - slice->data()), (*edge).size);
    auto trace_ts =
        context_->clock_tracker->ToTraceTime(trace_file_clock_, edge_timestamp);
    if (trace_ts) {
      stream_->Push(*trace_ts, std::move(edge_slice));
    }
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::primes
