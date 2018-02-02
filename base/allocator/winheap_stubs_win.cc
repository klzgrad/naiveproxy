// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This code should move into the default Windows shim once the win-specific
// allocation shim has been removed, and the generic shim has becaome the
// default.

#include "winheap_stubs_win.h"

#include <limits.h>
#include <malloc.h>
#include <new.h>
#include <windows.h>

namespace base {
namespace allocator {

bool g_is_win_shim_layer_initialized = false;

namespace {

const size_t kWindowsPageSize = 4096;
const size_t kMaxWindowsAllocation = INT_MAX - kWindowsPageSize;

inline HANDLE get_heap_handle() {
  return reinterpret_cast<HANDLE>(_get_heap_handle());
}

}  // namespace

void* WinHeapMalloc(size_t size) {
  if (size < kMaxWindowsAllocation)
    return HeapAlloc(get_heap_handle(), 0, size);
  return nullptr;
}

void WinHeapFree(void* ptr) {
  if (!ptr)
    return;

  HeapFree(get_heap_handle(), 0, ptr);
}

void* WinHeapRealloc(void* ptr, size_t size) {
  if (!ptr)
    return WinHeapMalloc(size);
  if (!size) {
    WinHeapFree(ptr);
    return nullptr;
  }
  if (size < kMaxWindowsAllocation)
    return HeapReAlloc(get_heap_handle(), 0, ptr, size);
  return nullptr;
}

size_t WinHeapGetSizeEstimate(void* ptr) {
  if (!ptr)
    return 0;

  // Get the user size of the allocation.
  size_t size = HeapSize(get_heap_handle(), 0, ptr);

  // Account for the 8-byte HEAP_HEADER preceding the block.
  size += 8;

// Round up to the nearest allocation granularity, which is 8 for
// 32 bit machines, and 16 for 64 bit machines.
#if defined(ARCH_CPU_64_BITS)
  const size_t kAllocationGranularity = 16;
#else
  const size_t kAllocationGranularity = 8;
#endif

  return (size + kAllocationGranularity - 1) & ~(kAllocationGranularity - 1);
}

// Call the new handler, if one has been set.
// Returns true on successfully calling the handler, false otherwise.
bool WinCallNewHandler(size_t size) {
#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS
#error "Exceptions in allocator shim are not supported!"
#endif  // defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS
  // Get the current new handler.
  _PNH nh = _query_new_handler();
  if (!nh)
    return false;
  // Since exceptions are disabled, we don't really know if new_handler
  // failed.  Assume it will abort if it fails.
  return nh(size) ? true : false;
}

}  // namespace allocator
}  // namespace base
