/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "perfetto/protozero/scattered_stream_writer.h"

#include <algorithm>

#include "perfetto/base/logging.h"

namespace protozero {

ScatteredStreamWriter::Delegate::~Delegate() {}

uint8_t* ScatteredStreamWriter::Delegate::AnnotatePatch(uint8_t* patch_addr) {
  // In most cases, a patch is transparent. The caller can write directly into
  // `to_patch`, because its memory is not going away. TraceWriterImpl, however,
  // requires a more complicated logic, because the chunks might be copied
  // earlier.
  return patch_addr;
}

ScatteredStreamWriter::ScatteredStreamWriter(Delegate* delegate)
    : delegate_(delegate),
      cur_range_({nullptr, nullptr}),
      write_ptr_(nullptr) {}

ScatteredStreamWriter::~ScatteredStreamWriter() {}

void ScatteredStreamWriter::Reset(ContiguousMemoryRange range) {
  written_previously_ += static_cast<uint64_t>(write_ptr_ - cur_range_.begin);
  cur_range_ = range;
  write_ptr_ = range.begin;
  PERFETTO_DCHECK(!write_ptr_ || write_ptr_ < cur_range_.end);
}

void ScatteredStreamWriter::Extend() {
  Reset(delegate_->GetNewBuffer());
}

void ScatteredStreamWriter::WriteBytesSlowPath(const uint8_t* src,
                                               size_t size) {
  size_t bytes_left = size;
  while (bytes_left > 0) {
    if (write_ptr_ >= cur_range_.end)
      Extend();
    const size_t burst_size = std::min(bytes_available(), bytes_left);
    WriteBytesUnsafe(src, burst_size);
    bytes_left -= burst_size;
    src += burst_size;
  }
}

// TODO(primiano): perf optimization: I suspect that at the end this will always
// be called with |size| == 4, in which case we might just hardcode it.
uint8_t* ScatteredStreamWriter::ReserveBytes(size_t size) {
  PERFETTO_DCHECK(write_ptr_ <= cur_range_.end);
  if (size > static_cast<size_t>(cur_range_.end - write_ptr_)) {
    // Assume the reservations are always < Delegate::GetNewBuffer().size(),
    // so that one single call to Extend() will definitely give enough headroom.
    Extend();
    PERFETTO_DCHECK(write_ptr_ <= cur_range_.end);
    PERFETTO_DCHECK(size <= static_cast<size_t>(cur_range_.end - write_ptr_));
  }
  uint8_t* begin = write_ptr_;
  write_ptr_ += size;
#if PERFETTO_DCHECK_IS_ON()
  // In the past, the service had a matching DCHECK in
  // TraceBuffer::TryPatchChunkContents, which was assuming that service and all
  // producers are built with matching DCHECK levels. This turned out to be a
  // source of problems and was removed in b/197340286. This memset is useless
  // these days and is here only to maintain ABI compatibility between producers
  // that use a v20+ SDK and older versions of the service that were built in
  // debug mode. At some point around 2023 it should be safe to remove it.
  // (running a debug version of traced in production seems a bad idea
  // regardless).
  memset(begin, 0, size);
#endif
  return begin;
}

}  // namespace protozero
