// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/allocator_extension.h"

#include "base/logging.h"

#if defined(USE_TCMALLOC)
#include "third_party/tcmalloc/chromium/src/gperftools/heap-profiler.h"
#include "third_party/tcmalloc/chromium/src/gperftools/malloc_extension.h"
#include "third_party/tcmalloc/chromium/src/gperftools/malloc_hook.h"
#endif

namespace base {
namespace allocator {

void ReleaseFreeMemory() {
#if defined(USE_TCMALLOC)
  ::MallocExtension::instance()->ReleaseFreeMemory();
#endif
}

bool GetNumericProperty(const char* name, size_t* value) {
#if defined(USE_TCMALLOC)
  return ::MallocExtension::instance()->GetNumericProperty(name, value);
#endif
  return false;
}

bool IsHeapProfilerRunning() {
#if defined(USE_TCMALLOC)
  return ::IsHeapProfilerRunning();
#endif
  return false;
}

void SetHooks(AllocHookFunc alloc_hook, FreeHookFunc free_hook) {
// TODO(sque): Use allocator shim layer instead.
#if defined(USE_TCMALLOC)
  // Make sure no hooks get overwritten.
  auto prev_alloc_hook = MallocHook::SetNewHook(alloc_hook);
  if (alloc_hook)
    DCHECK(!prev_alloc_hook);

  auto prev_free_hook = MallocHook::SetDeleteHook(free_hook);
  if (free_hook)
    DCHECK(!prev_free_hook);
#endif
}

int GetCallStack(void** stack, int max_stack_size) {
#if defined(USE_TCMALLOC)
  return MallocHook::GetCallerStackTrace(stack, max_stack_size, 0);
#endif
  return 0;
}

}  // namespace allocator
}  // namespace base
