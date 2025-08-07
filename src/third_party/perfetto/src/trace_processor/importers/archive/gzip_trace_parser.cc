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

#include "src/trace_processor/importers/archive/gzip_trace_parser.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/gzip_utils.h"

namespace perfetto::trace_processor {

namespace {

using ResultCode = util::GzipDecompressor::ResultCode;

}  // namespace

GzipTraceParser::GzipTraceParser(TraceProcessorContext* context)
    : context_(context) {}

GzipTraceParser::GzipTraceParser(std::unique_ptr<ChunkedTraceReader> reader)
    : context_(nullptr), inner_(std::move(reader)) {}

GzipTraceParser::~GzipTraceParser() = default;

base::Status GzipTraceParser::Parse(TraceBlobView blob) {
  return ParseUnowned(blob.data(), blob.size());
}

base::Status GzipTraceParser::ParseUnowned(const uint8_t* data, size_t size) {
  const uint8_t* start = data;
  size_t len = size;

  if (!inner_) {
    PERFETTO_CHECK(context_);
    inner_.reset(new ForwardingTraceParser(
        context_, context_->trace_file_tracker->AddFile("")));
  }

  if (!first_chunk_parsed_) {
    // .ctrace files begin with: "TRACE:\n" or "done. TRACE:\n" strip this if
    // present.
    base::StringView beginning(reinterpret_cast<const char*>(start), size);

    static const char* kSystraceFileHeader = "TRACE:\n";
    size_t offset = Find(kSystraceFileHeader, beginning);
    if (offset != std::string::npos) {
      start += strlen(kSystraceFileHeader) + offset;
      len -= strlen(kSystraceFileHeader) + offset;
    }
    first_chunk_parsed_ = true;
  }

  // Our default uncompressed buffer size is 32MB as it allows for good
  // throughput.
  constexpr size_t kUncompressedBufferSize = 32ul * 1024 * 1024;
  decompressor_.Feed(start, len);

  for (;;) {
    if (!buffer_) {
      buffer_.reset(new uint8_t[kUncompressedBufferSize]);
      bytes_written_ = 0;
    }

    auto result =
        decompressor_.ExtractOutput(buffer_.get() + bytes_written_,
                                    kUncompressedBufferSize - bytes_written_);
    util::GzipDecompressor::ResultCode ret = result.ret;
    if (ret == ResultCode::kError)
      return base::ErrStatus("Failed to decompress trace chunk");

    if (ret == ResultCode::kNeedsMoreInput) {
      PERFETTO_DCHECK(result.bytes_written == 0);
      return base::OkStatus();
    }
    bytes_written_ += result.bytes_written;
    output_state_ = kMidStream;

    if (bytes_written_ == kUncompressedBufferSize || ret == ResultCode::kEof) {
      TraceBlob blob =
          TraceBlob::TakeOwnership(std::move(buffer_), bytes_written_);
      RETURN_IF_ERROR(inner_->Parse(TraceBlobView(std::move(blob))));
    }

    // We support multiple gzip streams in a single gzip file (which is valid
    // according to RFC1952 section 2.2): in that case, we just need to reset
    // the decompressor to begin processing the next stream: all other variables
    // can be preserved.
    if (ret == ResultCode::kEof) {
      decompressor_.Reset();
      output_state_ = kStreamBoundary;

      if (decompressor_.AvailIn() == 0) {
        return base::OkStatus();
      }
    }
  }
}

base::Status GzipTraceParser::NotifyEndOfFile() {
  if (output_state_ != kStreamBoundary || decompressor_.AvailIn() > 0) {
    return base::ErrStatus("GZIP stream incomplete, trace is likely corrupt");
  }
  PERFETTO_CHECK(!buffer_);
  return inner_ ? inner_->NotifyEndOfFile() : base::OkStatus();
}

}  // namespace perfetto::trace_processor
