// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_

// DESCRIPTION
// PartitionRoot::Alloc() and PartitionRoot::Free() are approximately analagous
// to malloc() and free().
//
// The main difference is that a PartitionRoot object must be supplied to these
// functions, representing a specific "heap partition" that will be used to
// satisfy the allocation. Different partitions are guaranteed to exist in
// separate address spaces, including being separate from the main system
// heap. If the contained objects are all freed, physical memory is returned to
// the system but the address space remains reserved.  See PartitionAlloc.md for
// other security properties PartitionAlloc provides.
//
// THE ONLY LEGITIMATE WAY TO OBTAIN A PartitionRoot IS THROUGH THE
// PartitionAllocator classes. To minimize the instruction count to the fullest
// extent possible, the PartitionRoot is really just a header adjacent to other
// data areas provided by the allocator class.
//
// The constraints for PartitionRoot::Alloc() are:
// - Multi-threaded use against a single partition is ok; locking is handled.
// - Allocations of any arbitrary size can be handled (subject to a limit of
//   INT_MAX bytes for security reasons).
// - Bucketing is by approximate size, for example an allocation of 4000 bytes
//   might be placed into a 4096-byte bucket. Bucket sizes are chosen to try and
//   keep worst-case waste to ~10%.
//
// The allocators are designed to be extremely fast, thanks to the following
// properties and design:
// - Just two single (reasonably predicatable) branches in the hot / fast path
//   for both allocating and (significantly) freeing.
// - A minimal number of operations in the hot / fast path, with the slow paths
//   in separate functions, leading to the possibility of inlining.
// - Each partition page (which is usually multiple physical pages) has a
//   metadata structure which allows fast mapping of free() address to an
//   underlying bucket.
// - Supports a lock-free API for fast performance in single-threaded cases.
// - The freelist for a given bucket is split across a number of partition
//   pages, enabling various simple tricks to try and minimize fragmentation.
// - Fine-grained bucket sizes leading to less waste and better packing.
//
// The following security properties could be investigated in the future:
// - Per-object bucketing (instead of per-size) is mostly available at the API,
// but not used yet.
// - No randomness of freelist entries or bucket position.
// - Better checking for wild pointers in free().
// - Better freelist masking function to guarantee fault on 32-bit.

#include <limits.h>
#include <string.h>

#include "base/allocator/partition_allocator/memory_reclaimer.h"
#include "base/allocator/partition_allocator/page_allocator.h"
#include "base/allocator/partition_allocator/partition_address_space.h"
#include "base/allocator/partition_allocator/partition_alloc_check.h"
#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/allocator/partition_allocator/partition_alloc_forward.h"
#include "base/allocator/partition_allocator/partition_bucket.h"
#include "base/allocator/partition_allocator/partition_cookie.h"
#include "base/allocator/partition_allocator/partition_direct_map_extent.h"
#include "base/allocator/partition_allocator/partition_page.h"
#include "base/allocator/partition_allocator/spin_lock.h"
#include "base/base_export.h"
#include "base/bits.h"
#include "base/check_op.h"
#include "base/compiler_specific.h"
#include "base/notreached.h"
#include "base/partition_alloc_buildflags.h"
#include "base/stl_util.h"
#include "base/sys_byteorder.h"
#include "build/build_config.h"
#include "build/buildflag.h"

#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
#include <stdlib.h>
#endif

// We use this to make MEMORY_TOOL_REPLACES_ALLOCATOR behave the same for max
// size as other alloc code.
#define CHECK_MAX_SIZE_OR_RETURN_NULLPTR(size, flags) \
  if (size > kGenericMaxDirectMapped) {               \
    if (flags & PartitionAllocReturnNull) {           \
      return nullptr;                                 \
    }                                                 \
    PA_CHECK(false);                                  \
  }

