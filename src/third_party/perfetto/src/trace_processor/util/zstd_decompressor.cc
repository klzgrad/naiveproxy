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

#include "src/trace_processor/util/zstd_decompressor.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
#include <zstd.h>
#else
struct ZSTD_DCtx_s {};
#endif

namespace perfetto::trace_processor::util {

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

ZstdDecompressor::ZstdDecompressor() : dstream_(ZSTD_createDStream()) {
  PERFETTO_CHECK(dstream_);
  PERFETTO_CHECK(!ZSTD_isError(ZSTD_initDStream(dstream_.get())));
}

void ZstdDecompressor::Reset() {
  // Only reset the session: keep the (possibly partially consumed) input so a
  // multi-frame stream can continue decoding the next frame in place.
  ZSTD_DCtx_reset(dstream_.get(), ZSTD_reset_session_only);
  eof_ = false;
}

void ZstdDecompressor::Feed(const uint8_t* data, size_t size) {
  in_data_ = data;
  in_size_ = size;
  in_pos_ = 0;
  // Clear any prior EOF so a fresh stream fed after a completed one decodes.
  // Reset() clears it too, for the multi-frame-in-place case.
  eof_ = false;
}

ZstdDecompressor::Result ZstdDecompressor::ExtractOutput(uint8_t* out,
                                                         size_t out_capacity) {
  if (eof_)
    return Result{ResultCode::kEof, 0};

  ZSTD_outBuffer out_buf = {out, out_capacity, 0};
  ZSTD_inBuffer in_buf = {in_data_, in_size_, in_pos_};
  const size_t prev_in_pos = in_pos_;
  // Note: we intentionally do not early-return when the input is exhausted.
  // zstd can hold a decompressed block in an internal buffer when the caller's
  // output buffer is smaller than the block, and that data must be flushed with
  // further calls even with no new input.
  size_t ret = ZSTD_decompressStream(dstream_.get(), &out_buf, &in_buf);
  in_pos_ = in_buf.pos;

  if (ZSTD_isError(ret))
    return Result{ResultCode::kError, 0};
  if (ret == 0) {
    eof_ = true;
    return Result{ResultCode::kEof, out_buf.pos};
  }
  // ret > 0: the frame is not complete yet.
  if (out_buf.pos > 0)
    return Result{ResultCode::kOk,
                  out_buf.pos};  // Produced output; keep going.
  // No output this call: keep going only if we made input progress, otherwise
  // we genuinely need more input. This keeps the invariant that
  // kNeedsMoreInput always reports zero bytes written.
  if (in_pos_ > prev_in_pos)
    return Result{ResultCode::kOk, 0};
  return Result{ResultCode::kNeedsMoreInput, 0};
}

size_t ZstdDecompressor::AvailIn() const {
  return in_size_ - in_pos_;
}

void ZstdDecompressor::Deleter::operator()(ZSTD_DCtx_s* stream) const {
  ZSTD_freeDStream(stream);
}

#else  // !PERFETTO_ZSTD

ZstdDecompressor::ZstdDecompressor() = default;
void ZstdDecompressor::Reset() {}
void ZstdDecompressor::Feed(const uint8_t*, size_t) {}
ZstdDecompressor::Result ZstdDecompressor::ExtractOutput(uint8_t*, size_t) {
  return Result{ResultCode::kError, 0};
}
size_t ZstdDecompressor::AvailIn() const {
  return 0;
}
void ZstdDecompressor::Deleter::operator()(ZSTD_DCtx_s*) const {}

#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

}  // namespace perfetto::trace_processor::util
