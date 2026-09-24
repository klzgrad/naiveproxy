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

#include "src/trace_processor/importers/archive/decompressing_trace_reader.h"

#include <algorithm>
#include <cstddef>
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
#include "src/trace_processor/importers/common/builtin_trace_importers.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/decompressor.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

DecompressingTraceReader::DecompressingTraceReader(
    TraceProcessorContext* context,
    util::CompressionType type)
    : context_(context),
      type_(type),
      decompressor_(util::CreateDecompressor(type)) {}

DecompressingTraceReader::DecompressingTraceReader(
    std::unique_ptr<ChunkedTraceReader> reader,
    util::CompressionType type)
    : context_(nullptr),
      type_(type),
      decompressor_(util::CreateDecompressor(type)),
      inner_(std::move(reader)) {}

DecompressingTraceReader::~DecompressingTraceReader() = default;

base::Status DecompressingTraceReader::Parse(TraceBlobView blob) {
  return ParseUnowned(blob.data(), blob.size());
}

base::Status DecompressingTraceReader::ParseUnowned(const uint8_t* data,
                                                    size_t size) {
  if (!decompressor_) {
    auto info = util::GetCompressionCodecInfo(type_);
    return base::ErrStatus(
        "Cannot decompress trace: %s is not enabled in this build (rebuild "
        "with %s=true)",
        info.name, info.gn_arg);
  }

  const uint8_t* start = data;
  size_t len = size;

  if (!inner_) {
    PERFETTO_CHECK(context_);
    inner_.reset(new ForwardingTraceParser(
        context_, context_->trace_file_tracker->AddFile("")));
  }

  if (!first_chunk_parsed_) {
    first_chunk_parsed_ = true;
    // .ctrace files (gzip) begin with "TRACE:\n" or "done. TRACE:\n"; strip it
    // if present. This framing is gzip-only, so don't look for it otherwise.
    if (type_ == util::CompressionType::kGzip) {
      base::StringView beginning(reinterpret_cast<const char*>(start), size);
      static const base::StringView kSystraceFileHeader("TRACE:\n");
      size_t offset = base::Find(kSystraceFileHeader, beginning);
      if (offset != std::string::npos) {
        start += kSystraceFileHeader.size() + offset;
        len -= kSystraceFileHeader.size() + offset;
      }
    }
  }

  // Our default uncompressed buffer size is 32MB as it allows for good
  // throughput.
  constexpr size_t kUncompressedBufferSize = 32ul * 1024 * 1024;
  decompressor_->Feed(start, len);

  using ResultCode = util::Decompressor::ResultCode;
  for (;;) {
    if (!buffer_) {
      buffer_.reset(new uint8_t[kUncompressedBufferSize]);
      bytes_written_ = 0;
    }

    auto result =
        decompressor_->ExtractOutput(buffer_.get() + bytes_written_,
                                     kUncompressedBufferSize - bytes_written_);
    if (result.ret == ResultCode::kError)
      return base::ErrStatus(
          "Failed to decompress trace chunk (ERR:tp-corrupt)");

    if (result.ret == ResultCode::kNeedsMoreInput) {
      PERFETTO_DCHECK(result.bytes_written == 0);
      return base::OkStatus();
    }
    bytes_written_ += result.bytes_written;
    output_state_ = kMidStream;

    if (bytes_written_ == kUncompressedBufferSize ||
        result.ret == ResultCode::kEof) {
      TraceBlob blob =
          TraceBlob::TakeOwnership(std::move(buffer_), bytes_written_);
      RETURN_IF_ERROR(inner_->Parse(TraceBlobView(std::move(blob))));
    }

    // A compressed file may contain multiple concatenated streams/frames (valid
    // for gzip per RFC1952 §2.2, and for zstd). When one is fully decoded,
    // reset the decompressor to begin the next: all other state can be
    // preserved.
    if (result.ret == ResultCode::kEof) {
      decompressor_->Reset();
      output_state_ = kStreamBoundary;

      if (decompressor_->AvailIn() == 0) {
        return base::OkStatus();
      }
    }
  }
}

