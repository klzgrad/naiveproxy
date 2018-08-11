// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_THREAD_HEAP_USAGE_TRACKER_H_
#define BASE_DEBUG_THREAD_HEAP_USAGE_TRACKER_H_

#include <stdint.h>

#include "base/allocator/buildflags.h"
#include "base/base_export.h"
#include "base/threading/thread_checker.h"

namespace base {
namespace allocator {
struct AllocatorDispatch;
}  // namespace allocator

namespace debug {

// Used to store the heap allocator usage in a scope.
struct ThreadHeapUsage {
  // The cumulative number of allocation operations.
  uint64_t alloc_ops;

  // The cumulative number of allocated bytes. Where available, this is
  // inclusive heap padding and estimated or actual heap overhead.
  uint64_t alloc_bytes;

  // Where available, cumulative number of heap padding and overhead bytes.
  uint64_t alloc_overhead_bytes;

  // The cumulative number of free operations.
  uint64_t free_ops;

  // The cumulative number of bytes freed.
  // Only recorded if the underlying heap shim can return the size of an
  // allocation.
  uint64_t free_bytes;

  // The maximal value of |alloc_bytes| - |free_bytes| seen for this thread.
  // Only recorded if the underlying heap shim supports returning the size of
  // an allocation.
  uint64_t max_allocated_bytes;
};

// By keeping a tally on heap operations, it's possible to track:
// - the number of alloc/free operations, where a realloc is zero or one
//   of each, depending on the input parameters (see man realloc).
// - the number of bytes allocated/freed.
// - the number of estimated bytes of heap overhead used.
// - the high-watermark amount of bytes allocated in the scope.
// This in turn allows measuring the memory usage and memory usage churn over
// a scope. Scopes must be cleanly nested, and each scope must be
// destroyed on the thread where it's created.
//
// Note that this depends on the capabilities of the underlying heap shim. If
// that shim can not yield a size estimate for an allocation, it's not possible
// to keep track of overhead, freed bytes and the allocation high water mark.
class BASE_EXPORT ThreadHeapUsageTracker {
 public:
  ThreadHeapUsageTracker();
  ~ThreadHeapUsageTracker();

  // Start tracking heap usage on this thread.
  // This may only be called on the thread where the instance is created.
  // Note IsHeapTrackingEnabled() must be true.
  void Start();

  // Stop tracking heap usage on this thread and store the usage tallied.
  // If |usage_is_exclusive| is true, the usage tallied won't be added to the
  // outer scope's usage. If |usage_is_exclusive| is false, the usage tallied
  // in this scope will also tally to any outer scope.
  // This may only be called on the thread where the instance is created.
  void Stop(bool usage_is_exclusive);

  // After Stop() returns the usage tallied from Start() to Stop().
  const ThreadHeapUsage& usage() const { return usage_; }

  // Returns this thread's heap usage from the start of the innermost
  // enclosing ThreadHeapUsageTracker instance, if any.
  static ThreadHeapUsage GetUsageSnapshot();

  // Enables the heap intercept. May only be called once, and only if the heap
  // shim is available, e.g. if BUILDFLAG(USE_ALLOCATOR_SHIM) is
  // true.
  static void EnableHeapTracking();

  // Returns true iff heap tracking is enabled.
  static bool IsHeapTrackingEnabled();

 protected:
  // Exposed for testing only - note that it's safe to re-EnableHeapTracking()
  // after calling this function in tests.
  static void DisableHeapTrackingForTesting();

  // Exposed for testing only.
  static void EnsureTLSInitialized();

  // Exposed to allow testing the shim without inserting it in the allocator
  // shim chain.
  static base::allocator::AllocatorDispatch* GetDispatchForTesting();

 private:
  ThreadChecker thread_checker_;

  // The heap usage at Start(), or the difference from Start() to Stop().
  ThreadHeapUsage usage_;

  // This thread's heap usage, non-null from Start() to Stop().
  ThreadHeapUsage* thread_usage_;
};

}  // namespace debug
}  // namespace base

#endif  // BASE_DEBUG_THREAD_HEAP_USAGE_TRACKER_H_