namespace base {

typedef void (*OomFunction)(size_t);

// PartitionAlloc supports setting hooks to observe allocations/frees as they
// occur as well as 'override' hooks that allow overriding those operations.
class BASE_EXPORT PartitionAllocHooks {
 public:
  // Log allocation and free events.
  typedef void AllocationObserverHook(void* address,
                                      size_t size,
                                      const char* type_name);
  typedef void FreeObserverHook(void* address);

  // If it returns true, the allocation has been overridden with the pointer in
  // *out.
  typedef bool AllocationOverrideHook(void** out,
                                      int flags,
                                      size_t size,
                                      const char* type_name);
  // If it returns true, then the allocation was overridden and has been freed.
  typedef bool FreeOverrideHook(void* address);
  // If it returns true, the underlying allocation is overridden and *out holds
  // the size of the underlying allocation.
  typedef bool ReallocOverrideHook(size_t* out, void* address);

  // To unhook, call Set*Hooks with nullptrs.
  static void SetObserverHooks(AllocationObserverHook* alloc_hook,
                               FreeObserverHook* free_hook);
  static void SetOverrideHooks(AllocationOverrideHook* alloc_hook,
                               FreeOverrideHook* free_hook,
                               ReallocOverrideHook realloc_hook);

  // Helper method to check whether hooks are enabled. This is an optimization
  // so that if a function needs to call observer and override hooks in two
  // different places this value can be cached and only loaded once.
  static bool AreHooksEnabled() {
    return hooks_enabled_.load(std::memory_order_relaxed);
  }

  static void AllocationObserverHookIfEnabled(void* address,
                                              size_t size,
                                              const char* type_name);
  static bool AllocationOverrideHookIfEnabled(void** out,
                                              int flags,
                                              size_t size,
                                              const char* type_name);

  static void FreeObserverHookIfEnabled(void* address);
  static bool FreeOverrideHookIfEnabled(void* address);

  static void ReallocObserverHookIfEnabled(void* old_address,
                                           void* new_address,
                                           size_t size,
                                           const char* type_name);
  static bool ReallocOverrideHookIfEnabled(size_t* out, void* address);

 private:
  // Single bool that is used to indicate whether observer or allocation hooks
  // are set to reduce the numbers of loads required to check whether hooking is
  // enabled.
  static std::atomic<bool> hooks_enabled_;

  // Lock used to synchronize Set*Hooks calls.
  static std::atomic<AllocationObserverHook*> allocation_observer_hook_;
  static std::atomic<FreeObserverHook*> free_observer_hook_;

  static std::atomic<AllocationOverrideHook*> allocation_override_hook_;
  static std::atomic<FreeOverrideHook*> free_override_hook_;
  static std::atomic<ReallocOverrideHook*> realloc_override_hook_;
};

namespace internal {

template <bool thread_safe>
class LOCKABLE MaybeSpinLock {
 public:
  void Lock() EXCLUSIVE_LOCK_FUNCTION() {}
  void Unlock() UNLOCK_FUNCTION() {}
  void AssertAcquired() const ASSERT_EXCLUSIVE_LOCK() {}
};

template <bool thread_safe>
class SCOPED_LOCKABLE ScopedGuard {
 public:
  explicit ScopedGuard(MaybeSpinLock<thread_safe>& lock)
      EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_.Lock();
  }
  ~ScopedGuard() UNLOCK_FUNCTION() { lock_.Unlock(); }

 private:
  MaybeSpinLock<thread_safe>& lock_;
};

#if DCHECK_IS_ON()
template <>
class LOCKABLE MaybeSpinLock<ThreadSafe> {
 public:
  MaybeSpinLock() : lock_() {}
  void Lock() EXCLUSIVE_LOCK_FUNCTION() { lock_->Acquire(); }
  void Unlock() UNLOCK_FUNCTION() { lock_->Release(); }
  void AssertAcquired() const ASSERT_EXCLUSIVE_LOCK() {
    lock_->AssertAcquired();
  }

