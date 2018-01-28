// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_

// DESCRIPTION
// PartitionRoot::Alloc() / PartitionRootGeneric::Alloc() and PartitionFree() /
// PartitionRootGeneric::Free() are approximately analagous to malloc() and
// free().
//
// The main difference is that a PartitionRoot / PartitionRootGeneric object
// must be supplied to these functions, representing a specific "heap partition"
// that will be used to satisfy the allocation. Different partitions are
// guaranteed to exist in separate address spaces, including being separate from
// the main system heap. If the contained objects are all freed, physical memory
// is returned to the system but the address space remains reserved.
// See PartitionAlloc.md for other security properties PartitionAlloc provides.
//
// THE ONLY LEGITIMATE WAY TO OBTAIN A PartitionRoot IS THROUGH THE
// SizeSpecificPartitionAllocator / PartitionAllocatorGeneric classes. To
// minimize the instruction count to the fullest extent possible, the
// PartitionRoot is really just a header adjacent to other data areas provided
// by the allocator class.
//
// The PartitionRoot::Alloc() variant of the API has the following caveats:
// - Allocations and frees against a single partition must be single threaded.
// - Allocations must not exceed a max size, chosen at compile-time via a
// templated parameter to PartitionAllocator.
// - Allocation sizes must be aligned to the system pointer size.
// - Allocations are bucketed exactly according to size.
//
// And for PartitionRootGeneric::Alloc():
// - Multi-threaded use against a single partition is ok; locking is handled.
// - Allocations of any arbitrary size can be handled (subject to a limit of
// INT_MAX bytes for security reasons).
// - Bucketing is by approximate size, for example an allocation of 4000 bytes
// might be placed into a 4096-byte bucket. Bucket sizes are chosen to try and
// keep worst-case waste to ~10%.
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

#include "base/allocator/partition_allocator/page_allocator.h"
#include "base/allocator/partition_allocator/spin_lock.h"
#include "base/base_export.h"
#include "base/bits.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/sys_byteorder.h"
#include "build/build_config.h"

#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
#include <stdlib.h>
#endif

