// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/memory_mapped_file.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"

#if defined(OS_ANDROID)
#include <android/api-level.h>
#endif

namespace base {

MemoryMappedFile::MemoryMappedFile() : data_(NULL), length_(0) {
}

#if !defined(OS_NACL)
bool MemoryMappedFile::MapFileRegionToMemory(
    const MemoryMappedFile::Region& region,
    Access access) {
  ThreadRestrictions::AssertIOAllowed();

  off_t map_start = 0;
  size_t map_size = 0;
  int32_t data_offset = 0;

  if (region == MemoryMappedFile::Region::kWholeFile) {
    int64_t file_len = file_.GetLength();
    if (file_len < 0) {
      DPLOG(ERROR) << "fstat " << file_.GetPlatformFile();
      return false;
    }
    map_size = static_cast<size_t>(file_len);
    length_ = map_size;
  } else {
    // The region can be arbitrarily aligned. mmap, instead, requires both the
    // start and size to be page-aligned. Hence, we map here the page-aligned
    // outer region [|aligned_start|, |aligned_start| + |size|] which contains
    // |region| and then add up the |data_offset| displacement.
    int64_t aligned_start = 0;
    int64_t aligned_size = 0;
    CalculateVMAlignedBoundaries(region.offset,
                                 region.size,
                                 &aligned_start,
                                 &aligned_size,
                                 &data_offset);

    // Ensure that the casts in the mmap call below are sane.
    if (aligned_start < 0 || aligned_size < 0 ||
        aligned_start > std::numeric_limits<off_t>::max() ||
        static_cast<uint64_t>(aligned_size) >
            std::numeric_limits<size_t>::max() ||
        static_cast<uint64_t>(region.size) >
            std::numeric_limits<size_t>::max()) {
      DLOG(ERROR) << "Region bounds are not valid for mmap";
      return false;
    }

    map_start = static_cast<off_t>(aligned_start);
    map_size = static_cast<size_t>(aligned_size);
    length_ = static_cast<size_t>(region.size);
  }

  int flags = 0;
  switch (access) {
    case READ_ONLY:
      flags |= PROT_READ;
      break;

    case READ_WRITE:
      flags |= PROT_READ | PROT_WRITE;
      break;

    case READ_WRITE_EXTEND:
      flags |= PROT_READ | PROT_WRITE;

      const int64_t new_file_len = region.offset + region.size;

      // POSIX won't auto-extend the file when it is written so it must first
      // be explicitly extended to the maximum size. Zeros will fill the new
      // space. It is assumed that the existing file is fully realized as
      // otherwise the entire file would have to be read and possibly written.
      const int64_t original_file_len = file_.GetLength();
      if (original_file_len < 0) {
        DPLOG(ERROR) << "fstat " << file_.GetPlatformFile();
        return false;
      }

      // Increase the actual length of the file, if necessary. This can fail if
      // the disk is full and the OS doesn't support sparse files.
      if (!file_.SetLength(std::max(original_file_len, new_file_len))) {
        DPLOG(ERROR) << "ftruncate " << file_.GetPlatformFile();
        return false;
      }

      // Realize the extent of the file so that it can't fail (and crash) later
      // when trying to write to a memory page that can't be created. This can
      // fail if the disk is full and the file is sparse.
      //
      // Only Android API>=21 supports the fallocate call. Older versions need
      // to manually extend the file by writing zeros at block intervals.
      //
      // Mac OSX doesn't support this call but the primary filesystem doesn't
      // support sparse files so is unneeded.
      bool do_manual_extension = false;

#if defined(OS_ANDROID) && __ANDROID_API__ < 21
      do_manual_extension = true;
#elif !defined(OS_MACOSX)
      if (posix_fallocate(file_.GetPlatformFile(), region.offset,
                          region.size) != 0) {
        DPLOG(ERROR) << "posix_fallocate " << file_.GetPlatformFile();
        // This can fail because the filesystem doesn't support it so don't
        // give up just yet. Try the manual method below.
        do_manual_extension = true;
      }
#endif

      // Manually realize the extended file by writing bytes to it at intervals.
      if (do_manual_extension) {
        int64_t block_size = 512;  // Start with something safe.
        struct stat statbuf;
        if (fstat(file_.GetPlatformFile(), &statbuf) == 0 &&
            statbuf.st_blksize > 0) {
          block_size = statbuf.st_blksize;
        }

        // Write starting at the next block boundary after the old file length.
        const int64_t extension_start =
            (original_file_len + block_size - 1) & ~(block_size - 1);
        for (int64_t i = extension_start; i < new_file_len; i += block_size) {
          char existing_byte;
          if (pread(file_.GetPlatformFile(), &existing_byte, 1, i) != 1)
            return false;  // Can't read? Not viable.
          if (existing_byte != 0)
            continue;  // Block has data so must already exist.
          if (pwrite(file_.GetPlatformFile(), &existing_byte, 1, i) != 1)
            return false;  // Can't write? Not viable.
        }
      }

      break;
  }

  data_ = static_cast<uint8_t*>(mmap(NULL, map_size, flags, MAP_SHARED,
                                     file_.GetPlatformFile(), map_start));
  if (data_ == MAP_FAILED) {
    DPLOG(ERROR) << "mmap " << file_.GetPlatformFile();
    return false;
  }

  data_ += data_offset;
  return true;
}
#endif

void MemoryMappedFile::CloseHandles() {
  ThreadRestrictions::AssertIOAllowed();

  if (data_ != NULL)
    munmap(data_, length_);
  file_.Close();

  data_ = NULL;
  length_ = 0;
}

}  // namespace base
