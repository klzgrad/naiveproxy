// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_SHARDED_ALLOCATION_REGISTER_H_
#define BASE_TRACE_EVENT_SHARDED_ALLOCATION_REGISTER_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/atomicops.h"
#include "base/base_export.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/heap_profiler_allocation_register.h"

namespace base {
namespace trace_event {

class TraceEventMemoryOverhead;

// This container holds allocations, and context for each allocation [in the
// form of a back trace].
// This container is thread-safe.
class BASE_EXPORT ShardedAllocationRegister {
 public:
  using MetricsMap = std::unordered_map<AllocationContext, AllocationMetrics>;

  struct OutputMetrics {
    // Total size of allocated objects.
    size_t size;
    // Total count of allocated objects.
    size_t count;
  };

  ShardedAllocationRegister();

  // This class must be enabled before calling Insert() or Remove(). Once the
  // class is enabled, it's okay if Insert() or Remove() is called [due to
  // races] after the class is disabled.
  void SetEnabled();
  void SetDisabled();
  bool is_enabled() const { return !!base::subtle::Acquire_Load(&enabled_); }

  ~ShardedAllocationRegister();

  // Inserts allocation details into the container. If the address was present
  // already, its details are updated. |address| must not be null.
  //
  // Returns true if an insert occurred. Inserts may fail because the table
  // is full.
  bool Insert(const void* address,
              size_t size,
              const AllocationContext& context);

  // Removes the address from the container if it is present. It is ok to call
  // this with a null pointer.
  void Remove(const void* address);

  // Finds allocation for the address and fills |out_allocation|.
  bool Get(const void* address,
           AllocationRegister::Allocation* out_allocation) const;

  // Estimates memory overhead including |sizeof(AllocationRegister)|.
  void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead) const;

  // Updates |map| with all allocated objects and their statistics.
  // Returns aggregate statistics.
  OutputMetrics UpdateAndReturnsMetrics(MetricsMap& map) const;

 private:
  struct RegisterAndLock {
    RegisterAndLock();
    ~RegisterAndLock();
    AllocationRegister allocation_register;
    Lock lock;
  };
  std::unique_ptr<RegisterAndLock[]> allocation_registers_;

  // This member needs to be checked on every allocation and deallocation [fast
  // path] when heap profiling is enabled. Using a lock here causes significant
  // contention.
  base::subtle::Atomic32 enabled_;

  DISALLOW_COPY_AND_ASSIGN(ShardedAllocationRegister);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_SHARDED_ALLOCATION_REGISTER_H_