namespace base {

// Allocation granularity of sizeof(void*) bytes.
static const size_t kAllocationGranularity = sizeof(void*);
static const size_t kAllocationGranularityMask = kAllocationGranularity - 1;
static const size_t kBucketShift = (kAllocationGranularity == 8) ? 3 : 2;

// Underlying partition storage pages are a power-of-two size. It is typical
// for a partition page to be based on multiple system pages. Most references to
// "page" refer to partition pages.
// We also have the concept of "super pages" -- these are the underlying system
// allocations we make. Super pages contain multiple partition pages inside them
// and include space for a small amount of metadata per partition page.
// Inside super pages, we store "slot spans". A slot span is a continguous range
// of one or more partition pages that stores allocations of the same size.
// Slot span sizes are adjusted depending on the allocation size, to make sure
// the packing does not lead to unused (wasted) space at the end of the last
// system page of the span. For our current max slot span size of 64k and other
// constant values, we pack _all_ PartitionRootGeneric::Alloc() sizes perfectly
// up against the end of a system page.
static const size_t kPartitionPageShift = 14;  // 16KB
static const size_t kPartitionPageSize = 1 << kPartitionPageShift;
static const size_t kPartitionPageOffsetMask = kPartitionPageSize - 1;
static const size_t kPartitionPageBaseMask = ~kPartitionPageOffsetMask;
static const size_t kMaxPartitionPagesPerSlotSpan = 4;

// To avoid fragmentation via never-used freelist entries, we hand out partition
// freelist sections gradually, in units of the dominant system page size.
// What we're actually doing is avoiding filling the full partition page (16 KB)
// with freelist pointers right away. Writing freelist pointers will fault and
// dirty a private page, which is very wasteful if we never actually store
// objects there.
static const size_t kNumSystemPagesPerPartitionPage =
    kPartitionPageSize / kSystemPageSize;
static const size_t kMaxSystemPagesPerSlotSpan =
    kNumSystemPagesPerPartitionPage * kMaxPartitionPagesPerSlotSpan;

// We reserve virtual address space in 2MB chunks (aligned to 2MB as well).
// These chunks are called "super pages". We do this so that we can store
// metadata in the first few pages of each 2MB aligned section. This leads to
// a very fast free(). We specifically choose 2MB because this virtual address
// block represents a full but single PTE allocation on ARM, ia32 and x64.
//
// The layout of the super page is as follows. The sizes below are the same
// for 32 bit and 64 bit.
//
//   | Guard page (4KB)    |
//   | Metadata page (4KB) |
//   | Guard pages (8KB)   |
//   | Slot span           |
//   | Slot span           |
//   | ...                 |
//   | Slot span           |
//   | Guard page (4KB)    |
//
//   - Each slot span is a contiguous range of one or more PartitionPages.
//   - The metadata page has the following format. Note that the PartitionPage
//     that is not at the head of a slot span is "unused". In other words,
//     the metadata for the slot span is stored only in the first PartitionPage
//     of the slot span. Metadata accesses to other PartitionPages are
//     redirected to the first PartitionPage.
//
//     | SuperPageExtentEntry (32B)                 |
//     | PartitionPage of slot span 1 (32B, used)   |
//     | PartitionPage of slot span 1 (32B, unused) |
//     | PartitionPage of slot span 1 (32B, unused) |
//     | PartitionPage of slot span 2 (32B, used)   |
//     | PartitionPage of slot span 3 (32B, used)   |
//     | ...                                        |
//     | PartitionPage of slot span N (32B, unused) |
//
// A direct mapped page has a similar layout to fake it looking like a super
// page:
//
//     | Guard page (4KB)     |
//     | Metadata page (4KB)  |
//     | Guard pages (8KB)    |
//     | Direct mapped object |
//     | Guard page (4KB)     |
//
//    - The metadata page has the following layout:
//
//     | SuperPageExtentEntry (32B)    |
//     | PartitionPage (32B)           |
//     | PartitionBucket (32B)         |
//     | PartitionDirectMapExtent (8B) |
static const size_t kSuperPageShift = 21;  // 2MB
static const size_t kSuperPageSize = 1 << kSuperPageShift;
static const size_t kSuperPageOffsetMask = kSuperPageSize - 1;
static const size_t kSuperPageBaseMask = ~kSuperPageOffsetMask;
static const size_t kNumPartitionPagesPerSuperPage =
    kSuperPageSize / kPartitionPageSize;

static const size_t kPageMetadataShift = 5;  // 32 bytes per partition page.
static const size_t kPageMetadataSize = 1 << kPageMetadataShift;

// The following kGeneric* constants apply to the generic variants of the API.
// The "order" of an allocation is closely related to the power-of-two size of
// the allocation. More precisely, the order is the bit index of the
// most-significant-bit in the allocation size, where the bit numbers starts
// at index 1 for the least-significant-bit.
// In terms of allocation sizes, order 0 covers 0, order 1 covers 1, order 2
// covers 2->3, order 3 covers 4->7, order 4 covers 8->15.
static const size_t kGenericMinBucketedOrder = 4;  // 8 bytes.
static const size_t kGenericMaxBucketedOrder =
    20;  // Largest bucketed order is 1<<(20-1) (storing 512KB -> almost 1MB)
static const size_t kGenericNumBucketedOrders =
    (kGenericMaxBucketedOrder - kGenericMinBucketedOrder) + 1;
// Eight buckets per order (for the higher orders), e.g. order 8 is 128, 144,
// 160, ..., 240:
static const size_t kGenericNumBucketsPerOrderBits = 3;
static const size_t kGenericNumBucketsPerOrder =
    1 << kGenericNumBucketsPerOrderBits;
static const size_t kGenericNumBuckets =
    kGenericNumBucketedOrders * kGenericNumBucketsPerOrder;
static const size_t kGenericSmallestBucket = 1
                                             << (kGenericMinBucketedOrder - 1);
static const size_t kGenericMaxBucketSpacing =
    1 << ((kGenericMaxBucketedOrder - 1) - kGenericNumBucketsPerOrderBits);
static const size_t kGenericMaxBucketed =
    (1 << (kGenericMaxBucketedOrder - 1)) +
    ((kGenericNumBucketsPerOrder - 1) * kGenericMaxBucketSpacing);
static const size_t kGenericMinDirectMappedDownsize =
    kGenericMaxBucketed +
    1;  // Limit when downsizing a direct mapping using realloc().
static const size_t kGenericMaxDirectMapped = 1UL << 31;  // 2 GiB
static const size_t kBitsPerSizeT = sizeof(void*) * CHAR_BIT;

// Constants for the memory reclaim logic.
static const size_t kMaxFreeableSpans = 16;

// If the total size in bytes of allocated but not committed pages exceeds this
// value (probably it is a "out of virtual address space" crash),
// a special crash stack trace is generated at |partitionOutOfMemory|.
// This is to distinguish "out of virtual address space" from
// "out of physical memory" in crash reports.
static const size_t kReasonableSizeOfUnusedPages = 1024 * 1024 * 1024;  // 1GiB

#if DCHECK_IS_ON()
// These two byte values match tcmalloc.
static const unsigned char kUninitializedByte = 0xAB;
static const unsigned char kFreedByte = 0xCD;
static const size_t kCookieSize =
    16;  // Handles alignment up to XMM instructions on Intel.
static const unsigned char kCookieValue[kCookieSize] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xD0, 0x0D,
    0x13, 0x37, 0xF0, 0x05, 0xBA, 0x11, 0xAB, 0x1E};
