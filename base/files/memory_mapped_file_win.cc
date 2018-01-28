// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/memory_mapped_file.h"

#include <stddef.h>
#include <stdint.h>

#include <limits>

#include "base/files/file_path.h"
#include "base/strings/string16.h"
#include "base/threading/thread_restrictions.h"

namespace base {

MemoryMappedFile::MemoryMappedFile() : data_(NULL), length_(0) {
}

bool MemoryMappedFile::MapFileRegionToMemory(
    const MemoryMappedFile::Region& region,
    Access access) {
  ThreadRestrictions::AssertIOAllowed();

  if (!file_.IsValid())
    return false;

  int flags = 0;
  uint32_t size_low = 0;
  uint32_t size_high = 0;
  switch (access) {
    case READ_ONLY:
      flags |= PAGE_READONLY;
      break;
    case READ_WRITE:
      flags |= PAGE_READWRITE;
      break;
    case READ_WRITE_EXTEND:
      flags |= PAGE_READWRITE;
      size_high = static_cast<uint32_t>(region.size >> 32);
      size_low = static_cast<uint32_t>(region.size & 0xFFFFFFFF);
      break;
  }

  file_mapping_.Set(::CreateFileMapping(file_.GetPlatformFile(), NULL, flags,
                                        size_high, size_low, NULL));
  if (!file_mapping_.IsValid())
    return false;

  LARGE_INTEGER map_start = {};
  SIZE_T map_size = 0;
  int32_t data_offset = 0;

  if (region == MemoryMappedFile::Region::kWholeFile) {
    DCHECK_NE(READ_WRITE_EXTEND, access);
    int64_t file_len = file_.GetLength();
    if (file_len <= 0 || file_len > std::numeric_limits<int32_t>::max())
      return false;
    length_ = static_cast<size_t>(file_len);
  } else {
    // The region can be arbitrarily aligned. MapViewOfFile, instead, requires
    // that the start address is aligned to the VM granularity (which is
    // typically larger than a page size, for instance 32k).
    // Also, conversely to POSIX's mmap, the |map_size| doesn't have to be
    // aligned and must be less than or equal the mapped file size.
    // We map here the outer region [|aligned_start|, |aligned_start+size|]
    // which contains |region| and then add up the |data_offset| displacement.
    int64_t aligned_start = 0;
    int64_t ignored = 0;
    CalculateVMAlignedBoundaries(
        region.offset, region.size, &aligned_start, &ignored, &data_offset);
    int64_t size = region.size + data_offset;

    // Ensure that the casts below in the MapViewOfFile call are sane.
    if (aligned_start < 0 || size < 0 ||
        static_cast<uint64_t>(size) > std::numeric_limits<SIZE_T>::max()) {
      DLOG(ERROR) << "Region bounds are not valid for MapViewOfFile";
      return false;
    }
    map_start.QuadPart = aligned_start;
    map_size = static_cast<SIZE_T>(size);
    length_ = static_cast<size_t>(region.size);
  }

  data_ = static_cast<uint8_t*>(
      ::MapViewOfFile(file_mapping_.Get(),
                      (flags & PAGE_READONLY) ? FILE_MAP_READ : FILE_MAP_WRITE,
                      map_start.HighPart, map_start.LowPart, map_size));
  if (data_ == NULL)
    return false;
  data_ += data_offset;
  return true;
}

void MemoryMappedFile::CloseHandles() {
  if (data_)
    ::UnmapViewOfFile(data_);
  if (file_mapping_.IsValid())
    file_mapping_.Close();
  if (file_.IsValid())
    file_.Close();

  data_ = NULL;
  length_ = 0;
}

}  // namespace base
