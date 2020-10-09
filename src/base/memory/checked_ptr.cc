// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/checked_ptr.h"

#include "base/allocator/partition_allocator/checked_ptr_support.h"
#include "base/allocator/partition_allocator/partition_address_space.h"
#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/partition_alloc_buildflags.h"
#include "build/build_config.h"
#include "build/buildflag.h"

namespace base {
namespace internal {

#if defined(ARCH_CPU_64_BITS) && !defined(OS_NACL) && \
    BUILDFLAG(USE_PARTITION_ALLOC) && ENABLE_CHECKED_PTR2_OR_MTE_IMPL

BASE_EXPORT bool CheckedPtr2OrMTEImplPartitionAllocSupport::EnabledForPtr(
    void* ptr) {
  // CheckedPtr2 and MTECheckedPtr algorithms work only when memory is allocated
  // by PartitionAlloc, from normal buckets pool. CheckedPtr2 additionally
  // requires that the pointer points to the beginning of the allocated slot.
  //
  // TODO(bartekn): Allow direct-map buckets for MTECheckedPtr, once
  // PartitionAlloc supports it. (Currently not implemented for simplicity, but
  // there are no technological obstacles preventing it; whereas in case of
  // CheckedPtr2, PartitionAllocGetSlotOffset won't work with direct-map.)
  //
  // NOTE, CheckedPtr doesn't know which thread-safery PartitionAlloc variant
  // it's dealing with. Just use ThreadSafe variant, because it's more common.
  // NotThreadSafe is only used by Blink's layout, which is currently being
  // transitioned to Oilpan. PartitionAllocGetSlotOffset is expected to return
  // the same result regardless, anyway.
  // TODO(bartekn): Figure out the thread-safety mismatch.
  return IsManagedByPartitionAllocNormalBuckets(ptr)
  // Checking offset is not needed for ENABLE_TAG_FOR_SINGLE_TAG_CHECKED_PTR,
  // but call it anyway for apples-to-apples comparison with
  // ENABLE_TAG_FOR_CHECKED_PTR2.
#if ENABLE_TAG_FOR_CHECKED_PTR2 || ENABLE_TAG_FOR_SINGLE_TAG_CHECKED_PTR
         && PartitionAllocGetSlotOffset<ThreadSafe>(ptr) == 0
#endif
      ;
}

#endif

}  // namespace internal
}  // namespace base