 private:
  // NoDestructor to avoid issues with the "static destruction order fiasco".
  //
  // This also means that for DCHECK_IS_ON() builds we leak a lock when a
  // partition is destructed. This will in practice only show in some tests, as
  // partitons are not destructed in regular use. In addition, on most
  // platforms, base::Lock doesn't allocate memory and neither does the OS
  // library, and the destructor is a no-op.
  base::NoDestructor<base::Lock> lock_;
};

#else
template <>
class LOCKABLE MaybeSpinLock<ThreadSafe> {
 public:
  void Lock() EXCLUSIVE_LOCK_FUNCTION() { lock_.lock(); }
  void Unlock() UNLOCK_FUNCTION() { lock_.unlock(); }
  void AssertAcquired() const ASSERT_EXCLUSIVE_LOCK() {
    // Not supported by subtle::SpinLock.
  }

 private:
  subtle::SpinLock lock_;
};
#endif  // DCHECK_IS_ON()

// An "extent" is a span of consecutive superpages. We link to the partition's
// next extent (if there is one) to the very start of a superpage's metadata
// area.
template <bool thread_safe>
struct PartitionSuperPageExtentEntry {
  PartitionRoot<thread_safe>* root;
  char* super_page_base;
  char* super_pages_end;
  PartitionSuperPageExtentEntry<thread_safe>* next;
};
static_assert(
    sizeof(PartitionSuperPageExtentEntry<ThreadSafe>) <= kPageMetadataSize,
    "PartitionSuperPageExtentEntry must be able to fit in a metadata slot");

// g_oom_handling_function is invoked when PartitionAlloc hits OutOfMemory.
static OomFunction g_oom_handling_function = nullptr;

}  // namespace internal

class PartitionStatsDumper;

enum PartitionPurgeFlags {
  // Decommitting the ring list of empty pages is reasonably fast.
  PartitionPurgeDecommitEmptyPages = 1 << 0,
  // Discarding unused system pages is slower, because it involves walking all
  // freelists in all active partition pages of all buckets >= system page
  // size. It often frees a similar amount of memory to decommitting the empty
  // pages, though.
  PartitionPurgeDiscardUnusedSystemPages = 1 << 1,
};

// Struct used to retrieve total memory usage of a partition. Used by
// PartitionStatsDumper implementation.
struct PartitionMemoryStats {
  size_t total_mmapped_bytes;    // Total bytes mmaped from the system.
  size_t total_committed_bytes;  // Total size of commmitted pages.
  size_t total_resident_bytes;   // Total bytes provisioned by the partition.
  size_t total_active_bytes;     // Total active bytes in the partition.
  size_t total_decommittable_bytes;  // Total bytes that could be decommitted.
  size_t total_discardable_bytes;    // Total bytes that could be discarded.
};

// Struct used to retrieve memory statistics about a partition bucket. Used by
// PartitionStatsDumper implementation.
struct PartitionBucketMemoryStats {
  bool is_valid;       // Used to check if the stats is valid.
  bool is_direct_map;  // True if this is a direct mapping; size will not be
                       // unique.
  uint32_t bucket_slot_size;     // The size of the slot in bytes.
  uint32_t allocated_page_size;  // Total size the partition page allocated from
                                 // the system.
  uint32_t active_bytes;         // Total active bytes used in the bucket.
  uint32_t resident_bytes;       // Total bytes provisioned in the bucket.
  uint32_t decommittable_bytes;  // Total bytes that could be decommitted.
  uint32_t discardable_bytes;    // Total bytes that could be discarded.
  uint32_t num_full_pages;       // Number of pages with all slots allocated.
  uint32_t num_active_pages;     // Number of pages that have at least one
                                 // provisioned slot.
  uint32_t num_empty_pages;      // Number of pages that are empty
                                 // but not decommitted.
  uint32_t num_decommitted_pages;  // Number of pages that are empty
                                   // and decommitted.
};