base::Status DecompressingTraceReader::OnPushDataToSorter() {
  if (output_state_ != kStreamBoundary ||
      (decompressor_ && decompressor_->AvailIn() > 0)) {
    return base::ErrStatus(
        "Compressed stream incomplete, trace is likely corrupt "
        "(ERR:tp-corrupt)");
  }
  PERFETTO_CHECK(!buffer_);
  return inner_ ? inner_->OnPushDataToSorter() : base::OkStatus();
}

void DecompressingTraceReader::OnEventsFullyExtracted() {
  if (inner_) {
    inner_->OnEventsFullyExtracted();
  }
}

namespace {

// Gzip-compressed trace container.
class GzipImporter : public TraceImporter<GzipImporter> {
 public:
  GzipImporter() : TraceImporter(MakeDescriptor()) {}
  ~GzipImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    static constexpr char kMagic[] = {'\x1f', '\x8b'};
    return size >= sizeof(kMagic) && memcmp(data, kMagic, sizeof(kMagic)) == 0;
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    if (!util::IsGzipSupported()) {
      return base::ErrStatus(
          "Cannot open compressed trace. zlib not enabled in the build config");
    }
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<DecompressingTraceReader>(
            context, util::CompressionType::kGzip));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "gzip";
    d.is_container = true;
    d.sort_policy = TraceSortPolicy::kNone;
    d.archive_priority = 1;
    d.forks_context = false;
    d.detection_priority = 60;
    return d;
  }
};

GzipImporter::~GzipImporter() = default;

// Zstd-compressed trace container.
class ZstdImporter : public TraceImporter<ZstdImporter> {
 public:
  ZstdImporter() : TraceImporter(MakeDescriptor()) {}
  ~ZstdImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    static constexpr char kMagic[] = {'\x28', '\xb5', '\x2f', '\xfd'};
    return size >= sizeof(kMagic) && memcmp(data, kMagic, sizeof(kMagic)) == 0;
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    if (!util::IsZstdSupported()) {
      return base::ErrStatus(
          "Cannot open compressed trace. zstd not enabled in the build config");
    }
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<DecompressingTraceReader>(
            context, util::CompressionType::kZstd));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "zstd";
    d.is_container = true;
    d.sort_policy = TraceSortPolicy::kNone;
    d.archive_priority = 1;
    d.forks_context = false;
    d.detection_priority = 65;
    return d;
  }
};

ZstdImporter::~ZstdImporter() = default;

// atrace -z compressed systrace (ctrace); a gzip stream behind "TRACE:\n".
class CtraceImporter : public TraceImporter<CtraceImporter> {
 public:
  CtraceImporter() : TraceImporter(MakeDescriptor()) {}
  ~CtraceImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    std::string start(reinterpret_cast<const char*>(data),
                      std::min<size_t>(size, kGuessTraceMaxLookahead));
    return base::Contains(start, "TRACE:\n\x78\x9c");
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    if (!util::IsGzipSupported()) {
      return base::ErrStatus(
          "Cannot open compressed trace. zlib not enabled in the build config");
    }
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<DecompressingTraceReader>(
            context, util::CompressionType::kGzip));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "ctrace";
    d.is_container = true;
    d.forks_context = false;
    d.detection_priority = 160;
    return d;
  }
};

CtraceImporter::~CtraceImporter() = default;

}  // namespace

std::unique_ptr<TraceImporterBase> CreateGzipImporter() {
  return std::make_unique<GzipImporter>();
}

std::unique_ptr<TraceImporterBase> CreateZstdImporter() {
  return std::make_unique<ZstdImporter>();
}

std::unique_ptr<TraceImporterBase> CreateCtraceImporter() {
  return std::make_unique<CtraceImporter>();
}

}  // namespace perfetto::trace_processor