#endif

class PartitionStatsDumper;
struct PartitionBucket;
struct PartitionRootBase;

struct PartitionFreelistEntry {
  PartitionFreelistEntry* next;
};

// Some notes on page states. A page can be in one of four major states:
// 1) Active.
// 2) Full.
// 3) Empty.
// 4) Decommitted.
// An active page has available free slots. A full page has no free slots. An
// empty page has no free slots, and a decommitted page is an empty page that
// had its backing memory released back to the system.
// There are two linked lists tracking the pages. The "active page" list is an
// approximation of a list of active pages. It is an approximation because
// full, empty and decommitted pages may briefly be present in the list until
// we next do a scan over it.
// The "empty page" list is an accurate list of pages which are either empty
// or decommitted.
//
// The significant page transitions are:
// - free() will detect when a full page has a slot free()'d and immediately
// return the page to the head of the active list.
// - free() will detect when a page is fully emptied. It _may_ add it to the
// empty list or it _may_ leave it on the active list until a future list scan.
// - malloc() _may_ scan the active page list in order to fulfil the request.
// If it does this, full, empty and decommitted pages encountered will be
// booted out of the active list. If there are no suitable active pages found,
// an empty or decommitted page (if one exists) will be pulled from the empty
// list on to the active list.
//
// TODO(ajwong): Evaluate if this should be named PartitionSlotSpanMetadata or
// similar. If so, all uses of the term "page" in comments, member variables,
// local variables, and documentation that refer to this concept should be
// updated.
struct PartitionPage {
  PartitionFreelistEntry* freelist_head;
  PartitionPage* next_page;
  PartitionBucket* bucket;
  // Deliberately signed, 0 for empty or decommitted page, -n for full pages:
  int16_t num_allocated_slots;
  uint16_t num_unprovisioned_slots;
  uint16_t page_offset;
  int16_t empty_cache_index;  // -1 if not in the empty cache.

  // Public API

  // Note the matching Alloc() functions are in PartitionPage.
  BASE_EXPORT NOINLINE void FreeSlowPath();
  ALWAYS_INLINE void Free(void* ptr);

  // Pointer manipulation functions. These must be static as the input |page|
  // pointer may be the result of an offset calculation and therefore cannot
  // be trusted. The objective of these functions is to sanitize this input.
  ALWAYS_INLINE static void* ToPointer(const PartitionPage* page);
  ALWAYS_INLINE static PartitionPage* FromPointerNoAlignmentCheck(void* ptr);
  ALWAYS_INLINE static PartitionPage* FromPointer(void* ptr);
  ALWAYS_INLINE static bool IsPointerValid(PartitionPage* page);

  ALWAYS_INLINE const size_t* get_raw_size_ptr() const;
  ALWAYS_INLINE size_t* get_raw_size_ptr() {
    return const_cast<size_t*>(
        const_cast<const PartitionPage*>(this)->get_raw_size_ptr());
  }

  ALWAYS_INLINE size_t get_raw_size() const;
};
static_assert(sizeof(PartitionPage) <= kPageMetadataSize,
              "PartitionPage must be able to fit in a metadata slot");

struct PartitionBucket {
  // Accessed most in hot path => goes first.
  PartitionPage* active_pages_head;

  PartitionPage* empty_pages_head;
  PartitionPage* decommitted_pages_head;
  uint32_t slot_size;
  unsigned num_system_pages_per_slot_span : 8;
  unsigned num_full_pages : 24;

  // Public API.

  // Note the matching Free() functions are in PartitionPage.
  BASE_EXPORT void* Alloc(PartitionRootBase* root, int flags, size_t size);
  BASE_EXPORT NOINLINE void* SlowPathAlloc(PartitionRootBase* root,
                                           int flags,
                                           size_t size);

  ALWAYS_INLINE bool is_direct_mapped() const {
    return !num_system_pages_per_slot_span;
  }
  ALWAYS_INLINE size_t get_bytes_per_span() const {
    // TODO(ajwong): Chagne to CheckedMul. https://crbug.com/787153
    return num_system_pages_per_slot_span * kSystemPageSize;
  }
  ALWAYS_INLINE uint16_t get_slots_per_span() const {
    // TODO(ajwong): Chagne to CheckedMul. https://crbug.com/787153
    return static_cast<uint16_t>(get_bytes_per_span() / slot_size);
  }
};

