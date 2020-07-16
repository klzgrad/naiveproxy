// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ROOT_BASE_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ROOT_BASE_H_

#include "base/allocator/partition_allocator/page_allocator.h"
#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/allocator/partition_allocator/partition_alloc_forward.h"
#include "base/allocator/partition_allocator/partition_bucket.h"
#include "base/allocator/partition_allocator/partition_direct_map_extent.h"
#include "base/allocator/partition_allocator/partition_page.h"
#include "base/allocator/partition_allocator/spin_lock.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "build/build_config.h"

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
template <bool thread_safety>
struct PartitionSuperPageExtentEntry {
  PartitionRootBase<thread_safety>* root;
  char* super_page_base;
  char* super_pages_end;
  PartitionSuperPageExtentEntry<thread_safety>* next;
};
static_assert(
    sizeof(PartitionSuperPageExtentEntry<ThreadSafe>) <= kPageMetadataSize,
    "PartitionSuperPageExtentEntry must be able to fit in a metadata slot");

// g_oom_handling_function is invoked when PartitionAlloc hits OutOfMemory.
static OomFunction g_oom_handling_function = nullptr;

template <bool thread_safety>
struct BASE_EXPORT PartitionRootBase {
  using Page = PartitionPage<thread_safety>;
  using Bucket = PartitionBucket<thread_safety>;
  using ScopedGuard = internal::ScopedGuard<thread_safety>;

  PartitionRootBase();
  virtual ~PartitionRootBase();
  MaybeSpinLock<thread_safety> lock_;
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
  PartitionSuperPageExtentEntry<thread_safety>* current_extent = nullptr;
  PartitionSuperPageExtentEntry<thread_safety>* first_extent = nullptr;
  PartitionDirectMapExtent<thread_safety>* direct_map_list = nullptr;
  Page* global_empty_page_ring[kMaxFreeableSpans] = {};
  int16_t global_empty_page_ring_index = 0;
  uintptr_t inverted_self = 0;

  // Public API

  // Allocates out of the given bucket. Properly, this function should probably
  // be in PartitionBucket, but because the implementation needs to be inlined
  // for performance, and because it needs to inspect PartitionPage,
  // it becomes impossible to have it in PartitionBucket as this causes a
  // cyclical dependency on PartitionPage function implementations.
  //
  // Moving it a layer lower couples PartitionRootBase and PartitionBucket, but
  // preserves the layering of the includes.
  //
  // Note the matching Free() functions are in PartitionPage.
  ALWAYS_INLINE void* AllocFromBucket(Bucket* bucket, int flags, size_t size)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  ALWAYS_INLINE void Free(void* ptr);

  ALWAYS_INLINE static bool IsValidPage(Page* page);
  ALWAYS_INLINE static PartitionRootBase* FromPage(Page* page);

  ALWAYS_INLINE void IncreaseCommittedPages(size_t len);
  ALWAYS_INLINE void DecreaseCommittedPages(size_t len);
  ALWAYS_INLINE void DecommitSystemPages(void* address, size_t length)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  ALWAYS_INLINE void RecommitSystemPages(void* address, size_t length)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Frees memory from this partition, if possible, by decommitting pages.
  // |flags| is an OR of base::PartitionPurgeFlags.
  virtual void PurgeMemory(int flags) = 0;
  NOINLINE void OutOfMemory(size_t size);

 protected:
  void DecommitEmptyPages() EXCLUSIVE_LOCKS_REQUIRED(lock_);
};

