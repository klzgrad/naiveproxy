// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/library_loader/library_prefetcher.h"

#include <stddef.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"

namespace base {
namespace android {

namespace {

// Android defines the background priority to this value since at least 2009
// (see Process.java).
const int kBackgroundPriority = 10;
// Valid for all the Android architectures.
const size_t kPageSize = 4096;
const char* kLibchromeSuffix = "libchrome.so";
// "base.apk" is a suffix because the library may be loaded directly from the
// APK.
const char* kSuffixesToMatch[] = {kLibchromeSuffix, "base.apk"};

bool IsReadableAndPrivate(const base::debug::MappedMemoryRegion& region) {
  return region.permissions & base::debug::MappedMemoryRegion::READ &&
         region.permissions & base::debug::MappedMemoryRegion::PRIVATE;
}

bool PathMatchesSuffix(const std::string& path) {
  for (size_t i = 0; i < arraysize(kSuffixesToMatch); i++) {
    if (EndsWith(path, kSuffixesToMatch[i], CompareCase::SENSITIVE)) {
      return true;
    }
  }
  return false;
}

// For each range, reads a byte per page to force it into the page cache.
// Heap allocations, syscalls and library functions are not allowed in this
// function.
// Returns true for success.
#if defined(ADDRESS_SANITIZER)
// Disable AddressSanitizer instrumentation for this function. It is touching
// memory that hasn't been allocated by the app, though the addresses are
// valid. Furthermore, this takes place in a child process. See crbug.com/653372
// for the context.
__attribute__((no_sanitize_address))
#endif
bool Prefetch(const std::vector<std::pair<uintptr_t, uintptr_t>>& ranges) {
  for (const auto& range : ranges) {
    const uintptr_t page_mask = kPageSize - 1;
    // If start or end is not page-aligned, parsing went wrong. It is better to
    // exit with an error.
    if ((range.first & page_mask) || (range.second & page_mask)) {
      return false;  // CHECK() is not allowed here.
    }
    unsigned char* start_ptr = reinterpret_cast<unsigned char*>(range.first);
    unsigned char* end_ptr = reinterpret_cast<unsigned char*>(range.second);
    unsigned char dummy = 0;
    for (unsigned char* ptr = start_ptr; ptr < end_ptr; ptr += kPageSize) {
      // Volatile is required to prevent the compiler from eliminating this
      // loop.
      dummy ^= *static_cast<volatile unsigned char*>(ptr);
    }
  }
  return true;
}

}  // namespace

// static
bool NativeLibraryPrefetcher::IsGoodToPrefetch(
    const base::debug::MappedMemoryRegion& region) {
  return PathMatchesSuffix(region.path) &&
         IsReadableAndPrivate(region);  // .text and .data mappings are private.
}

// static
void NativeLibraryPrefetcher::FilterLibchromeRangesOnlyIfPossible(
    const std::vector<base::debug::MappedMemoryRegion>& regions,
    std::vector<AddressRange>* ranges) {
  bool has_libchrome_region = false;
  for (const base::debug::MappedMemoryRegion& region : regions) {
    if (EndsWith(region.path, kLibchromeSuffix, CompareCase::SENSITIVE)) {
      has_libchrome_region = true;
      break;
    }
  }
  for (const base::debug::MappedMemoryRegion& region : regions) {
    if (has_libchrome_region &&
        !EndsWith(region.path, kLibchromeSuffix, CompareCase::SENSITIVE)) {
      continue;
    }
    ranges->push_back(std::make_pair(region.start, region.end));
  }
}

// static
bool NativeLibraryPrefetcher::FindRanges(std::vector<AddressRange>* ranges) {
  std::string proc_maps;
  if (!base::debug::ReadProcMaps(&proc_maps))
    return false;
  std::vector<base::debug::MappedMemoryRegion> regions;
  if (!base::debug::ParseProcMaps(proc_maps, &regions))
    return false;

  std::vector<base::debug::MappedMemoryRegion> regions_to_prefetch;
  for (const auto& region : regions) {
    if (IsGoodToPrefetch(region)) {
      regions_to_prefetch.push_back(region);
    }
  }

  FilterLibchromeRangesOnlyIfPossible(regions_to_prefetch, ranges);
  return true;
}

// static
bool NativeLibraryPrefetcher::ForkAndPrefetchNativeLibrary() {
  // Avoid forking with cygprofile instrumentation because the latter performs
  // memory allocations.
#if defined(CYGPROFILE_INSTRUMENTATION)
  return false;
#endif

  // Looking for ranges is done before the fork, to avoid syscalls and/or memory
  // allocations in the forked process. The child process inherits the lock
  // state of its parent thread. It cannot rely on being able to acquire any
  // lock (unless special care is taken in a pre-fork handler), including being
  // able to call malloc().
  std::vector<AddressRange> ranges;
  if (!FindRanges(&ranges))
    return false;

  pid_t pid = fork();
  if (pid == 0) {
    setpriority(PRIO_PROCESS, 0, kBackgroundPriority);
    // _exit() doesn't call the atexit() handlers.
    _exit(Prefetch(ranges) ? 0 : 1);
  } else {
    if (pid < 0) {
      return false;
    }
    int status;
    const pid_t result = HANDLE_EINTR(waitpid(pid, &status, 0));
    if (result == pid) {
      if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0;
      }
    }
    return false;
  }
}

// static
int NativeLibraryPrefetcher::PercentageOfResidentCode(
    const std::vector<AddressRange>& ranges) {
  size_t total_pages = 0;
  size_t resident_pages = 0;
  const uintptr_t page_mask = kPageSize - 1;

  for (const auto& range : ranges) {
    if (range.first & page_mask || range.second & page_mask)
      return -1;
    size_t length = range.second - range.first;
    size_t pages = length / kPageSize;
    total_pages += pages;
    std::vector<unsigned char> is_page_resident(pages);
    int err = mincore(reinterpret_cast<void*>(range.first), length,
                      &is_page_resident[0]);
    DPCHECK(!err);
    if (err)
      return -1;
    resident_pages +=
        std::count_if(is_page_resident.begin(), is_page_resident.end(),
                      [](unsigned char x) { return x & 1; });
  }
  if (total_pages == 0)
    return -1;
  return static_cast<int>((100 * resident_pages) / total_pages);
}

// static
int NativeLibraryPrefetcher::PercentageOfResidentNativeLibraryCode() {
  std::vector<AddressRange> ranges;
  if (!FindRanges(&ranges))
    return -1;
  return PercentageOfResidentCode(ranges);
}

}  // namespace android
}  // namespace base