// An "extent" is a span of consecutive superpages. We link to the partition's
// next extent (if there is one) to the very start of a superpage's metadata
// area.
struct PartitionSuperPageExtentEntry {
  PartitionRootBase* root;
  char* super_page_base;
  char* super_pages_end;
  PartitionSuperPageExtentEntry* next;
};
static_assert(
    sizeof(PartitionSuperPageExtentEntry) <= kPageMetadataSize,
    "PartitionSuperPageExtentEntry must be able to fit in a metadata slot");

struct PartitionDirectMapExtent {
  PartitionDirectMapExtent* next_extent;
  PartitionDirectMapExtent* prev_extent;
  PartitionBucket* bucket;
  size_t map_size;  // Mapped size, not including guard pages and meta-data.
};

struct BASE_EXPORT PartitionRootBase {
  PartitionRootBase();
  virtual ~PartitionRootBase();
  size_t total_size_of_committed_pages = 0;
  size_t total_size_of_super_pages = 0;
  size_t total_size_of_direct_mapped_pages = 0;
  // Invariant: total_size_of_committed_pages <=
  //                total_size_of_super_pages +
  //                total_size_of_direct_mapped_pages.
  unsigned num_buckets = 0;
  unsigned max_allocation = 0;
  bool initialized = false;
  char* next_super_page = nullptr;
  char* next_partition_page = nullptr;
  char* next_partition_page_end = nullptr;
  PartitionSuperPageExtentEntry* current_extent = nullptr;
  PartitionSuperPageExtentEntry* first_extent = nullptr;
  PartitionDirectMapExtent* direct_map_list = nullptr;
  PartitionPage* global_empty_page_ring[kMaxFreeableSpans] = {};
  int16_t global_empty_page_ring_index = 0;
  uintptr_t inverted_self = 0;

  // Pubic API

  // gOomHandlingFunction is invoked when ParitionAlloc hits OutOfMemory.
  static void (*gOomHandlingFunction)();
};

enum PartitionPurgeFlags {
  // Decommitting the ring list of empty pages is reasonably fast.
  PartitionPurgeDecommitEmptyPages = 1 << 0,
  // Discarding unused system pages is slower, because it involves walking all
  // freelists in all active partition pages of all buckets >= system page
  // size. It often frees a similar amount of memory to decommitting the empty
  // pages, though.
  PartitionPurgeDiscardUnusedSystemPages = 1 << 1,
};

// Never instantiate a PartitionRoot directly, instead use PartitionAlloc.
struct BASE_EXPORT PartitionRoot : public PartitionRootBase {
  PartitionRoot();
  ~PartitionRoot() override;
  // This references the buckets OFF the edge of this struct. All uses of
  // PartitionRoot must have the bucket array come right after.
  //
  // The PartitionAlloc templated class ensures the following is correct.
  ALWAYS_INLINE PartitionBucket* buckets() {
    return reinterpret_cast<PartitionBucket*>(this + 1);
  }
  ALWAYS_INLINE const PartitionBucket* buckets() const {
    return reinterpret_cast<const PartitionBucket*>(this + 1);
  }

  void Init(size_t num_buckets, size_t max_allocation);

  ALWAYS_INLINE void* Alloc(size_t size, const char* type_name);

  void PurgeMemory(int flags);

  void DumpStats(const char* partition_name,
                 bool is_light_dump,
                 PartitionStatsDumper* dumper);
};

// Never instantiate a PartitionRootGeneric directly, instead use
// PartitionAllocatorGeneric.
struct BASE_EXPORT PartitionRootGeneric : public PartitionRootBase {
  PartitionRootGeneric();
  ~PartitionRootGeneric() override;
  subtle::SpinLock lock;
  // Some pre-computed constants.
  size_t order_index_shifts[kBitsPerSizeT + 1] = {};
  size_t order_sub_index_masks[kBitsPerSizeT + 1] = {};
  // The bucket lookup table lets us map a size_t to a bucket quickly.
  // The trailing +1 caters for the overflow case for very large allocation
  // sizes.  It is one flat array instead of a 2D array because in the 2D
  // world, we'd need to index array[blah][max+1] which risks undefined
  // behavior.
  PartitionBucket*
      bucket_lookups[((kBitsPerSizeT + 1) * kGenericNumBucketsPerOrder) + 1] =
          {};
  PartitionBucket buckets[kGenericNumBuckets] = {};

  // Public API.
  void Init();

  ALWAYS_INLINE void* Alloc(size_t size, const char* type_name);
  ALWAYS_INLINE void Free(void* ptr);

  NOINLINE void* Realloc(void* ptr, size_t new_size, const char* type_name);

  ALWAYS_INLINE size_t ActualSize(size_t size);

  void PurgeMemory(int flags);

  void DumpStats(const char* partition_name,
                 bool is_light_dump,
                 PartitionStatsDumper* partition_stats_dumper);
};

