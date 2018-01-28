// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_LIBRARY_LOADER_LIBRARY_PREFETCHER_H_
#define BASE_ANDROID_LIBRARY_LOADER_LIBRARY_PREFETCHER_H_

#include <jni.h>

#include <stdint.h>
#include <string>

#include "base/debug/proc_maps_linux.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"

namespace base {
namespace android {

// Forks and waits for a process prefetching the native library. This is done in
// a forked process for the following reasons:
// - Isolating the main process from mistakes in the parsing. If the parsing
//   returns an incorrect address, only the forked process will crash.
// - Not inflating the memory used by the main process uselessly, which could
//   increase its likelihood to be killed.
// The forked process has background priority and, since it is not declared to
// the Android runtime, can be killed at any time, which is not an issue here.
class BASE_EXPORT NativeLibraryPrefetcher {
 public:
  // Finds the ranges matching the native library, forks a low priority
  // process pre-fetching these ranges and wait()s for it.
  // Returns true for success.
  static bool ForkAndPrefetchNativeLibrary();
  // Returns the percentage of the native library code currently resident in
  // memory, or -1 in case of error.
  static int PercentageOfResidentNativeLibraryCode();

 private:
  using AddressRange = std::pair<uintptr_t, uintptr_t>;
  // Returns true if the region matches native code or data.
  static bool IsGoodToPrefetch(const base::debug::MappedMemoryRegion& region);
  // Filters the regions to keep only libchrome ranges if possible.
  static void FilterLibchromeRangesOnlyIfPossible(
      const std::vector<base::debug::MappedMemoryRegion>& regions,
      std::vector<AddressRange>* ranges);
  // Finds the ranges matching the native library in /proc/self/maps.
  // Returns true for success.
  static bool FindRanges(std::vector<AddressRange>* ranges);

  // Returns the percentage of the given address ranges currently resident in
  // memory, or -1 in case of error.
  static int PercentageOfResidentCode(const std::vector<AddressRange>& ranges);

  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestIsGoodToPrefetchNoRange);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestIsGoodToPrefetchUnreadableRange);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestIsGoodToPrefetchSkipSharedRange);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestIsGoodToPrefetchLibchromeRange);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestIsGoodToPrefetchBaseApkRange);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestFilterLibchromeRangesOnlyIfPossibleNoLibchrome);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestFilterLibchromeRangesOnlyIfPossibleHasLibchrome);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestPercentageOfResidentCode);
  FRIEND_TEST_ALL_PREFIXES(NativeLibraryPrefetcherTest,
                           TestPercentageOfResidentCodeTwoRegions);

  DISALLOW_IMPLICIT_CONSTRUCTORS(NativeLibraryPrefetcher);
};

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_LIBRARY_LOADER_LIBRARY_PREFETCHER_H_
