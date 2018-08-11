// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/protected_memory.h"

#include <windows.h>

#include <stdint.h>

#include "base/process/process_metrics.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"

namespace base {

namespace {

bool SetMemory(void* start, void* end, DWORD prot) {
  DCHECK(end > start);
  const uintptr_t page_mask = ~(base::GetPageSize() - 1);
  const uintptr_t page_start = reinterpret_cast<uintptr_t>(start) & page_mask;
  DWORD old_prot;
  return VirtualProtect(reinterpret_cast<void*>(page_start),
                        reinterpret_cast<uintptr_t>(end) - page_start, prot,
                        &old_prot) != 0;
}

}  // namespace

bool AutoWritableMemory::SetMemoryReadWrite(void* start, void* end) {
  return SetMemory(start, end, PAGE_READWRITE);
}

bool AutoWritableMemory::SetMemoryReadOnly(void* start, void* end) {
  return SetMemory(start, end, PAGE_READONLY);
}

void AssertMemoryIsReadOnly(const void* ptr) {
#if DCHECK_IS_ON()
  const uintptr_t page_mask = ~(base::GetPageSize() - 1);
  const uintptr_t page_start = reinterpret_cast<uintptr_t>(ptr) & page_mask;

  MEMORY_BASIC_INFORMATION info;
  SIZE_T result =
      VirtualQuery(reinterpret_cast<LPCVOID>(page_start), &info, sizeof(info));
  DCHECK_GT(result, 0U);
  DCHECK(info.Protect == PAGE_READONLY);
#endif  // DCHECK_IS_ON()
}

}  // namespace base