// Flags for PartitionAllocGenericFlags.
enum PartitionAllocFlags {
  PartitionAllocReturnNull = 1 << 0,
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

BASE_EXPORT void PartitionAllocGlobalInit(void (*oom_handling_function)());

class BASE_EXPORT PartitionAllocHooks {
 public:
  typedef void AllocationHook(void* address, size_t, const char* type_name);
  typedef void FreeHook(void* address);

  // To unhook, call Set*Hook with nullptr.
  static void SetAllocationHook(AllocationHook* hook) {
    // Chained allocation hooks are not supported. Registering a non-null
    // hook when a non-null hook is already registered indicates somebody is
    // trying to overwrite a hook.
    DCHECK(!hook || !allocation_hook_) << "Overwriting allocation hook";
    allocation_hook_ = hook;
  }
  static void SetFreeHook(FreeHook* hook) {
    DCHECK(!hook || !free_hook_) << "Overwriting free hook";
    free_hook_ = hook;
  }

  static void AllocationHookIfEnabled(void* address,
                                      size_t size,
                                      const char* type_name) {
    AllocationHook* hook = allocation_hook_;
    if (UNLIKELY(hook != nullptr))
      hook(address, size, type_name);
  }

  static void FreeHookIfEnabled(void* address) {
    FreeHook* hook = free_hook_;
    if (UNLIKELY(hook != nullptr))
      hook(address);
  }

  static void ReallocHookIfEnabled(void* old_address,
                                   void* new_address,
                                   size_t size,
                                   const char* type_name) {
    // Report a reallocation as a free followed by an allocation.
    AllocationHook* allocation_hook = allocation_hook_;
    FreeHook* free_hook = free_hook_;
    if (UNLIKELY(allocation_hook && free_hook)) {
      free_hook(old_address);
      allocation_hook(new_address, size, type_name);
    }
  }

 private:
  // Pointers to hook functions that PartitionAlloc will call on allocation and
  // free if the pointers are non-null.
  static AllocationHook* allocation_hook_;
  static FreeHook* free_hook_;
};

ALWAYS_INLINE PartitionFreelistEntry* PartitionFreelistMask(
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

ALWAYS_INLINE size_t PartitionCookieSizeAdjustAdd(size_t size) {
#if DCHECK_IS_ON()
  // Add space for cookies, checking for integer overflow. TODO(palmer):
  // Investigate the performance and code size implications of using
  // CheckedNumeric throughout PA.
  DCHECK(size + (2 * kCookieSize) > size);
  size += 2 * kCookieSize;
#endif
  return size;
}

ALWAYS_INLINE size_t PartitionCookieSizeAdjustSubtract(size_t size) {
#if DCHECK_IS_ON()
  // Remove space for cookies.
  DCHECK(size >= 2 * kCookieSize);
  size -= 2 * kCookieSize;
#endif
  return size;
}

ALWAYS_INLINE void* PartitionCookieFreePointerAdjust(void* ptr) {
#if DCHECK_IS_ON()
  // The value given to the application is actually just after the cookie.
  ptr = static_cast<char*>(ptr) - kCookieSize;
#endif
  return ptr;
}

ALWAYS_INLINE void PartitionCookieWriteValue(void* ptr) {
#if DCHECK_IS_ON()
  unsigned char* cookie_ptr = reinterpret_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < kCookieSize; ++i, ++cookie_ptr)
    *cookie_ptr = kCookieValue[i];
#endif
}

ALWAYS_INLINE void PartitionCookieCheckValue(void* ptr) {
#if DCHECK_IS_ON()
  unsigned char* cookie_ptr = reinterpret_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < kCookieSize; ++i, ++cookie_ptr)
    DCHECK(*cookie_ptr == kCookieValue[i]);
#endif
}

ALWAYS_INLINE char* PartitionSuperPageToMetadataArea(char* ptr) {
  uintptr_t pointer_as_uint = reinterpret_cast<uintptr_t>(ptr);
  DCHECK(!(pointer_as_uint & kSuperPageOffsetMask));
  // The metadata area is exactly one system page (the guard page) into the
  // super page.
  return reinterpret_cast<char*>(pointer_as_uint + kSystemPageSize);
}

ALWAYS_INLINE PartitionPage* PartitionPage::FromPointerNoAlignmentCheck(
    void* ptr) {
  uintptr_t pointer_as_uint = reinterpret_cast<uintptr_t>(ptr);
  char* super_page_ptr =
      reinterpret_cast<char*>(pointer_as_uint & kSuperPageBaseMask);
  uintptr_t partition_page_index =
      (pointer_as_uint & kSuperPageOffsetMask) >> kPartitionPageShift;
  // Index 0 is invalid because it is the metadata and guard area and
  // the last index is invalid because it is a guard page.
  DCHECK(partition_page_index);
  DCHECK(partition_page_index < kNumPartitionPagesPerSuperPage - 1);
  PartitionPage* page = reinterpret_cast<PartitionPage*>(
      PartitionSuperPageToMetadataArea(super_page_ptr) +
      (partition_page_index << kPageMetadataShift));
  // Partition pages in the same slot span can share the same page object.
  // Adjust for that.
  size_t delta = page->page_offset << kPageMetadataShift;
  page =
      reinterpret_cast<PartitionPage*>(reinterpret_cast<char*>(page) - delta);
  return page;
}

