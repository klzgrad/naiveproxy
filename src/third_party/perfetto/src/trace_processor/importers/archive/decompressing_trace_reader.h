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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_DECOMPRESSING_TRACE_READER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_DECOMPRESSING_TRACE_READER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "perfetto/base/status.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/util/decompressor.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Parses a whole-file compressed trace (gzip or zstd), streaming the
// decompressed bytes into an inner ForwardingTraceParser. The codec is fixed at
// construction; the trace type detection picks it (kGzipTraceType,
// kCtraceTraceType -> gzip; kZstdTraceType -> zstd).
class DecompressingTraceReader : public ChunkedTraceReader {
 public:
  DecompressingTraceReader(TraceProcessorContext*, util::CompressionType);
  DecompressingTraceReader(std::unique_ptr<ChunkedTraceReader>,
                           util::CompressionType);
  ~DecompressingTraceReader() override;

  // ChunkedTraceReader implementation
  base::Status Parse(TraceBlobView) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override;

  base::Status ParseUnowned(const uint8_t*, size_t);

 private:
  TraceProcessorContext* const context_;
  const util::CompressionType type_;
  std::unique_ptr<util::Decompressor> decompressor_;
  std::unique_ptr<ChunkedTraceReader> inner_;

  std::unique_ptr<uint8_t[]> buffer_;
  size_t bytes_written_ = 0;

  bool first_chunk_parsed_ = false;
  enum { kStreamBoundary, kMidStream } output_state_ = kStreamBoundary;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_DECOMPRESSING_TRACE_READER_H_
