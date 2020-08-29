// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ADDRESS_SPACE_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ADDRESS_SPACE_H_

#include "base/allocator/partition_allocator/address_pool_manager.h"
#include "base/allocator/partition_allocator/partition_alloc_check.h"
#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/allocator/partition_allocator/partition_alloc_features.h"
#include "base/base_export.h"
#include "base/bits.h"
#include "base/feature_list.h"
#include "base/notreached.h"
#include "build/build_config.h"

namespace base {

namespace internal {

// The feature is not applicable to 32-bit address space.
// ARCH_CPU_64_BITS implies 64-bit instruction set, but not necessarily 64-bit
// address space. The only known case where address space is 32-bit is NaCl, so
// eliminate it explicitly. static_assert below ensures that other won't slip
// through.
// TODO(tasak): define ADDRESS_SPACE_64_BITS as "defined(ARCH_CPU_64_BITS) &&
// !defined(OS_NACL)" and use it.
#if defined(ARCH_CPU_64_BITS) && !defined(OS_NACL)

static_assert(sizeof(size_t) >= 8, "Nee more than 32-bit address space");

// Reserves address space for PartitionAllocator.
class BASE_EXPORT PartitionAddressSpace {
 public:
  static ALWAYS_INLINE internal::pool_handle GetDirectMapPool() {
    return direct_map_pool_;
  }
  static ALWAYS_INLINE internal::pool_handle GetNormalBucketPool() {
    return normal_bucket_pool_;
  }

  static void Init();
  static void UninitForTesting();

  static ALWAYS_INLINE bool Contains(const void* address) {
    return (reinterpret_cast<uintptr_t>(address) &
            kReservedAddressSpaceBaseMask) == reserved_base_address_;
  }

  static ALWAYS_INLINE bool IsInNormalBucketPool(const void* address) {
    return (reinterpret_cast<uintptr_t>(address) & kNormalBucketPoolBaseMask) ==
           normal_bucket_pool_base_address_;
  }

  // PartitionAddressSpace is static_only class.
  PartitionAddressSpace() = delete;
  PartitionAddressSpace(const PartitionAddressSpace&) = delete;
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;

 private:
  // Partition Alloc Address Space
  // Reserves 32GiB address space for 1 direct map space(16GiB) and 1 normal
  // bucket space(16GiB).
  // TODO(bartekn): Look into devices with 39-bit address space that have 256GiB
  // user-mode space. Libraries loaded at random addresses may stand in the way
  // of reserving a contiguous 64GiB region. (even though we're requesting only
  // 32GiB, AllocPages may under the covers reserve 64GiB to satisfy the
  // alignment requirements)
  //
  // +----------------+ reserved_base_address_(32GiB aligned)
  // |  direct map    |
  // |    space       |
  // +----------------+ reserved_base_address_ + 16GiB
  // | normal buckets |
  // |    space       |
  // +----------------+ reserved_base_address_ + 32GiB

  static constexpr size_t kGigaBytes = 1024 * 1024 * 1024;
  static constexpr size_t kDirectMapPoolSize = 16 * kGigaBytes;
  static constexpr size_t kNormalBucketPoolSize = 16 * kGigaBytes;
  static constexpr uintptr_t kNormalBucketPoolOffsetMask =
      static_cast<uintptr_t>(kNormalBucketPoolSize) - 1;
  static constexpr uintptr_t kNormalBucketPoolBaseMask =
      ~kNormalBucketPoolOffsetMask;

  // Reserves 32GiB aligned address space.
  // We align on 32GiB as well, and since it's a power of two we can check a
  // pointer with a single bitmask operation.
  static constexpr size_t kDesiredAddressSpaceSize =
      kDirectMapPoolSize + kNormalBucketPoolSize;
  static constexpr size_t kReservedAddressSpaceAlignment =
      kDesiredAddressSpaceSize;
  static constexpr uintptr_t kReservedAddressSpaceOffsetMask =
      static_cast<uintptr_t>(kReservedAddressSpaceAlignment) - 1;
  static constexpr uintptr_t kReservedAddressSpaceBaseMask =
      ~kReservedAddressSpaceOffsetMask;

  static_assert(
      bits::IsPowerOfTwo(PartitionAddressSpace::kReservedAddressSpaceAlignment),
      "kReservedAddressSpaceALignment should be a power of two.");
  static_assert(PartitionAddressSpace::kReservedAddressSpaceAlignment >=
                    PartitionAddressSpace::kDesiredAddressSpaceSize,
                "kReservedAddressSpaceAlignment should be larger or equal to "
                "kDesiredAddressSpaceSize.");
  static_assert(
      PartitionAddressSpace::kReservedAddressSpaceAlignment / 2 <
          PartitionAddressSpace::kDesiredAddressSpaceSize,
      "kReservedAddressSpaceAlignment should be the smallest power of "
      "two greater or equal to kDesiredAddressSpaceSize. So a half of "
      "the alignment should be smaller than the desired size.");

  // See the comment describing the address layout above.
  static uintptr_t reserved_base_address_;

  static uintptr_t normal_bucket_pool_base_address_;

  static internal::pool_handle direct_map_pool_;
  static internal::pool_handle normal_bucket_pool_;
};

ALWAYS_INLINE internal::pool_handle GetDirectMapPool() {
  PA_DCHECK(IsPartitionAllocGigaCageEnabled());
  return PartitionAddressSpace::GetDirectMapPool();
}

ALWAYS_INLINE internal::pool_handle GetNormalBucketPool() {
  PA_DCHECK(IsPartitionAllocGigaCageEnabled());
  return PartitionAddressSpace::GetNormalBucketPool();
}

#else  // defined(ARCH_CPU_64_BITS) && !defined(OS_NACL)

ALWAYS_INLINE internal::pool_handle GetDirectMapPool() {
  NOTREACHED();
  return 0;
}

ALWAYS_INLINE internal::pool_handle GetNormalBucketPool() {
  NOTREACHED();
  return 0;
}

#endif  // defined(ARCH_CPU_64_BITS) && !defined(OS_NACL)

}  // namespace internal

}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ADDRESS_SPACE_H_