// Resturns start of the slot span for the PartitionPage.
ALWAYS_INLINE void* PartitionPage::ToPointer(const PartitionPage* page) {
  uintptr_t pointer_as_uint = reinterpret_cast<uintptr_t>(page);

  uintptr_t super_page_offset = (pointer_as_uint & kSuperPageOffsetMask);

  // A valid |page| must be past the first guard System page and within
  // the following metadata region.
  DCHECK(super_page_offset > kSystemPageSize);
  // Must be less than total metadata region.
  DCHECK(super_page_offset < kSystemPageSize + (kNumPartitionPagesPerSuperPage *
                                                kPageMetadataSize));
  uintptr_t partition_page_index =
      (super_page_offset - kSystemPageSize) >> kPageMetadataShift;
  // Index 0 is invalid because it is the superpage extent metadata and the
  // last index is invalid because the whole PartitionPage is set as guard
  // pages for the metadata region.
  DCHECK(partition_page_index);
  DCHECK(partition_page_index < kNumPartitionPagesPerSuperPage - 1);
  uintptr_t super_page_base = (pointer_as_uint & kSuperPageBaseMask);
  void* ret = reinterpret_cast<void*>(
      super_page_base + (partition_page_index << kPartitionPageShift));
  return ret;
}

ALWAYS_INLINE PartitionPage* PartitionPage::FromPointer(void* ptr) {
  PartitionPage* page = PartitionPage::FromPointerNoAlignmentCheck(ptr);
  // Checks that the pointer is a multiple of bucket size.
  DCHECK(!((reinterpret_cast<uintptr_t>(ptr) -
            reinterpret_cast<uintptr_t>(PartitionPage::ToPointer(page))) %
           page->bucket->slot_size));
  return page;
}

ALWAYS_INLINE const size_t* PartitionPage::get_raw_size_ptr() const {
  // For single-slot buckets which span more than one partition page, we
  // have some spare metadata space to store the raw allocation size. We
  // can use this to report better statistics.
  if (bucket->slot_size <= kMaxSystemPagesPerSlotSpan * kSystemPageSize)
    return nullptr;

  DCHECK((bucket->slot_size % kSystemPageSize) == 0);
  DCHECK(bucket->is_direct_mapped() || bucket->get_slots_per_span() == 1);

  const PartitionPage* the_next_page = this + 1;
  return reinterpret_cast<const size_t*>(&the_next_page->freelist_head);
}

ALWAYS_INLINE size_t PartitionPage::get_raw_size() const {
  const size_t* ptr = get_raw_size_ptr();
  if (UNLIKELY(ptr != nullptr))
    return *ptr;
  return 0;
}

ALWAYS_INLINE PartitionRootBase* PartitionPageToRoot(PartitionPage* page) {
  PartitionSuperPageExtentEntry* extent_entry =
      reinterpret_cast<PartitionSuperPageExtentEntry*>(
          reinterpret_cast<uintptr_t>(page) & kSystemPageBaseMask);
  return extent_entry->root;
}

ALWAYS_INLINE bool PartitionPage::IsPointerValid(PartitionPage* page) {
  PartitionRootBase* root = PartitionPageToRoot(page);
  return root->inverted_self == ~reinterpret_cast<uintptr_t>(root);
}

