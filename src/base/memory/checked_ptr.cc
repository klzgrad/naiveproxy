// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/checked_ptr.h"

#include "base/allocator/partition_allocator/partition_alloc.h"

namespace base {
namespace internal {

#if defined(ARCH_CPU_64_BITS) && !defined(OS_NACL)

BASE_EXPORT bool CheckedPtr2ImplPartitionAllocSupport::EnabledForPtr(
    void* ptr) {
  // CheckedPtr2Impl works only when memory is allocated by PartitionAlloc and
  // only only if the pointer points to the beginning of the allocated slot.
  //
  // TODO(bartekn): Add |&& PartitionAllocGetSlotOffset(ptr) == 0|
  // CheckedPtr2Impl uses a fake implementation at the moment, which happens to
  // work even for non-0 offsets, so skip this check for now to get a better
  // coverage.
  return IsManagedByPartitionAlloc(ptr);
}

#endif

}  // namespace internal
}  // namespace base