template <bool thread_safety>
ALWAYS_INLINE void* PartitionRootBase<thread_safety>::AllocFromBucket(
    Bucket* bucket,
    int flags,
    size_t size) {
  bool zero_fill = flags & PartitionAllocZeroFill;
  bool is_already_zeroed = false;

  Page* page = bucket->active_pages_head;
  // Check that this page is neither full nor freed.
  DCHECK(page);
  DCHECK(page->num_allocated_slots >= 0);
  void* ret = page->freelist_head;
  if (LIKELY(ret != 0)) {
    // If these DCHECKs fire, you probably corrupted memory. TODO(palmer): See
    // if we can afford to make these CHECKs.
    DCHECK(IsValidPage(page));

    // All large allocations must go through the slow path to correctly update
    // the size metadata.
    DCHECK(page->get_raw_size() == 0);
    internal::PartitionFreelistEntry* new_head =
        internal::EncodedPartitionFreelistEntry::Decode(
            page->freelist_head->next);
    page->freelist_head = new_head;
    page->num_allocated_slots++;
  } else {
    ret = bucket->SlowPathAlloc(this, flags, size, &is_already_zeroed);
    // TODO(palmer): See if we can afford to make this a CHECK.
    DCHECK(!ret || IsValidPage(Page::FromPointer(ret)));
  }

#if DCHECK_IS_ON()
  if (!ret) {
    return nullptr;
  }

  page = Page::FromPointer(ret);
  // TODO(ajwong): Can |page->bucket| ever not be |this|? If not, can this just
  // be bucket->slot_size?
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

  // Fill the region kUninitializedByte or 0, and surround it with 2 cookies.
  PartitionCookieWriteValue(char_ret);
  if (!zero_fill) {
    memset(ret, kUninitializedByte, no_cookie_size);
  } else if (!is_already_zeroed) {
    memset(ret, 0, no_cookie_size);
  }
  PartitionCookieWriteValue(char_ret + kCookieSize + no_cookie_size);
#else
  if (ret && zero_fill && !is_already_zeroed) {
    memset(ret, 0, size);
  }
#endif

  return ret;
}

template <bool thread_safety>
ALWAYS_INLINE void PartitionRootBase<thread_safety>::Free(void* ptr) {
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
  free(ptr);
#else
  DCHECK(initialized);

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
  DCHECK(IsValidPage(page));
  internal::DeferredUnmap deferred_unmap;
  {
    ScopedGuard guard{lock_};
    deferred_unmap = page->Free(ptr);
  }
  deferred_unmap.Run();
#endif
}

template <bool thread_safety>
ALWAYS_INLINE bool PartitionRootBase<thread_safety>::IsValidPage(Page* page) {
  PartitionRootBase* root = PartitionRootBase::FromPage(page);
  return root->inverted_self == ~reinterpret_cast<uintptr_t>(root);
}

template <bool thread_safety>
ALWAYS_INLINE PartitionRootBase<thread_safety>*
PartitionRootBase<thread_safety>::FromPage(Page* page) {
  auto* extent_entry =
      reinterpret_cast<PartitionSuperPageExtentEntry<thread_safety>*>(
          reinterpret_cast<uintptr_t>(page) & kSystemPageBaseMask);
  return extent_entry->root;
}

template <bool thread_safety>
ALWAYS_INLINE void PartitionRootBase<thread_safety>::IncreaseCommittedPages(
    size_t len) {
  total_size_of_committed_pages += len;
  DCHECK(total_size_of_committed_pages <=
         total_size_of_super_pages + total_size_of_direct_mapped_pages);
}

template <bool thread_safety>
ALWAYS_INLINE void PartitionRootBase<thread_safety>::DecreaseCommittedPages(
    size_t len) {
  total_size_of_committed_pages -= len;
  DCHECK(total_size_of_committed_pages <=
         total_size_of_super_pages + total_size_of_direct_mapped_pages);
}

template <bool thread_safety>
ALWAYS_INLINE void PartitionRootBase<thread_safety>::DecommitSystemPages(
    void* address,
    size_t length) {
  ::base::DecommitSystemPages(address, length);
  DecreaseCommittedPages(length);
}

template <bool thread_safety>
ALWAYS_INLINE void PartitionRootBase<thread_safety>::RecommitSystemPages(
    void* address,
    size_t length) {
  CHECK(::base::RecommitSystemPages(address, length, PageReadWrite));
  IncreaseCommittedPages(length);
}

}  // namespace internal
}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ROOT_BASE_H_