ALWAYS_INLINE void* PartitionBucket::Alloc(PartitionRootBase* root,
                                           int flags,
                                           size_t size) {
  PartitionPage* page = this->active_pages_head;
  // Check that this page is neither full nor freed.
  DCHECK(page->num_allocated_slots >= 0);
  void* ret = page->freelist_head;
  if (LIKELY(ret != 0)) {
    // If these DCHECKs fire, you probably corrupted memory.
    // TODO(palmer): See if we can afford to make this a CHECK.
    DCHECK(PartitionPage::IsPointerValid(page));
    // All large allocations must go through the slow path to correctly
    // update the size metadata.
    DCHECK(page->get_raw_size() == 0);
    PartitionFreelistEntry* new_head =
        PartitionFreelistMask(static_cast<PartitionFreelistEntry*>(ret)->next);
    page->freelist_head = new_head;
    page->num_allocated_slots++;
  } else {
    ret = this->SlowPathAlloc(root, flags, size);
    // TODO(palmer): See if we can afford to make this a CHECK.
    DCHECK(!ret ||
           PartitionPage::IsPointerValid(PartitionPage::FromPointer(ret)));
  }
#if DCHECK_IS_ON()
  if (!ret)
    return 0;
  // Fill the uninitialized pattern, and write the cookies.
  page = PartitionPage::FromPointer(ret);
  // TODO(ajwong): Can |page->bucket| ever not be |this|? If not, can this just
  // be this->slot_size?
  size_t new_slot_size = page->bucket->slot_size;
  size_t raw_size = page->get_raw_size();
  if (raw_size) {
    DCHECK(raw_size == size);
    new_slot_size = raw_size;
  }
  size_t no_cookie_size = PartitionCookieSizeAdjustSubtract(new_slot_size);
  char* char_ret = static_cast<char*>(ret);
  // The value given to the application is actually just after the cookie.
  ret = char_ret + kCookieSize;

  // Debug fill region kUninitializedByte and surround it with 2 cookies.
  PartitionCookieWriteValue(char_ret);
  memset(ret, kUninitializedByte, no_cookie_size);
  PartitionCookieWriteValue(char_ret + kCookieSize + no_cookie_size);
#endif
  return ret;
}

ALWAYS_INLINE void* PartitionRoot::Alloc(size_t size, const char* type_name) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  void* result = malloc(size);
  CHECK(result);
  return result;
#else
  size_t requested_size = size;
  size = PartitionCookieSizeAdjustAdd(size);
  DCHECK(this->initialized);
  size_t index = size >> kBucketShift;
  DCHECK(index < this->num_buckets);
  DCHECK(size == index << kBucketShift);
  PartitionBucket* bucket = &this->buckets()[index];
  void* result = bucket->Alloc(this, 0, size);
  PartitionAllocHooks::AllocationHookIfEnabled(result, requested_size,
                                               type_name);
  return result;
#endif  // defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
}

ALWAYS_INLINE void PartitionPage::Free(void* ptr) {
// If these asserts fire, you probably corrupted memory.
#if DCHECK_IS_ON()
  size_t slot_size = this->bucket->slot_size;
  size_t raw_size = get_raw_size();
  if (raw_size)
    slot_size = raw_size;
  PartitionCookieCheckValue(ptr);
  PartitionCookieCheckValue(reinterpret_cast<char*>(ptr) + slot_size -
                            kCookieSize);
  memset(ptr, kFreedByte, slot_size);
#endif
  DCHECK(this->num_allocated_slots);
  // TODO(palmer): See if we can afford to make this a CHECK.
  DCHECK(!freelist_head || PartitionPage::IsPointerValid(
                               PartitionPage::FromPointer(freelist_head)));
  CHECK(ptr != freelist_head);  // Catches an immediate double free.
  // Look for double free one level deeper in debug.
  DCHECK(!freelist_head || ptr != PartitionFreelistMask(freelist_head->next));
  PartitionFreelistEntry* entry = static_cast<PartitionFreelistEntry*>(ptr);
  entry->next = PartitionFreelistMask(freelist_head);
  freelist_head = entry;
  --this->num_allocated_slots;
  if (UNLIKELY(this->num_allocated_slots <= 0)) {
    FreeSlowPath();
  } else {
    // All single-slot allocations must go through the slow path to
    // correctly update the size metadata.
    DCHECK(get_raw_size() == 0);
  }
}

ALWAYS_INLINE void PartitionFree(void* ptr) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  free(ptr);
#else
  // TODO(palmer): Check ptr alignment before continuing. Shall we do the check
  // inside PartitionCookieFreePointerAdjust?
  PartitionAllocHooks::FreeHookIfEnabled(ptr);
  ptr = PartitionCookieFreePointerAdjust(ptr);
  PartitionPage* page = PartitionPage::FromPointer(ptr);
  // TODO(palmer): See if we can afford to make this a CHECK.
  DCHECK(PartitionPage::IsPointerValid(page));
  page->Free(ptr);
#endif
}

ALWAYS_INLINE PartitionBucket* PartitionGenericSizeToBucket(
    PartitionRootGeneric* root,
    size_t size) {
  size_t order = kBitsPerSizeT - bits::CountLeadingZeroBitsSizeT(size);
  // The order index is simply the next few bits after the most significant bit.
  size_t order_index = (size >> root->order_index_shifts[order]) &
                       (kGenericNumBucketsPerOrder - 1);
  // And if the remaining bits are non-zero we must bump the bucket up.
  size_t sub_order_index = size & root->order_sub_index_masks[order];
  PartitionBucket* bucket =
      root->bucket_lookups[(order << kGenericNumBucketsPerOrderBits) +
                           order_index + !!sub_order_index];
  DCHECK(!bucket->slot_size || bucket->slot_size >= size);
  DCHECK(!(bucket->slot_size % kGenericSmallestBucket));
  return bucket;
}