// Interface that is passed to PartitionDumpStats and
// PartitionDumpStatsGeneric for using the memory statistics.
class BASE_EXPORT PartitionStatsDumper {
 public:
  // Called to dump total memory used by partition, once per partition.
  virtual void PartitionDumpTotals(const char* partition_name,
                                   const PartitionMemoryStats*) = 0;

  // Called to dump stats about buckets, for each bucket.
  virtual void PartitionsDumpBucketStats(const char* partition_name,
                                         const PartitionBucketMemoryStats*) = 0;
};

// Never instantiate a PartitionRoot directly, instead use
// PartitionAllocator.
template <bool thread_safe>
struct BASE_EXPORT PartitionRoot {
  using Page = internal::PartitionPage<thread_safe>;
  using Bucket = internal::PartitionBucket<thread_safe>;
  using SuperPageExtentEntry =
      internal::PartitionSuperPageExtentEntry<thread_safe>;
  using DirectMapExtent = internal::PartitionDirectMapExtent<thread_safe>;
  using ScopedGuard = internal::ScopedGuard<thread_safe>;

  internal::MaybeSpinLock<thread_safe> lock_;
  size_t total_size_of_committed_pages = 0;
  size_t total_size_of_super_pages = 0;
  size_t total_size_of_direct_mapped_pages = 0;
  // Invariant: total_size_of_committed_pages <=
  //                total_size_of_super_pages +
  //                total_size_of_direct_mapped_pages.
  unsigned num_buckets = 0;
  unsigned max_allocation = 0;
  // Atomic as initialization can be concurrent.
  std::atomic<bool> initialized = {};
  char* next_super_page = nullptr;
  char* next_partition_page = nullptr;
  char* next_partition_page_end = nullptr;
  SuperPageExtentEntry* current_extent = nullptr;
  SuperPageExtentEntry* first_extent = nullptr;
  DirectMapExtent* direct_map_list = nullptr;
  Page* global_empty_page_ring[kMaxFreeableSpans] = {};
  int16_t global_empty_page_ring_index = 0;
  uintptr_t inverted_self = 0;

  // Some pre-computed constants.
  size_t order_index_shifts[kBitsPerSizeT + 1] = {};
  size_t order_sub_index_masks[kBitsPerSizeT + 1] = {};
  // The bucket lookup table lets us map a size_t to a bucket quickly.
  // The trailing +1 caters for the overflow case for very large allocation
  // sizes.  It is one flat array instead of a 2D array because in the 2D
  // world, we'd need to index array[blah][max+1] which risks undefined
  // behavior.
  Bucket* bucket_lookups[((kBitsPerSizeT + 1) * kGenericNumBucketsPerOrder) +
                         1] = {};
  Bucket buckets[kGenericNumBuckets] = {};

  PartitionRoot() = default;
  ~PartitionRoot() = default;

  // Public API
  //
  // Allocates out of the given bucket. Properly, this function should probably
  // be in PartitionBucket, but because the implementation needs to be inlined
  // for performance, and because it needs to inspect PartitionPage,
  // it becomes impossible to have it in PartitionBucket as this causes a
  // cyclical dependency on PartitionPage function implementations.
  //
  // Moving it a layer lower couples PartitionRoot and PartitionBucket, but
  // preserves the layering of the includes.
  ALWAYS_INLINE void Init() {
    if (LIKELY(initialized.load(std::memory_order_relaxed)))
      return;

    InitSlowPath();
  }

  ALWAYS_INLINE static bool IsValidPage(Page* page);
  ALWAYS_INLINE static PartitionRoot* FromPage(Page* page);

  ALWAYS_INLINE void IncreaseCommittedPages(size_t len);
  ALWAYS_INLINE void DecreaseCommittedPages(size_t len);
  ALWAYS_INLINE void DecommitSystemPages(void* address, size_t length)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  ALWAYS_INLINE void RecommitSystemPages(void* address, size_t length)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  NOINLINE void OutOfMemory(size_t size);

