// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/partition_root_base.h"

#include "base/allocator/partition_allocator/oom.h"
#include "base/allocator/partition_allocator/partition_oom.h"
#include "base/allocator/partition_allocator/partition_page.h"
#include "build/build_config.h"

namespace base {
namespace internal {

template <bool thread_safety>
NOINLINE void PartitionRootBase<thread_safety>::OutOfMemory(size_t size) {
#if !defined(ARCH_CPU_64_BITS)
  // Check whether this OOM is due to a lot of super pages that are allocated
  // but not committed, probably due to http://crbug.com/421387.
  if (total_size_of_super_pages + total_size_of_direct_mapped_pages -
          total_size_of_committed_pages >
      kReasonableSizeOfUnusedPages) {
    PartitionOutOfMemoryWithLotsOfUncommitedPages(size);
  }
#endif
  if (g_oom_handling_function)
    (*g_oom_handling_function)(size);
  OOM_CRASH(size);
}

template <bool thread_safe>
void PartitionRootBase<thread_safe>::DecommitEmptyPages() {
  for (size_t i = 0; i < kMaxFreeableSpans; ++i) {
    Page* page = global_empty_page_ring[i];
    if (page)
      page->DecommitIfPossible(this);
    global_empty_page_ring[i] = nullptr;
  }
}

template <bool thread_safe>
internal::PartitionRootBase<thread_safe>::PartitionRootBase() = default;
template <bool thread_safe>
internal::PartitionRootBase<thread_safe>::~PartitionRootBase() = default;

template struct PartitionRootBase<ThreadSafe>;
template struct PartitionRootBase<NotThreadSafe>;

}  // namespace internal
}  // namespace base
