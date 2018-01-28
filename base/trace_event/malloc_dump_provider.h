// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_MALLOC_DUMP_PROVIDER_H_
#define BASE_TRACE_EVENT_MALLOC_DUMP_PROVIDER_H_

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/sharded_allocation_register.h"
#include "build/build_config.h"

#if defined(OS_LINUX) || defined(OS_ANDROID) || defined(OS_WIN) || \
    (defined(OS_MACOSX) && !defined(OS_IOS))
#define MALLOC_MEMORY_TRACING_SUPPORTED
#endif

namespace base {
namespace trace_event {

// Dump provider which collects process-wide memory stats.
class BASE_EXPORT MallocDumpProvider : public MemoryDumpProvider {
 public:
  // Name of the allocated_objects dump. Use this to declare suballocator dumps
  // from other dump providers.
  static const char kAllocatedObjects[];

  static MallocDumpProvider* GetInstance();

  // MemoryDumpProvider implementation.
  bool OnMemoryDump(const MemoryDumpArgs& args,
                    ProcessMemoryDump* pmd) override;

  void OnHeapProfilingEnabled(bool enabled) override;

  // For heap profiling.
  void InsertAllocation(void* address, size_t size);
  void RemoveAllocation(void* address);

  // Used by out-of-process heap-profiling. When malloc is profiled by an
  // external process, that process will be responsible for emitting metrics on
  // behalf of this one. Thus, MallocDumpProvider should not do anything.
  void EnableMetrics();
  void DisableMetrics();

 private:
  friend struct DefaultSingletonTraits<MallocDumpProvider>;

  MallocDumpProvider();
  ~MallocDumpProvider() override;

  // For heap profiling.
  ShardedAllocationRegister allocation_register_;

  // When in OnMemoryDump(), this contains the current thread ID.
  // This is to prevent re-entrancy in the heap profiler when the heap dump
  // generation is malloc/new-ing for its own bookkeeping data structures.
  PlatformThreadId tid_dumping_heap_;

  bool emit_metrics_on_memory_dump_ = true;
  base::Lock emit_metrics_on_memory_dump_lock_;

  DISALLOW_COPY_AND_ASSIGN(MallocDumpProvider);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_MALLOC_DUMP_PROVIDER_H_
