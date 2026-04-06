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

#include "src/trace_processor/read_trace_internal.h"

#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {
namespace {

// 1MB chunk size seems the best tradeoff on a MacBook Pro 2013 - i7 2.8 GHz.
constexpr size_t kChunkSize = 1024 * 1024;

base::Status ReadTraceUsingRead(
    TraceProcessor* tp,
    int fd,
    uint64_t* file_size,
    const std::function<void(uint64_t parsed_size)>& progress_callback) {
  // Load the trace in chunks using ordinary read().
  for (int i = 0;; i++) {
    if (progress_callback && i % 128 == 0)
      progress_callback(*file_size);

    TraceBlob blob = TraceBlob::Allocate(kChunkSize);
    auto rsize = base::Read(fd, blob.data(), blob.size());
    if (rsize == 0)
      break;

    if (rsize < 0) {
      return base::ErrStatus("Reading trace file failed (errno: %d, %s)", errno,
                             strerror(errno));
    }

    *file_size += static_cast<uint64_t>(rsize);
    TraceBlobView blob_view(std::move(blob), 0, static_cast<size_t>(rsize));
    RETURN_IF_ERROR(tp->Parse(std::move(blob_view)));
  }
  return base::OkStatus();
}
}  // namespace

base::Status ReadTraceUnfinalized(
    TraceProcessor* tp,
    const char* filename,
    const std::function<void(uint64_t parsed_size)>& progress_callback) {
  uint64_t bytes_read = 0;

#if PERFETTO_HAS_MMAP()
  char* no_mmap = getenv("TRACE_PROCESSOR_NO_MMAP");
  bool use_mmap = !no_mmap || *no_mmap != '1';

  if (use_mmap) {
    base::ScopedMmap mapped = base::ReadMmapWholeFile(filename);
    if (mapped.IsValid()) {
      size_t length = mapped.length();
      TraceBlobView whole_mmap(TraceBlob::FromMmap(std::move(mapped)));
      // Parse the file in chunks so we get some status update on stdio.
      static constexpr size_t kMmapChunkSize = 128ul * 1024 * 1024;
      while (bytes_read < length) {
        progress_callback(bytes_read);
        const size_t bytes_read_z = static_cast<size_t>(bytes_read);
        size_t slice_size = std::min(length - bytes_read_z, kMmapChunkSize);
        TraceBlobView slice = whole_mmap.slice_off(bytes_read_z, slice_size);
        RETURN_IF_ERROR(tp->Parse(std::move(slice)));
        bytes_read += slice_size;
      }  // while (slices)
    }  // if (mapped.IsValid())
  }  // if (use_mmap)
  if (bytes_read == 0)
    PERFETTO_LOG("Cannot use mmap on this system. Falling back on read()");
#endif  // PERFETTO_HAS_MMAP()
  if (bytes_read == 0) {
    base::ScopedFile fd(base::OpenFile(filename, O_RDONLY));
    if (!fd)
      return base::ErrStatus("Could not open trace file (path: %s)", filename);
    RETURN_IF_ERROR(
        ReadTraceUsingRead(tp, *fd, &bytes_read, progress_callback));
  }
  tp->SetCurrentTraceName(filename);

  if (progress_callback)
    progress_callback(bytes_read);
  return base::OkStatus();
}
}  // namespace perfetto::trace_processor