  ALWAYS_INLINE void* Alloc(size_t size, const char* type_name);
  ALWAYS_INLINE void* AllocFlags(int flags, size_t size, const char* type_name);

  ALWAYS_INLINE void* Realloc(void* ptr,
                              size_t new_size,
                              const char* type_name);
  // Overload that may return nullptr if reallocation isn't possible. In this
  // case, |ptr| remains valid.
  ALWAYS_INLINE void* TryRealloc(void* ptr,
                                 size_t new_size,
                                 const char* type_name);
  NOINLINE void* ReallocFlags(int flags,
                              void* ptr,
                              size_t new_size,
                              const char* type_name);
  ALWAYS_INLINE void Free(void* ptr);

  ALWAYS_INLINE size_t ActualSize(size_t size);

  // Frees memory from this partition, if possible, by decommitting pages.
  // |flags| is an OR of base::PartitionPurgeFlags.
  void PurgeMemory(int flags);

  void DumpStats(const char* partition_name,
                 bool is_light_dump,
                 PartitionStatsDumper* partition_stats_dumper);

  internal::PartitionBucket<thread_safe>* SizeToBucket(size_t size) const;

 private:
  void InitSlowPath();
  ALWAYS_INLINE void* AllocFromBucket(Bucket* bucket, int flags, size_t size)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  bool ReallocDirectMappedInPlace(internal::PartitionPage<thread_safe>* page,
                                  size_t raw_size)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void DecommitEmptyPages() EXCLUSIVE_LOCKS_REQUIRED(lock_);
};

template <bool thread_safe>
ALWAYS_INLINE void* PartitionRoot<thread_safe>::AllocFromBucket(Bucket* bucket,
                                                                int flags,
                                                                size_t size) {
  bool zero_fill = flags & PartitionAllocZeroFill;
  bool is_already_zeroed = false;

  Page* page = bucket->active_pages_head;
  // Check that this page is neither full nor freed.
  PA_DCHECK(page);
  PA_DCHECK(page->num_allocated_slots >= 0);
  void* ret = page->freelist_head;
  if (LIKELY(ret)) {
    // If these DCHECKs fire, you probably corrupted memory. TODO(palmer): See
    // if we can afford to make these CHECKs.
    PA_DCHECK(IsValidPage(page));

    // All large allocations must go through the slow path to correctly update
    // the size metadata.
    PA_DCHECK(page->get_raw_size() == 0);
    internal::PartitionFreelistEntry* new_head =
        internal::EncodedPartitionFreelistEntry::Decode(
            page->freelist_head->next);
    page->freelist_head = new_head;
    page->num_allocated_slots++;
  } else {
    ret = bucket->SlowPathAlloc(this, flags, size, &is_already_zeroed);
    // TODO(palmer): See if we can afford to make this a CHECK.
    PA_DCHECK(!ret || IsValidPage(Page::FromPointer(ret)));
  }

#if DCHECK_IS_ON() && !BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  if (!ret) {
    return nullptr;
  }

  page = Page::FromPointer(ret);
  // TODO(ajwong): Can |page->bucket| ever not be |bucket|? If not, can this
  // just be bucket->slot_size?
  size_t new_slot_size = page->bucket->slot_size;
  size_t raw_size = page->get_raw_size();
  if (raw_size) {
    PA_DCHECK(raw_size == size);
    new_slot_size = raw_size;
  }
  size_t no_cookie_size =
      internal::PartitionCookieSizeAdjustSubtract(new_slot_size);
  char* char_ret = static_cast<char*>(ret);
  // The value given to the application is actually just after the cookie.
  ret = char_ret + internal::kCookieSize;

  // Fill the region kUninitializedByte or 0, and surround it with 2 cookies.
  internal::PartitionCookieWriteValue(char_ret);
  if (!zero_fill) {
    memset(ret, kUninitializedByte, no_cookie_size);
  } else if (!is_already_zeroed) {
    memset(ret, 0, no_cookie_size);
  }
  internal::PartitionCookieWriteValue(char_ret + internal::kCookieSize +
                                      no_cookie_size);
