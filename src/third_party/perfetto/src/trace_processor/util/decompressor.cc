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

#include "src/trace_processor/util/decompressor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "perfetto/base/logging.h"
#include "src/trace_processor/util/gzip_decompressor.h"
#include "src/trace_processor/util/zstd_decompressor.h"

namespace perfetto::trace_processor::util {

Decompressor::~Decompressor() = default;

bool IsCompressionSupported(CompressionType type) {
  switch (type) {
    case CompressionType::kNone:
      return false;
    case CompressionType::kGzip:
      return IsGzipSupported();
    case CompressionType::kZstd:
      return IsZstdSupported();
  }
  return false;
}

CompressionCodecInfo GetCompressionCodecInfo(CompressionType type) {
  switch (type) {
    case CompressionType::kGzip:
      return {"gzip", "enable_perfetto_zlib"};
    case CompressionType::kZstd:
      return {"zstd", "enable_perfetto_zstd"};
    case CompressionType::kNone:
      break;
  }
  PERFETTO_FATAL("kNone has no codec");
}

std::unique_ptr<Decompressor> CreateDecompressor(CompressionType type) {
  switch (type) {
    case CompressionType::kNone:
      return nullptr;
    case CompressionType::kGzip:
      if (!IsGzipSupported()) {
        return nullptr;
      }
      return std::make_unique<GzipDecompressor>(
          GzipDecompressor::InputMode::kGzip);
    case CompressionType::kZstd:
      if (!IsZstdSupported()) {
        return nullptr;
      }
      return std::make_unique<ZstdDecompressor>();
  }
  return nullptr;
}

std::optional<DecompressedBuffer> DecompressToBuffer(Decompressor& decompressor,
                                                     const uint8_t* data,
                                                     size_t len,
                                                     FrameMode frame_mode) {
  using ResultCode = Decompressor::ResultCode;
  // Decompress into one owned buffer sized at 4x the input (the usual ratio) to
  // avoid a regrow, doubling it if that's not enough. Guard the *4 against
  // size_t overflow on 32-bit/Wasm; the grow loop covers any under-estimate.
  decompressor.Feed(data, len);
  size_t capacity = std::max<size_t>(len > SIZE_MAX / 4 ? len : len * 4, 4096);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[capacity]);
  size_t size = 0;
  for (;;) {
    if (size == capacity) {
      capacity *= 2;
      std::unique_ptr<uint8_t[]> grown(new uint8_t[capacity]);
      memcpy(grown.get(), buffer.get(), size);
      buffer = std::move(grown);
    }
    Decompressor::Result r =
        decompressor.ExtractOutput(buffer.get() + size, capacity - size);
    if (r.ret != ResultCode::kError) {
      size += r.bytes_written;
    }
    if (r.ret == ResultCode::kOk) {
      continue;
    }
    if (r.ret == ResultCode::kEof && decompressor.AvailIn() == 0) {
      break;  // Clean end of the last frame.
    }
    // kEof with input left is a concatenated same-codec frame (pzstd,
    // multi-member gzip); decode it too in kAllFrames mode. Anything else (a
    // different codec, corrupt, or truncated input) yields no usable data.
    if (frame_mode == FrameMode::kAllFrames && r.ret == ResultCode::kEof) {
      decompressor.Reset();
      continue;
    }
    return std::nullopt;
  }
  return DecompressedBuffer{std::move(buffer), size};
}

std::optional<DecompressedBuffer> DecompressToBuffer(CompressionType type,
                                                     const uint8_t* data,
                                                     size_t len) {
  std::unique_ptr<Decompressor> decompressor = CreateDecompressor(type);
  if (!decompressor) {
    return std::nullopt;
  }
  return DecompressToBuffer(*decompressor, data, len, FrameMode::kAllFrames);
}

}  // namespace perfetto::trace_processor::util
