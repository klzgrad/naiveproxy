// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/malloc_dump_provider.h"

#include <stddef.h>

#include <unordered_map>

#include "base/allocator/allocator_extension.h"
#include "base/allocator/allocator_shim.h"
#include "base/allocator/features.h"
#include "base/debug/profiler.h"
#include "base/trace_event/heap_profiler_allocation_context.h"
#include "base/trace_event/heap_profiler_allocation_context_tracker.h"
#include "base/trace_event/heap_profiler_heap_dump_writer.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event_argument.h"
#include "build/build_config.h"

#if defined(OS_MACOSX)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#if defined(OS_WIN)
#include <windows.h>
#endif

namespace base {
namespace trace_event {

namespace {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)

using allocator::AllocatorDispatch;

void* HookAlloc(const AllocatorDispatch* self, size_t size, void* context) {
  const AllocatorDispatch* const next = self->next;
  void* ptr = next->alloc_function(next, size, context);
  if (ptr)
    MallocDumpProvider::GetInstance()->InsertAllocation(ptr, size);
  return ptr;
}

void* HookZeroInitAlloc(const AllocatorDispatch* self,
                        size_t n,
                        size_t size,
                        void* context) {
  const AllocatorDispatch* const next = self->next;
  void* ptr = next->alloc_zero_initialized_function(next, n, size, context);
  if (ptr)
    MallocDumpProvider::GetInstance()->InsertAllocation(ptr, n * size);
  return ptr;
}

void* HookAllocAligned(const AllocatorDispatch* self,
                       size_t alignment,
                       size_t size,
                       void* context) {
  const AllocatorDispatch* const next = self->next;
  void* ptr = next->alloc_aligned_function(next, alignment, size, context);
  if (ptr)
    MallocDumpProvider::GetInstance()->InsertAllocation(ptr, size);
  return ptr;
}

void* HookRealloc(const AllocatorDispatch* self,
                  void* address,
                  size_t size,
                  void* context) {
  const AllocatorDispatch* const next = self->next;
  void* ptr = next->realloc_function(next, address, size, context);
  MallocDumpProvider::GetInstance()->RemoveAllocation(address);
  if (size > 0)  // realloc(size == 0) means free().
    MallocDumpProvider::GetInstance()->InsertAllocation(ptr, size);
  return ptr;
}

void HookFree(const AllocatorDispatch* self, void* address, void* context) {
  if (address)
    MallocDumpProvider::GetInstance()->RemoveAllocation(address);
  const AllocatorDispatch* const next = self->next;
  next->free_function(next, address, context);
}

size_t HookGetSizeEstimate(const AllocatorDispatch* self,
                           void* address,
                           void* context) {
  const AllocatorDispatch* const next = self->next;
  return next->get_size_estimate_function(next, address, context);
}

unsigned HookBatchMalloc(const AllocatorDispatch* self,
                         size_t size,
                         void** results,
                         unsigned num_requested,
                         void* context) {
  const AllocatorDispatch* const next = self->next;
  unsigned count =
      next->batch_malloc_function(next, size, results, num_requested, context);
  for (unsigned i = 0; i < count; ++i) {
    MallocDumpProvider::GetInstance()->InsertAllocation(results[i], size);
  }
  return count;
}

void HookBatchFree(const AllocatorDispatch* self,
                   void** to_be_freed,
                   unsigned num_to_be_freed,
                   void* context) {
  const AllocatorDispatch* const next = self->next;
  for (unsigned i = 0; i < num_to_be_freed; ++i) {
    MallocDumpProvider::GetInstance()->RemoveAllocation(to_be_freed[i]);
  }
  next->batch_free_function(next, to_be_freed, num_to_be_freed, context);
}

void HookFreeDefiniteSize(const AllocatorDispatch* self,
                          void* ptr,
                          size_t size,
                          void* context) {
  if (ptr)
    MallocDumpProvider::GetInstance()->RemoveAllocation(ptr);
  const AllocatorDispatch* const next = self->next;
  next->free_definite_size_function(next, ptr, size, context);
}

AllocatorDispatch g_allocator_hooks = {
    &HookAlloc,            /* alloc_function */
    &HookZeroInitAlloc,    /* alloc_zero_initialized_function */
    &HookAllocAligned,     /* alloc_aligned_function */
    &HookRealloc,          /* realloc_function */
    &HookFree,             /* free_function */
    &HookGetSizeEstimate,  /* get_size_estimate_function */
    &HookBatchMalloc,      /* batch_malloc_function */
    &HookBatchFree,        /* batch_free_function */
    &HookFreeDefiniteSize, /* free_definite_size_function */
    nullptr,               /* next */
};
#endif  // BUILDFLAG(USE_ALLOCATOR_SHIM)

#if defined(OS_WIN)
// A structure containing some information about a given heap.
struct WinHeapInfo {
  size_t committed_size;
  size_t uncommitted_size;
  size_t allocated_size;
  size_t block_count;
};

// NOTE: crbug.com/665516
// Unfortunately, there is no safe way to collect information from secondary
// heaps due to limitations and racy nature of this piece of WinAPI.
void WinHeapMemoryDumpImpl(WinHeapInfo* crt_heap_info) {
#if defined(SYZYASAN)
  if (base::debug::IsBinaryInstrumented())
    return;
#endif

  // Iterate through whichever heap our CRT is using.
  HANDLE crt_heap = reinterpret_cast<HANDLE>(_get_heap_handle());
  ::HeapLock(crt_heap);
  PROCESS_HEAP_ENTRY heap_entry;
  heap_entry.lpData = nullptr;
  // Walk over all the entries in the main heap.
  while (::HeapWalk(crt_heap, &heap_entry) != FALSE) {
    if ((heap_entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
      crt_heap_info->allocated_size += heap_entry.cbData;
      crt_heap_info->block_count++;
    } else if ((heap_entry.wFlags & PROCESS_HEAP_REGION) != 0) {
      crt_heap_info->committed_size += heap_entry.Region.dwCommittedSize;
      crt_heap_info->uncommitted_size += heap_entry.Region.dwUnCommittedSize;
    }
  }
  CHECK(::HeapUnlock(crt_heap) == TRUE);
}
#endif  // defined(OS_WIN)
}  // namespace

// static
const char MallocDumpProvider::kAllocatedObjects[] = "malloc/allocated_objects";

// static
MallocDumpProvider* MallocDumpProvider::GetInstance() {
  return Singleton<MallocDumpProvider,
                   LeakySingletonTraits<MallocDumpProvider>>::get();
}

MallocDumpProvider::MallocDumpProvider()
    : tid_dumping_heap_(kInvalidThreadId) {}

MallocDumpProvider::~MallocDumpProvider() = default;

// Called at trace dump point time. Creates a snapshot the memory counters for
// the current process.
bool MallocDumpProvider::OnMemoryDump(const MemoryDumpArgs& args,
                                      ProcessMemoryDump* pmd) {
  {
    base::AutoLock auto_lock(emit_metrics_on_memory_dump_lock_);
    if (!emit_metrics_on_memory_dump_)
      return true;
  }

  size_t total_virtual_size = 0;
  size_t resident_size = 0;
  size_t allocated_objects_size = 0;
  size_t allocated_objects_count = 0;
#if defined(USE_TCMALLOC)
  bool res =
      allocator::GetNumericProperty("generic.heap_size", &total_virtual_size);
  DCHECK(res);
  res = allocator::GetNumericProperty("generic.total_physical_bytes",
                                      &resident_size);
  DCHECK(res);
  res = allocator::GetNumericProperty("generic.current_allocated_bytes",
                                      &allocated_objects_size);
  DCHECK(res);
#elif defined(OS_MACOSX) || defined(OS_IOS)
  malloc_statistics_t stats = {0};
  malloc_zone_statistics(nullptr, &stats);
  total_virtual_size = stats.size_allocated;
  allocated_objects_size = stats.size_in_use;

  // Resident size is approximated pretty well by stats.max_size_in_use.
  // However, on macOS, freed blocks are both resident and reusable, which is
  // semantically equivalent to deallocated. The implementation of libmalloc
  // will also only hold a fixed number of freed regions before actually
  // starting to deallocate them, so stats.max_size_in_use is also not
  // representative of the peak size. As a result, stats.max_size_in_use is
  // typically somewhere between actually resident [non-reusable] pages, and
  // peak size. This is not very useful, so we just use stats.size_in_use for
  // resident_size, even though it's an underestimate and fails to account for
  // fragmentation. See
  // https://bugs.chromium.org/p/chromium/issues/detail?id=695263#c1.
  resident_size = stats.size_in_use;
#elif defined(OS_WIN)
  // This is too expensive on Windows, crbug.com/780735.
  if (args.level_of_detail == MemoryDumpLevelOfDetail::DETAILED) {
    WinHeapInfo main_heap_info = {};
    WinHeapMemoryDumpImpl(&main_heap_info);
    total_virtual_size =
        main_heap_info.committed_size + main_heap_info.uncommitted_size;
    // Resident size is approximated with committed heap size. Note that it is
    // possible to do this with better accuracy on windows by intersecting the
    // working set with the virtual memory ranges occuipied by the heap. It's
    // not clear that this is worth it, as it's fairly expensive to do.
    resident_size = main_heap_info.committed_size;
    allocated_objects_size = main_heap_info.allocated_size;
    allocated_objects_count = main_heap_info.block_count;
  }
#elif defined(OS_FUCHSIA)
// TODO(fuchsia): Port, see https://crbug.com/706592.
#else
  struct mallinfo info = mallinfo();
  DCHECK_GE(info.arena + info.hblkhd, info.uordblks);

  // In case of Android's jemalloc |arena| is 0 and the outer pages size is
  // reported by |hblkhd|. In case of dlmalloc the total is given by
  // |arena| + |hblkhd|. For more details see link: http://goo.gl/fMR8lF.
  total_virtual_size = info.arena + info.hblkhd;
  resident_size = info.uordblks;

  // Total allocated space is given by |uordblks|.
  allocated_objects_size = info.uordblks;
#endif

  MemoryAllocatorDump* outer_dump = pmd->CreateAllocatorDump("malloc");
  outer_dump->AddScalar("virtual_size", MemoryAllocatorDump::kUnitsBytes,
                        total_virtual_size);
  outer_dump->AddScalar(MemoryAllocatorDump::kNameSize,
                        MemoryAllocatorDump::kUnitsBytes, resident_size);

  MemoryAllocatorDump* inner_dump = pmd->CreateAllocatorDump(kAllocatedObjects);
  inner_dump->AddScalar(MemoryAllocatorDump::kNameSize,
                        MemoryAllocatorDump::kUnitsBytes,
                        allocated_objects_size);
  if (allocated_objects_count != 0) {
    inner_dump->AddScalar(MemoryAllocatorDump::kNameObjectCount,
                          MemoryAllocatorDump::kUnitsObjects,
                          allocated_objects_count);
  }

  if (resident_size > allocated_objects_size) {
    // Explicitly specify why is extra memory resident. In tcmalloc it accounts
    // for free lists and caches. In mac and ios it accounts for the
    // fragmentation and metadata.
    MemoryAllocatorDump* other_dump =
        pmd->CreateAllocatorDump("malloc/metadata_fragmentation_caches");
    other_dump->AddScalar(MemoryAllocatorDump::kNameSize,
                          MemoryAllocatorDump::kUnitsBytes,
                          resident_size - allocated_objects_size);
  }

  // Heap profiler dumps.
  if (!allocation_register_.is_enabled())
    return true;

  tid_dumping_heap_ = PlatformThread::CurrentId();
  // At this point the Insert/RemoveAllocation hooks will ignore this thread.
  // Enclosing all the temporary data structures in a scope, so that the heap
  // profiler does not see unbalanced malloc/free calls from these containers.
  {
    TraceEventMemoryOverhead overhead;
    std::unordered_map<AllocationContext, AllocationMetrics> metrics_by_context;
    if (AllocationContextTracker::capture_mode() !=
        AllocationContextTracker::CaptureMode::DISABLED) {
      ShardedAllocationRegister::OutputMetrics shim_metrics =
          allocation_register_.UpdateAndReturnsMetrics(metrics_by_context);

      // Aggregate data for objects allocated through the shim.
      inner_dump->AddScalar("shim_allocated_objects_size",
                            MemoryAllocatorDump::kUnitsBytes,
                            shim_metrics.size);
      inner_dump->AddScalar("shim_allocator_object_count",
                            MemoryAllocatorDump::kUnitsObjects,
                            shim_metrics.count);
    }
    allocation_register_.EstimateTraceMemoryOverhead(&overhead);

    pmd->DumpHeapUsage(metrics_by_context, overhead, "malloc");
  }
  tid_dumping_heap_ = kInvalidThreadId;

  return true;
}

void MallocDumpProvider::OnHeapProfilingEnabled(bool enabled) {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  if (enabled) {
    allocation_register_.SetEnabled();
    allocator::InsertAllocatorDispatch(&g_allocator_hooks);
  } else {
    allocation_register_.SetDisabled();
  }
#endif
}

void MallocDumpProvider::InsertAllocation(void* address, size_t size) {
  // CurrentId() can be a slow operation (crbug.com/497226). This apparently
  // redundant condition short circuits the CurrentID() calls when unnecessary.
  if (tid_dumping_heap_ != kInvalidThreadId &&
      tid_dumping_heap_ == PlatformThread::CurrentId())
    return;

  // AllocationContextTracker will return nullptr when called re-reentrantly.
  // This is the case of GetInstanceForCurrentThread() being called for the
  // first time, which causes a new() inside the tracker which re-enters the
  // heap profiler, in which case we just want to early out.
  auto* tracker = AllocationContextTracker::GetInstanceForCurrentThread();
  if (!tracker)
    return;

  AllocationContext context;
  if (!tracker->GetContextSnapshot(&context))
    return;

  if (!allocation_register_.is_enabled())
    return;

  allocation_register_.Insert(address, size, context);
}

void MallocDumpProvider::RemoveAllocation(void* address) {
  // No re-entrancy is expected here as none of the calls below should
  // cause a free()-s (|allocation_register_| does its own heap management).
  if (tid_dumping_heap_ != kInvalidThreadId &&
      tid_dumping_heap_ == PlatformThread::CurrentId())
    return;
  if (!allocation_register_.is_enabled())
    return;
  allocation_register_.Remove(address);
}

void MallocDumpProvider::EnableMetrics() {
  base::AutoLock auto_lock(emit_metrics_on_memory_dump_lock_);
  emit_metrics_on_memory_dump_ = true;
}

void MallocDumpProvider::DisableMetrics() {
  base::AutoLock auto_lock(emit_metrics_on_memory_dump_lock_);
  emit_metrics_on_memory_dump_ = false;
}

}  // namespace trace_event
}  // namespace base