ALWAYS_INLINE void* PartitionAllocGenericFlags(PartitionRootGeneric* root,
                                               int flags,
                                               size_t size,
                                               const char* type_name) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  void* result = malloc(size);
  CHECK(result || flags & PartitionAllocReturnNull);
  return result;
#else
  DCHECK(root->initialized);
  size_t requested_size = size;
  size = PartitionCookieSizeAdjustAdd(size);
  PartitionBucket* bucket = PartitionGenericSizeToBucket(root, size);
  void* ret = nullptr;
  {
    subtle::SpinLock::Guard guard(root->lock);
    ret = bucket->Alloc(root, flags, size);
  }
  PartitionAllocHooks::AllocationHookIfEnabled(ret, requested_size, type_name);
  return ret;
#endif
}

ALWAYS_INLINE void* PartitionRootGeneric::Alloc(size_t size,
                                                const char* type_name) {
  return PartitionAllocGenericFlags(this, 0, size, type_name);
}

ALWAYS_INLINE void PartitionRootGeneric::Free(void* ptr) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  free(ptr);
#else
  DCHECK(this->initialized);

  if (UNLIKELY(!ptr))
    return;

  PartitionAllocHooks::FreeHookIfEnabled(ptr);
  ptr = PartitionCookieFreePointerAdjust(ptr);
  PartitionPage* page = PartitionPage::FromPointer(ptr);
  // TODO(palmer): See if we can afford to make this a CHECK.
  DCHECK(PartitionPage::IsPointerValid(page));
  {
    subtle::SpinLock::Guard guard(this->lock);
    page->Free(ptr);
  }
#endif
}

ALWAYS_INLINE size_t PartitionDirectMapSize(size_t size) {
  // Caller must check that the size is not above the kGenericMaxDirectMapped
  // limit before calling. This also guards against integer overflow in the
  // calculation here.
  DCHECK(size <= kGenericMaxDirectMapped);
  return (size + kSystemPageOffsetMask) & kSystemPageBaseMask;
}

ALWAYS_INLINE size_t PartitionRootGeneric::ActualSize(size_t size) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  return size;
#else
  DCHECK(this->initialized);
  size = PartitionCookieSizeAdjustAdd(size);
  PartitionBucket* bucket = PartitionGenericSizeToBucket(this, size);
  if (LIKELY(!bucket->is_direct_mapped())) {
    size = bucket->slot_size;
  } else if (size > kGenericMaxDirectMapped) {
    // Too large to allocate => return the size unchanged.
  } else {
    size = PartitionDirectMapSize(size);
  }
  return PartitionCookieSizeAdjustSubtract(size);
#endif
}

ALWAYS_INLINE bool PartitionAllocSupportsGetSize() {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  return false;
#else
  return true;
#endif
}

ALWAYS_INLINE size_t PartitionAllocGetSize(void* ptr) {
  // No need to lock here. Only |ptr| being freed by another thread could
  // cause trouble, and the caller is responsible for that not happening.
  DCHECK(PartitionAllocSupportsGetSize());
  ptr = PartitionCookieFreePointerAdjust(ptr);
  PartitionPage* page = PartitionPage::FromPointer(ptr);
  // TODO(palmer): See if we can afford to make this a CHECK.
  DCHECK(PartitionPage::IsPointerValid(page));
  size_t size = page->bucket->slot_size;
  return PartitionCookieSizeAdjustSubtract(size);
}

template <size_t N>
class SizeSpecificPartitionAllocator {
 public:
  SizeSpecificPartitionAllocator() {
    memset(actual_buckets_, 0,
           sizeof(PartitionBucket) * arraysize(actual_buckets_));
  }
  ~SizeSpecificPartitionAllocator() = default;
  static const size_t kMaxAllocation = N - kAllocationGranularity;
  static const size_t kNumBuckets = N / kAllocationGranularity;
  void init() { partition_root_.Init(kNumBuckets, kMaxAllocation); }
  ALWAYS_INLINE PartitionRoot* root() { return &partition_root_; }

 private:
  PartitionRoot partition_root_;
  PartitionBucket actual_buckets_[kNumBuckets];
};

class BASE_EXPORT PartitionAllocatorGeneric {
 public:
  PartitionAllocatorGeneric();
  ~PartitionAllocatorGeneric();

  void init() { partition_root_.Init(); }
  ALWAYS_INLINE PartitionRootGeneric* root() { return &partition_root_; }

 private:
  PartitionRootGeneric partition_root_;
};

BASE_EXPORT PartitionPage* GetSentinelPageForTesting();

}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_H_
