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

#include "src/trace_processor/util/zstd_compressor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
#include <zstd.h>
#endif

namespace perfetto::trace_processor::util {

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

// static
std::unique_ptr<uint8_t, base::FreeDeleter> ZstdCompressor::CompressFully(
    const uint8_t* data,
    size_t len,
    size_t* out_size,
    int level) {
  size_t bound = ZSTD_compressBound(len);
  std::unique_ptr<uint8_t, base::FreeDeleter> out(
      static_cast<uint8_t*>(malloc(bound)));
  // ZSTD_compress allocates and frees a ZSTD_CCtx internally on every call.
  // That is fine for the handful of blobs this is used on; if it is ever placed
  // on a hot per-row path, reuse a ZSTD_compressCCtx with a (thread_local)
  // context to avoid the per-call allocation.
  size_t n = ZSTD_compress(out.get(), bound, data, len, level);
  if (ZSTD_isError(n)) {
    return nullptr;
  }
  *out_size = n;
  return out;
}

#else  // !PERFETTO_ZSTD

// static
std::unique_ptr<uint8_t, base::FreeDeleter>
ZstdCompressor::CompressFully(const uint8_t*, size_t, size_t*, int) {
  return nullptr;
}

#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

}  // namespace perfetto::trace_processor::util