#else
  if (ret && zero_fill && !is_already_zeroed) {
    memset(ret, 0, size);
  }
#endif

  return ret;
}

template <bool thread_safe>
ALWAYS_INLINE void PartitionRoot<thread_safe>::Free(void* ptr) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  free(ptr);
#else
  PA_DCHECK(initialized);

  if (UNLIKELY(!ptr))
    return;

  if (PartitionAllocHooks::AreHooksEnabled()) {
    PartitionAllocHooks::FreeObserverHookIfEnabled(ptr);
    if (PartitionAllocHooks::FreeOverrideHookIfEnabled(ptr))
      return;
  }

  ptr = internal::PartitionCookieFreePointerAdjust(ptr);
  Page* page = Page::FromPointer(ptr);
  // TODO(palmer): See if we can afford to make this a CHECK.
  PA_DCHECK(IsValidPage(page));
  internal::DeferredUnmap deferred_unmap;
  {
    ScopedGuard guard{lock_};
    deferred_unmap = page->Free(ptr);
  }
  deferred_unmap.Run();
#endif
}

// static
template <bool thread_safe>
ALWAYS_INLINE bool PartitionRoot<thread_safe>::IsValidPage(Page* page) {
  PartitionRoot* root = FromPage(page);
  return root->inverted_self == ~reinterpret_cast<uintptr_t>(root);
}

template <bool thread_safe>
ALWAYS_INLINE PartitionRoot<thread_safe>* PartitionRoot<thread_safe>::FromPage(
    Page* page) {
  auto* extent_entry = reinterpret_cast<SuperPageExtentEntry*>(
      reinterpret_cast<uintptr_t>(page) & kSystemPageBaseMask);
  return extent_entry->root;
}

template <bool thread_safe>
ALWAYS_INLINE void PartitionRoot<thread_safe>::IncreaseCommittedPages(
    size_t len) {
  total_size_of_committed_pages += len;
  PA_DCHECK(total_size_of_committed_pages <=
            total_size_of_super_pages + total_size_of_direct_mapped_pages);
}

template <bool thread_safe>
ALWAYS_INLINE void PartitionRoot<thread_safe>::DecreaseCommittedPages(
    size_t len) {
  total_size_of_committed_pages -= len;
  PA_DCHECK(total_size_of_committed_pages <=
            total_size_of_super_pages + total_size_of_direct_mapped_pages);
}

template <bool thread_safe>
ALWAYS_INLINE void PartitionRoot<thread_safe>::DecommitSystemPages(
    void* address,
    size_t length) {
  ::base::DecommitSystemPages(address, length);
  DecreaseCommittedPages(length);
}

template <bool thread_safe>
ALWAYS_INLINE void PartitionRoot<thread_safe>::RecommitSystemPages(
    void* address,
    size_t length) {
  PA_CHECK(::base::RecommitSystemPages(address, length, PageReadWrite));
  IncreaseCommittedPages(length);
}

BASE_EXPORT void PartitionAllocGlobalInit(OomFunction on_out_of_memory);
BASE_EXPORT void PartitionAllocGlobalUninitForTesting();

ALWAYS_INLINE bool IsManagedByPartitionAlloc(const void* address) {
#if BUILDFLAG(USE_PARTITION_ALLOC) && defined(ARCH_CPU_64_BITS) && \
    !defined(OS_NACL)
  return internal::PartitionAddressSpace::Contains(address);
#else
  return false;
#endif
}

ALWAYS_INLINE bool IsManagedByPartitionAllocAndNotDirectMapped(
    const void* address) {
#if BUILDFLAG(USE_PARTITION_ALLOC) && defined(ARCH_CPU_64_BITS) && \
    !defined(OS_NACL)
  return internal::PartitionAddressSpace::IsInNormalBucketPool(address);
#else
  return false;
#endif
}

