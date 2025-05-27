// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/test_file_util.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/logging.h"

namespace base {

bool EvictFileFromSystemCache(const FilePath& file) {
  // There aren't any really direct ways to purge a file from the UBC.  From
  // talking with Amit Singh, the safest is to mmap the file with MAP_FILE (the
  // default) + MAP_SHARED, then do an msync to invalidate the memory.  The next
  // open should then have to load the file from disk.

  std::optional<int64_t> length = GetFileSize(file);
  if (!length.has_value()) {
    DLOG(ERROR) << "failed to get size of " << file.value();
    return false;
  }

  // When a file is empty, we do not need to evict it from cache.
  // In fact, an attempt to map it to memory will result in error.
  if (length.value() == 0) {
    DLOG(WARNING) << "file size is zero, will not attempt to map to memory";
    return true;
  }

  MemoryMappedFile mapped_file;
  if (!mapped_file.Initialize(file)) {
    DLOG(WARNING) << "failed to memory map " << file.value();
    return false;
  }

  if (msync(const_cast<uint8_t*>(mapped_file.data()), mapped_file.length(),
            MS_INVALIDATE) != 0) {
    DPLOG(WARNING) << "failed to invalidate memory map of " << file.value();
    return false;
  }

  return true;
}

}  // namespace base
