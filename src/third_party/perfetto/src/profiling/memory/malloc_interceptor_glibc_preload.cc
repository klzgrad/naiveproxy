/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <malloc.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/heap_profile.h"
#include "src/profiling/memory/wrap_allocators.h"

namespace {
// AHeapProfile_registerHeap is guaranteed to be safe to call from global
// constructors.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wglobal-constructors"
uint32_t g_heap_id = AHeapProfile_registerHeap(AHeapInfo_create("libc.malloc"));
#pragma GCC diagnostic pop

bool IsPowerOfTwo(size_t v) {
  return (v != 0 && ((v & (v - 1)) == 0));
}

// The code inside the perfetto::profiling::wrap_ functions has been designed to
// avoid calling malloc/free functions, but, in some rare cases, this happens
// anyway inside glibc. The code belows prevents this reentrancy with a thread
// local variable, because:
// * It can cause infinite recursion.
// * If any lock is needed inside glibc, it can cause a deadlock.

// True if this thread is already inside heapprofd wrappers.
thread_local bool inside_wrapper = false;

class ScopedReentrancyPreventer {
 public:
  // Precondition: is_inside is false.
  ScopedReentrancyPreventer() { inside_wrapper = true; }
  ~ScopedReentrancyPreventer() { inside_wrapper = false; }
  static bool is_inside() { return inside_wrapper; }
};

}  // namespace

extern "C" {

// Prototypes for the C23 size-aware deallocation entry points. <stdlib.h>
// only declares them on glibc >= 2.41 and we may compile against older SDK
// headers, so we declare them ourselves.
void free_sized(void*, size_t);
void free_aligned_sized(void*, size_t, size_t);

// These are exported by GLibc to be used by functions overwriting malloc
// to call back to the real implementation. Note: glibc does not export
// `__libc_free_sized` / `__libc_free_aligned_sized` aliases for the C23
// size-aware free functions - the `__libc_*` naming convention is only used
// for the legacy entry points listed below. See
// https://elixir.bootlin.com/glibc/glibc-2.43/source/malloc/Versions for the
// canonical exported-symbol list.
extern void* __libc_malloc(size_t);
extern void __libc_free(void*);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_realloc(void*, size_t);
extern void* __libc_memalign(size_t, size_t);
extern void* __libc_pvalloc(size_t);
extern void* __libc_valloc(size_t);
extern void* __libc_reallocarray(void*, size_t, size_t);

#pragma GCC visibility push(default)

void* malloc(size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_malloc(size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_malloc(g_heap_id, __libc_malloc, size);
}

void free(void* ptr) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_free(ptr);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_free(g_heap_id, __libc_free, ptr);
}

// C23 size-aware free entry points. Per the spec the size/alignment hints
// are informational; upstream glibc itself ignores them and just forwards to
// free() - see
// https://elixir.bootlin.com/glibc/glibc-2.43/source/malloc/malloc.c#L3550.
// Calling free() from here resolves back to our own free() override above
// via the LD_PRELOAD chain, so the deallocation is still reported.
void free_sized(void* ptr, size_t /*size*/) {
  free(ptr);
}

void free_aligned_sized(void* ptr, size_t /*alignment*/, size_t /*size*/) {
  free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_calloc(nmemb, size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_calloc(g_heap_id, __libc_calloc, nmemb,
                                          size);
}

void* realloc(void* ptr, size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_realloc(ptr, size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_realloc(g_heap_id, __libc_realloc, ptr,
                                           size);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
  if (alignment % sizeof(void*) || !IsPowerOfTwo(alignment / sizeof(void*))) {
    return EINVAL;
  }

  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    void* alloc = __libc_memalign(alignment, size);
    if (!alloc) {
      return ENOMEM;
    }
    *memptr = alloc;
    return 0;
  }
  ScopedReentrancyPreventer p;

  void* alloc = perfetto::profiling::wrap_memalign(g_heap_id, __libc_memalign,
                                                   alignment, size);
  if (!alloc) {
    return ENOMEM;
  }
  *memptr = alloc;
  return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_memalign(alignment, size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_memalign(g_heap_id, __libc_memalign,
                                            alignment, size);
}

void* memalign(size_t alignment, size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_memalign(alignment, size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_memalign(g_heap_id, __libc_memalign,
                                            alignment, size);
}

void* pvalloc(size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_pvalloc(size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_pvalloc(g_heap_id, __libc_pvalloc, size);
}

void* valloc(size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_valloc(size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_valloc(g_heap_id, __libc_valloc, size);
}

void* reallocarray(void* ptr, size_t nmemb, size_t size) {
  if (PERFETTO_UNLIKELY(ScopedReentrancyPreventer::is_inside())) {
    return __libc_reallocarray(ptr, nmemb, size);
  }
  ScopedReentrancyPreventer p;

  return perfetto::profiling::wrap_reallocarray(g_heap_id, __libc_reallocarray,
                                                ptr, nmemb, size);
}

#pragma GCC visibility pop
}
