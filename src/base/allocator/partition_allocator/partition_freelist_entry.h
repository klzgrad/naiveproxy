// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_FREELIST_ENTRY_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_FREELIST_ENTRY_H_

#include <stdint.h>

#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/compiler_specific.h"
#include "base/sys_byteorder.h"
#include "build/build_config.h"

namespace base {
namespace internal {

// TODO(ajwong): Introduce an EncodedFreelistEntry type and then replace
// Transform() with Encode()/Decode() such that the API provides some static
// type safety.
//
// https://crbug.com/787153
struct PartitionFreelistEntry {
  PartitionFreelistEntry* next;

  static ALWAYS_INLINE PartitionFreelistEntry* Transform(
      PartitionFreelistEntry* ptr) {
// We use bswap on little endian as a fast mask for two reasons:
// 1) If an object is freed and its vtable used where the attacker doesn't
// get the chance to run allocations between the free and use, the vtable
// dereference is likely to fault.
// 2) If the attacker has a linear buffer overflow and elects to try and
// corrupt a freelist pointer, partial pointer overwrite attacks are
// thwarted.
// For big endian, similar guarantees are arrived at with a negation.
#if defined(ARCH_CPU_BIG_ENDIAN)
    uintptr_t masked = ~reinterpret_cast<uintptr_t>(ptr);
#else
    uintptr_t masked = ByteSwapUintPtrT(reinterpret_cast<uintptr_t>(ptr));
#endif
    return reinterpret_cast<PartitionFreelistEntry*>(masked);
  }
};

}  // namespace internal
}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_FREELIST_ENTRY_H_