ALWAYS_INLINE bool PartitionAllocSupportsGetSize() {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  return false;
#else
  return true;
#endif
}

namespace internal {
// Gets the PartitionPage object for the first partition page of the slot span
// that contains |ptr|. It's used with intention to do obtain the slot size.
// CAUTION! It works well for normal buckets, but for direct-mapped allocations
// it'll only work if |ptr| is in the first partition page of the allocation.
template <bool thread_safe>
ALWAYS_INLINE internal::PartitionPage<thread_safe>*
PartitionAllocGetPageForSize(void* ptr) {
  // No need to lock here. Only |ptr| being freed by another thread could
  // cause trouble, and the caller is responsible for that not happening.
  PA_DCHECK(PartitionAllocSupportsGetSize());
  auto* page =
      internal::PartitionPage<thread_safe>::FromPointerNoAlignmentCheck(ptr);
  // TODO(palmer): See if we can afford to make this a CHECK.
  PA_DCHECK(PartitionRoot<thread_safe>::IsValidPage(page));
  return page;
}
}  // namespace internal

// Gets the size of the allocated slot that contains |ptr|, adjusted for cookie
// (if any).
// CAUTION! For direct-mapped allocation, |ptr| has to be within the first
// partition page.
template <bool thread_safe>
ALWAYS_INLINE size_t PartitionAllocGetSize(void* ptr) {
  ptr = internal::PartitionCookieFreePointerAdjust(ptr);
  auto* page = internal::PartitionAllocGetPageForSize<thread_safe>(ptr);
  return internal::PartitionCookieSizeAdjustSubtract(page->bucket->slot_size);
}

// Gets the offset from the beginning of the allocated slot, adjusted for cookie
// (if any).
// CAUTION! Use only for normal buckets. Using on direct-mapped allocations may
// lead to undefined behavior.
template <bool thread_safe>
ALWAYS_INLINE size_t PartitionAllocGetSlotOffset(void* ptr) {
  PA_DCHECK(IsManagedByPartitionAllocAndNotDirectMapped(ptr));
  ptr = internal::PartitionCookieFreePointerAdjust(ptr);
  auto* page = internal::PartitionAllocGetPageForSize<thread_safe>(ptr);
  size_t slot_size = page->bucket->slot_size;

  // Get the offset from the beginning of the slot span.
  uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t slot_span_start = reinterpret_cast<uintptr_t>(
      internal::PartitionPage<thread_safe>::ToPointer(page));
  size_t offset_in_slot_span = ptr_addr - slot_span_start;
  // Knowing that slots are tightly packed in a slot span, calculate an offset
  // within a slot using simple % operation.
  // TODO(bartekn): Try to replace % with multiplication&shift magic.
  size_t offset_in_slot = offset_in_slot_span % slot_size;
  return offset_in_slot;
}

template <bool thread_safe>
ALWAYS_INLINE internal::PartitionBucket<thread_safe>*
PartitionRoot<thread_safe>::SizeToBucket(size_t size) const {
  size_t order = kBitsPerSizeT - bits::CountLeadingZeroBitsSizeT(size);
  // The order index is simply the next few bits after the most significant bit.
  size_t order_index =
      (size >> order_index_shifts[order]) & (kGenericNumBucketsPerOrder - 1);
  // And if the remaining bits are non-zero we must bump the bucket up.
  size_t sub_order_index = size & order_sub_index_masks[order];
  Bucket* bucket = bucket_lookups[(order << kGenericNumBucketsPerOrderBits) +
                                  order_index + !!sub_order_index];
  PA_CHECK(bucket);
  PA_DCHECK(!bucket->slot_size || bucket->slot_size >= size);
  PA_DCHECK(!(bucket->slot_size % kGenericSmallestBucket));
  return bucket;
}

