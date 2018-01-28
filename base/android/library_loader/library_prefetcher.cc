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
#include <memory>
#include <utility>
#include <vector>

#include "base/android/library_loader/anchor_functions.h"
#include "base/bits.h"
#include "base/files/file.h"
#include "base/format_macros.h"
#include "base/macros.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
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

// Populate the per-page residency for |range| in |residency|. If successful,
// |residency| has the size of |range| in pages.
// Returns true for success.
bool MincoreOnRange(const NativeLibraryPrefetcher::AddressRange& range,
                    std::vector<unsigned char>* residency) {
  if (range.first % kPageSize || range.second % kPageSize)
    return false;
  size_t size = range.second - range.first;
  size_t size_in_pages = size / kPageSize;
  if (residency->size() != size_in_pages)
    residency->resize(size_in_pages);
  int err = HANDLE_EINTR(
      mincore(reinterpret_cast<void*>(range.first), size, &(*residency)[0]));
  PLOG_IF(ERROR, err) << "mincore() failed";
  return !err;
}

#if defined(ARCH_CPU_ARMEL)
// Returns the start and end of .text, aligned to the lower and upper page
// boundaries, respectively.
NativeLibraryPrefetcher::AddressRange GetTextRange() {
  // |kStartOftext| may not be at the beginning of a page, since .plt can be
  // before it, yet in the same mapping for instance.
  size_t start_page = kStartOfText - kStartOfText % kPageSize;
  // Set the end to the page on which the beginning of the last symbol is. The
  // actual symbol may spill into the next page by a few bytes, but this is
  // outside of the executable code range anyway.
  size_t end_page = base::bits::Align(kEndOfText, kPageSize);
  return {start_page, end_page};
}

// Timestamp in ns since Unix Epoch, and residency, as returned by mincore().
struct TimestampAndResidency {
  uint64_t timestamp_nanos;
  std::vector<unsigned char> residency;

  TimestampAndResidency(uint64_t timestamp_nanos,
                        std::vector<unsigned char>&& residency)
      : timestamp_nanos(timestamp_nanos), residency(residency) {}
};

// Returns true for success.
bool CollectResidency(const NativeLibraryPrefetcher::AddressRange& range,
                      std::vector<TimestampAndResidency>* data) {
  // Not using base::TimeTicks() to not call too many base:: symbol that would
  // pollute the reached symbols dumps.
  struct timespec ts;
  if (HANDLE_EINTR(clock_gettime(CLOCK_MONOTONIC, &ts))) {
    PLOG(ERROR) << "Cannot get the time.";
    return false;
  }
  uint64_t now =
      static_cast<uint64_t>(ts.tv_sec) * 1000 * 1000 * 1000 + ts.tv_nsec;
  std::vector<unsigned char> residency;
  if (!MincoreOnRange(range, &residency))
    return false;

  data->emplace_back(now, std::move(residency));
  return true;
}

void DumpResidency(const NativeLibraryPrefetcher::AddressRange& range,
                   std::unique_ptr<std::vector<TimestampAndResidency>> data) {
  auto path = base::FilePath(
      base::StringPrintf("/data/local/tmp/chrome/residency-%d.txt", getpid()));
  auto file =
      base::File(path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    PLOG(ERROR) << "Cannot open file to dump the residency data "
                << path.value();
    return;
  }

  // First line: start-end of text range.
  CheckOrderingSanity();
  CHECK_LT(range.first, kStartOfText);
  CHECK_LT(kEndOfText, range.second);
  auto start_end =
      base::StringPrintf("%" PRIuS " %" PRIuS "\n", kStartOfText - range.first,
                         kEndOfText - range.first);
  file.WriteAtCurrentPos(start_end.c_str(), start_end.size());

  for (const auto& data_point : *data) {
    auto timestamp =
        base::StringPrintf("%" PRIu64 " ", data_point.timestamp_nanos);
    file.WriteAtCurrentPos(timestamp.c_str(), timestamp.size());

    std::vector<char> dump;
    dump.reserve(data_point.residency.size() + 1);
    for (auto c : data_point.residency)
      dump.push_back(c ? '1' : '0');
    dump[dump.size() - 1] = '\n';
    file.WriteAtCurrentPos(&dump[0], dump.size());
  }
}
#endif  // defined(ARCH_CPU_ARMEL)
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
  // All code (including in the forked process) relies on this assumption.
  if (sysconf(_SC_PAGESIZE) != static_cast<long>(kPageSize))
    return false;

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

  for (const auto& range : ranges) {
    std::vector<unsigned char> residency;
    bool ok = MincoreOnRange(range, &residency);
    if (!ok)
      return -1;
    total_pages += residency.size();
    resident_pages += std::count_if(residency.begin(), residency.end(),
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

// static
void NativeLibraryPrefetcher::PeriodicallyCollectResidency() {
#if defined(ARCH_CPU_ARMEL)
  CHECK_EQ(static_cast<long>(kPageSize), sysconf(_SC_PAGESIZE));

  const auto& range = GetTextRange();
  auto data = std::make_unique<std::vector<TimestampAndResidency>>();
  for (int i = 0; i < 60; ++i) {
    if (!CollectResidency(range, data.get()))
      return;
    usleep(2e5);
  }
  DumpResidency(range, std::move(data));
#else
  CHECK(false) << "Only supported on ARM";
#endif
}

// static
void NativeLibraryPrefetcher::MadviseRandomText() {
#if defined(ARCH_CPU_ARMEL)
  CheckOrderingSanity();
  const auto& range = GetTextRange();
  size_t size = range.second - range.first;
  int err = madvise(reinterpret_cast<void*>(range.first), size, MADV_RANDOM);
  if (err) {
    PLOG(ERROR) << "madvise() failed";
  }
#else
  CHECK(false) << "Only supported on ARM.";
#endif  // defined(ARCH_CPU_ARMEL)
}

}  // namespace android
}  // namespace base
