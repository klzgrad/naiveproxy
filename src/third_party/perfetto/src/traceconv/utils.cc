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

#include "src/traceconv/utils.h"

#include <stdio.h>

#include <cinttypes>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/trace_processor.h"

#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "protos/perfetto/trace/profiling/heap_graph.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace trace_to_text {
namespace {

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
constexpr size_t kCompressionBufferSize = 500 * 1024;
#endif

}  // namespace

bool ReadTraceUnfinalized(trace_processor::TraceProcessor* tp,
                          std::istream* input) {
  // 1MB chunk size seems the best tradeoff on a MacBook Pro 2013 - i7 2.8 GHz.
  constexpr size_t kChunkSize = 1024 * 1024;

// Printing the status update on stderr can be a perf bottleneck. On WASM print
// status updates more frequently because it can be slower to parse each chunk.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  constexpr int kStderrRate = 1;
#else
  constexpr int kStderrRate = 128;
#endif
  uint64_t file_size = 0;

  for (int i = 0;; i++) {
    if (i % kStderrRate == 0) {
      fprintf(stderr, "Loading trace %.2f MB%c",
              static_cast<double>(file_size) / 1.0e6, kProgressChar);
      fflush(stderr);
    }

    std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunkSize]);
    input->read(reinterpret_cast<char*>(buf.get()), kChunkSize);
    if (input->bad()) {
      PERFETTO_ELOG("Failed when reading trace");
      return false;
    }

    auto rsize = input->gcount();
    if (rsize <= 0)
      break;
    file_size += static_cast<uint64_t>(rsize);
    tp->Parse(std::move(buf), static_cast<size_t>(rsize));
  }

  fprintf(stderr, "Loaded trace%c", kProgressChar);
  fflush(stderr);
  return true;
}

void IngestTraceOrDie(trace_processor::TraceProcessor* tp,
                      const std::string& trace_proto) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
  memcpy(buf.get(), trace_proto.data(), trace_proto.size());
  auto status = tp->Parse(std::move(buf), trace_proto.size());
  if (!status.ok()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s", status.message().c_str());
  }
}

TraceWriter::TraceWriter(std::ostream* output) : output_(output) {}

TraceWriter::~TraceWriter() {
  output_->flush();
}

void TraceWriter::Write(const std::string& s) {
  Write(s.data(), s.size());
}

void TraceWriter::Write(const char* data, size_t sz) {
  output_->write(data, static_cast<std::streamsize>(sz));
}

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)

DeflateTraceWriter::DeflateTraceWriter(std::ostream* output)
    : TraceWriter(output),
      buf_(base::PagedMemory::Allocate(kCompressionBufferSize)),
      start_(static_cast<uint8_t*>(buf_.Get())),
      end_(start_ + buf_.size()) {
  CheckEq(deflateInit(&stream_, 9), Z_OK);
  stream_.next_out = start_;
  stream_.avail_out = static_cast<unsigned int>(end_ - start_);
}

DeflateTraceWriter::~DeflateTraceWriter() {
  // Drain compressor until it has no more input, and has flushed its internal
  // buffers.
  while (deflate(&stream_, Z_FINISH) != Z_STREAM_END) {
    Flush();
  }
  // Flush any outstanding output bytes to the backing TraceWriter.
  Flush();
  PERFETTO_CHECK(stream_.avail_out == static_cast<size_t>(end_ - start_));

  CheckEq(deflateEnd(&stream_), Z_OK);
}

void DeflateTraceWriter::Write(const char* data, size_t sz) {
  stream_.next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(data));
  stream_.avail_in = static_cast<unsigned int>(sz);
  while (stream_.avail_in > 0) {
    CheckEq(deflate(&stream_, Z_NO_FLUSH), Z_OK);
    if (stream_.avail_out == 0) {
      Flush();
    }
  }
}

void DeflateTraceWriter::Flush() {
  TraceWriter::Write(reinterpret_cast<char*>(start_),
                     static_cast<size_t>(stream_.next_out - start_));
  stream_.next_out = start_;
  stream_.avail_out = static_cast<unsigned int>(end_ - start_);
}

void DeflateTraceWriter::CheckEq(int actual_code, int expected_code) {
  if (actual_code == expected_code)
    return;
  PERFETTO_FATAL("Expected %d got %d: %s", actual_code, expected_code,
                 stream_.msg);
}
#else

DeflateTraceWriter::DeflateTraceWriter(std::ostream* output)
    : TraceWriter(output) {
  PERFETTO_ELOG("Cannot compress. Zlib is not enabled in the build config");
}
DeflateTraceWriter::~DeflateTraceWriter() = default;

#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZLIB)

}  // namespace trace_to_text
}  // namespace perfetto
