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

#ifndef SRC_TRACE_PROCESSOR_UTIL_DECOMPRESSOR_H_
#define SRC_TRACE_PROCESSOR_UTIL_DECOMPRESSOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "perfetto/base/build_config.h"

namespace perfetto::trace_processor::util {

// The compression codecs trace_processor can decompress. To add one, subclass
// Decompressor, add a case to CreateDecompressor() and, if the codec can arrive
// as a whole compressed file, a magic in trace_type.cc's
// SniffCompressedTraceType() and a matching importer.
enum class CompressionType : uint8_t {
  // Not compressed, or a header we don't recognize.
  kNone,
  // gzip-framed deflate (e.g. a .gz file).
  kGzip,
  // A zstd frame.
  kZstd,
};

// Whether the current build flags include the library each codec needs.
// CreateDecompressor() returns nullptr for unsupported codecs.
constexpr bool IsGzipSupported() {
#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
  return true;
#else
  return false;
#endif
}

constexpr bool IsZstdSupported() {
#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
  return true;
#else
  return false;
#endif
}

// An owned block of decompressed bytes. The allocation may be larger than
// `size`, so only `size` bytes are valid. Handed straight to
// TraceBlob::TakeOwnership on hot paths to avoid a copy.
struct DecompressedBuffer {
  std::unique_ptr<uint8_t[]> data;
  size_t size = 0;
};

// Whether DecompressToBuffer decodes one frame or a whole concatenated stream.
enum class FrameMode {
  // Decode exactly one frame; any input past it is treated as corrupt.
  kSingleFrame,
  // Decode every concatenated same-codec frame to the end (pzstd,
  // multi-member gzip).
  kAllFrames,
};

// Codec-agnostic streaming decompressor. Concrete codecs (gzip, zstd, ...)
// subclass this; obtain one via CreateDecompressor() so call sites never branch
// on the codec.
//
// For a whole in-memory block, use DecompressToBuffer(). To stream, Feed() then
// ExtractOutput() repeatedly until it returns kEof or kNeedsMoreInput (see
// ResultCode).
class Decompressor {
 public:
  enum class ResultCode {
    // Made progress; keep calling ExtractOutput to drain more output.
    kOk,
    // The stream is fully decompressed; no more input is needed.
    kEof,
    // Corrupt/invalid input.
    kError,
    // All available input was consumed but the stream is not complete; feed the
    // next mem-block and continue.
    kNeedsMoreInput,
  };
  struct Result {
    ResultCode ret;
    // Bytes written to output. Valid in all cases except |ResultCode::kError|.
    size_t bytes_written;
  };

  Decompressor() = default;
  virtual ~Decompressor();

  // Hands out / holds internal pointers; never copy or move.
  Decompressor(const Decompressor&) = delete;
  Decompressor& operator=(const Decompressor&) = delete;
  Decompressor(Decompressor&&) = delete;
  Decompressor& operator=(Decompressor&&) = delete;

  // Feed the next input mem-block.
  virtual void Feed(const uint8_t* data, size_t size) = 0;

  // Extract the newly available partial output. After each Feed(), call this
  // repeatedly until it returns kEof or kNeedsMoreInput.
  virtual Result ExtractOutput(uint8_t* out, size_t out_capacity) = 0;

  // Reset to decode the next stream/frame, reusing internal buffers. Any fed
  // but unconsumed input is preserved (so multi-stream inputs can continue).
  virtual void Reset() = 0;

  // The amount of input bytes left unprocessed.
  virtual size_t AvailIn() const = 0;
};

// Creates a decompressor for `type`, or nullptr if `type` is kNone or the build
// wasn't compiled with support for it.
std::unique_ptr<Decompressor> CreateDecompressor(CompressionType type);

// Whether CreateDecompressor(`type`) would succeed, i.e. this build carries the
// codec's library. False for kNone.
bool IsCompressionSupported(CompressionType type);

// The codec's display name ("gzip"/"zstd") and the GN arg that enables it, for
// "not compiled in; rebuild with X=true" errors. Not valid for kNone.
struct CompressionCodecInfo {
  const char* name;
  const char* gn_arg;
};
CompressionCodecInfo GetCompressionCodecInfo(CompressionType type);

// Decompresses an entire in-memory block into one owned heap buffer, returned
// for zero-copy handoff (e.g. TraceBlob::TakeOwnership). Returns nullopt if
// `type` is unsupported or the input is corrupt or truncated. Concatenated
// same-codec frames are decoded to the end.
std::optional<DecompressedBuffer> DecompressToBuffer(CompressionType type,
                                                     const uint8_t* data,
                                                     size_t len);

// As above, but drives an existing `decompressor` (which must be freshly
// created or Reset()) so a caller can reuse one across calls. See FrameMode for
// how input past the first frame is handled.
std::optional<DecompressedBuffer> DecompressToBuffer(Decompressor& decompressor,
                                                     const uint8_t* data,
                                                     size_t len,
                                                     FrameMode frame_mode);

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_DECOMPRESSOR_H_
