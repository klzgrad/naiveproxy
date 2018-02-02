// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/heap_profiler_allocation_register.h"

#include <windows.h>
#include <stddef.h>

#include "base/bits.h"
#include "base/logging.h"
#include "base/process/process_metrics.h"

namespace base {
namespace trace_event {
namespace internal {

namespace {
size_t GetGuardSize() {
  return GetPageSize();
}
}

void* AllocateGuardedVirtualMemory(size_t size) {
  size = bits::Align(size, GetPageSize());

  // Add space for a guard page at the end.
  size_t map_size = size + GetGuardSize();

  // Reserve the address space. This does not make the memory usable yet.
  void* addr = VirtualAlloc(nullptr, map_size, MEM_RESERVE, PAGE_NOACCESS);

  PCHECK(addr != nullptr);

  // Commit the non-guard pages as read-write memory.
  void* result = VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE);

  PCHECK(result != nullptr);

  // Mark the last page of the allocated address space as guard page. (NB: The
  // |PAGE_GUARD| flag is not the flag to use here, that flag can be used to
  // detect and intercept access to a certain memory region. Accessing a
  // |PAGE_NOACCESS| page will raise a general protection fault.) The
  // read/write accessible space is still at least |min_size| bytes.
  void* guard_addr =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + size);
  result = VirtualAlloc(guard_addr, GetGuardSize(), MEM_COMMIT, PAGE_NOACCESS);
  PCHECK(result != nullptr);

  return addr;
}

void FreeGuardedVirtualMemory(void* address, size_t allocated_size) {
  // For |VirtualFree|, the size passed with |MEM_RELEASE| must be 0. Windows
  // automatically frees the entire region that was reserved by the
  // |VirtualAlloc| with flag |MEM_RESERVE|.
  VirtualFree(address, 0, MEM_RELEASE);
}

}  // namespace internal
}  // namespace trace_event
}  // namespace base