template <bool thread_safe>
ALWAYS_INLINE void* PartitionRoot<thread_safe>::AllocFlags(
    int flags,
    size_t size,
    const char* type_name) {
  PA_DCHECK(flags < PartitionAllocLastFlag << 1);

#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  CHECK_MAX_SIZE_OR_RETURN_NULLPTR(size, flags);
  const bool zero_fill = flags & PartitionAllocZeroFill;
  void* result = zero_fill ? calloc(1, size) : malloc(size);
  PA_CHECK(result || flags & PartitionAllocReturnNull);
  return result;
#else
  PA_DCHECK(initialized);
  void* result;
  const bool hooks_enabled = PartitionAllocHooks::AreHooksEnabled();
  if (UNLIKELY(hooks_enabled)) {
    if (PartitionAllocHooks::AllocationOverrideHookIfEnabled(&result, flags,
                                                             size, type_name)) {
      PartitionAllocHooks::AllocationObserverHookIfEnabled(result, size,
                                                           type_name);
      return result;
    }
  }
  size_t requested_size = size;
  size = internal::PartitionCookieSizeAdjustAdd(size);
  auto* bucket = SizeToBucket(size);
  PA_DCHECK(bucket);
  {
    internal::ScopedGuard<thread_safe> guard{lock_};
    result = AllocFromBucket(bucket, flags, size);
  }
  if (UNLIKELY(hooks_enabled)) {
    PartitionAllocHooks::AllocationObserverHookIfEnabled(result, requested_size,
                                                         type_name);
  }

  return result;
#endif
}

template <bool thread_safe>
ALWAYS_INLINE void* PartitionRoot<thread_safe>::Alloc(size_t size,
                                                      const char* type_name) {
  return AllocFlags(0, size, type_name);
}

template <bool thread_safe>
ALWAYS_INLINE void* PartitionRoot<thread_safe>::Realloc(void* ptr,
                                                        size_t new_size,
                                                        const char* type_name) {
  return ReallocFlags(0, ptr, new_size, type_name);
}

template <bool thread_safe>
ALWAYS_INLINE void* PartitionRoot<thread_safe>::TryRealloc(
    void* ptr,
    size_t new_size,
    const char* type_name) {
  return ReallocFlags(PartitionAllocReturnNull, ptr, new_size, type_name);
}

template <bool thread_safe>
ALWAYS_INLINE size_t PartitionRoot<thread_safe>::ActualSize(size_t size) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  return size;
#else
  PA_DCHECK(PartitionRoot<thread_safe>::initialized);
  size = internal::PartitionCookieSizeAdjustAdd(size);
  auto* bucket = SizeToBucket(size);
  if (LIKELY(!bucket->is_direct_mapped())) {
    size = bucket->slot_size;
  } else if (size > kGenericMaxDirectMapped) {
    // Too large to allocate => return the size unchanged.
  } else {
    size = Bucket::get_direct_map_size(size);
  }
  return internal::PartitionCookieSizeAdjustSubtract(size);
#endif
}

namespace internal {
template <bool thread_safe>
struct BASE_EXPORT PartitionAllocator {
  PartitionAllocator() = default;
  ~PartitionAllocator() {
    PartitionAllocMemoryReclaimer::Instance()->UnregisterPartition(
        &partition_root_);
  }

  void init() {
    partition_root_.Init();
    PartitionAllocMemoryReclaimer::Instance()->RegisterPartition(
        &partition_root_);
  }
  ALWAYS_INLINE PartitionRoot<thread_safe>* root() { return &partition_root_; }

 private:
  PartitionRoot<thread_safe> partition_root_;
};
}  // namespace internal

using PartitionAllocator = internal::PartitionAllocator<internal::ThreadSafe>;
using ThreadUnsafePartitionAllocator =
    internal::PartitionAllocator<internal::NotThreadSafe>;

using ThreadSafePartitionRoot = PartitionRoot<internal::ThreadSafe>;
using ThreadUnsafePartitionRoot = PartitionRoot<internal::NotThreadSafe>;

}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_
