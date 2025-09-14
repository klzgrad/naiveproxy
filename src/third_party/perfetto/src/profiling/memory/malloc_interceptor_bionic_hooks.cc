/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <bionic/malloc.h>
#include <malloc.h>
#include <private/bionic_malloc_dispatch.h>

#include <atomic>
#include <cinttypes>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/heap_profile.h"
#include "src/profiling/memory/heap_profile_internal.h"

#include "src/profiling/memory/wrap_allocators.h"

#pragma GCC visibility push(default)
extern "C" {

bool heapprofd_initialize(const MallocDispatch* malloc_dispatch,
                          bool* zygote_child,
                          const char* options);
void heapprofd_finalize();
void heapprofd_dump_heap(const char* file_name);
void heapprofd_get_malloc_leak_info(uint8_t** info,
                                    size_t* overall_size,
                                    size_t* info_size,
                                    size_t* total_memory,
                                    size_t* backtrace_size);
bool heapprofd_write_malloc_leak_info(FILE* fp);
ssize_t heapprofd_malloc_backtrace(void* pointer,
                                   uintptr_t* frames,
                                   size_t frame_count);
void heapprofd_free_malloc_leak_info(uint8_t* info);
size_t heapprofd_malloc_usable_size(void* pointer);
void* heapprofd_malloc(size_t size);
void heapprofd_free(void* pointer);
void* heapprofd_aligned_alloc(size_t alignment, size_t size);
void* heapprofd_memalign(size_t alignment, size_t bytes);
void* heapprofd_realloc(void* pointer, size_t bytes);
void* heapprofd_calloc(size_t nmemb, size_t bytes);
struct mallinfo heapprofd_mallinfo();
int heapprofd_mallopt(int param, int value);
int heapprofd_malloc_info(int options, FILE* fp);
int heapprofd_posix_memalign(void** memptr, size_t alignment, size_t size);
int heapprofd_malloc_iterate(uintptr_t base,
                             size_t size,
                             void (*callback)(uintptr_t base,
                                              size_t size,
                                              void* arg),
                             void* arg);
void heapprofd_malloc_disable();
void heapprofd_malloc_enable();

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* heapprofd_pvalloc(size_t bytes);
void* heapprofd_valloc(size_t size);
#endif
}
#pragma GCC visibility pop

namespace {

// The real malloc function pointers we get in initialize. Set once in the first
// initialize invocation, and never changed afterwards. Because bionic does a
// release write after initialization and an acquire read to retrieve the hooked
// malloc functions, we can use relaxed memory mode for both writing and
// reading.
std::atomic<const MallocDispatch*> g_dispatch{nullptr};

const MallocDispatch* GetDispatch() {
  return g_dispatch.load(std::memory_order_relaxed);
}

// Note: android_mallopt(M_RESET_HOOKS) is mutually exclusive with
// heapprofd_initialize. Concurrent calls get discarded, which might be our
// unpatching attempt if there is a concurrent re-initialization running due to
// a new signal.
void ProfileDisabledCallback(void*, const AHeapProfileDisableCallbackInfo*) {
  if (!android_mallopt(M_RESET_HOOKS, nullptr, 0))
    PERFETTO_PLOG("Unpatching heapprofd hooks failed.");
}

uint32_t g_heap_id = AHeapProfile_registerHeap(
    AHeapInfo_setDisabledCallback(AHeapInfo_create("libc.malloc"),
                                  ProfileDisabledCallback,
                                  nullptr));

}  // namespace

// Setup for the rest of profiling. The first time profiling is triggered in a
// process, this is called after this client library is dlopened, but before the
// rest of the hooks are patched in. However, as we support multiple profiling
// sessions within a process' lifetime, this function can also be legitimately
// called any number of times afterwards (note: bionic guarantees that at most
// one initialize call is active at a time).
//
// Note: if profiling is triggered at runtime, this runs on a dedicated pthread
// (which is safe to block). If profiling is triggered at startup, then this
// code runs synchronously.
bool heapprofd_initialize(const MallocDispatch* malloc_dispatch,
                          bool*,
                          const char*) {
  // Table of pointers to backing implementation.
  g_dispatch.store(malloc_dispatch);
  return AHeapProfile_initSession(malloc_dispatch->malloc,
                                  malloc_dispatch->free);
}

void heapprofd_finalize() {
  // At the time of writing, invoked only as an atexit handler. We don't have
  // any specific action to take, and cleanup can be left to the OS.
}

void* heapprofd_malloc(size_t size) {
  return perfetto::profiling::wrap_malloc(g_heap_id, GetDispatch()->malloc,
                                          size);
}

void* heapprofd_calloc(size_t nmemb, size_t size) {
  return perfetto::profiling::wrap_calloc(g_heap_id, GetDispatch()->calloc,
                                          nmemb, size);
}

void* heapprofd_aligned_alloc(size_t alignment, size_t size) {
  // aligned_alloc is the same as memalign.
  return perfetto::profiling::wrap_memalign(
      g_heap_id, GetDispatch()->aligned_alloc, alignment, size);
}

void* heapprofd_memalign(size_t alignment, size_t size) {
  return perfetto::profiling::wrap_memalign(g_heap_id, GetDispatch()->memalign,
                                            alignment, size);
}

int heapprofd_posix_memalign(void** memptr, size_t alignment, size_t size) {
  return perfetto::profiling::wrap_posix_memalign(
      g_heap_id, GetDispatch()->posix_memalign, memptr, alignment, size);
}

void heapprofd_free(void* pointer) {
  return perfetto::profiling::wrap_free(g_heap_id, GetDispatch()->free,
                                        pointer);
}

// Approach to recording realloc: under the initial lock, get a safe copy of the
// client, and make the sampling decision in advance. Then record the
// deallocation, call the real realloc, and finally record the sample if one is
// necessary.
//
// As with the free, we record the deallocation before calling the backing
// implementation to make sure the address is still exclusive while we're
// processing it.
void* heapprofd_realloc(void* pointer, size_t size) {
  return perfetto::profiling::wrap_realloc(g_heap_id, GetDispatch()->realloc,
                                           pointer, size);
}

void heapprofd_dump_heap(const char*) {}

void heapprofd_get_malloc_leak_info(uint8_t**,
                                    size_t*,
                                    size_t*,
                                    size_t*,
                                    size_t*) {}

bool heapprofd_write_malloc_leak_info(FILE*) {
  return false;
}

ssize_t heapprofd_malloc_backtrace(void*, uintptr_t*, size_t) {
  return -1;
}

void heapprofd_free_malloc_leak_info(uint8_t*) {}

size_t heapprofd_malloc_usable_size(void* pointer) {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->malloc_usable_size(pointer);
}

struct mallinfo heapprofd_mallinfo() {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->mallinfo();
}

int heapprofd_mallopt(int param, int value) {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->mallopt(param, value);
}

int heapprofd_malloc_info(int options, FILE* fp) {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->malloc_info(options, fp);
}

int heapprofd_malloc_iterate(uintptr_t base,
                             size_t size,
                             void (*callback)(uintptr_t base,
                                              size_t size,
                                              void* arg),
                             void* arg) {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->malloc_iterate(base, size, callback, arg);
}

void heapprofd_malloc_disable() {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->malloc_disable();
}

void heapprofd_malloc_enable() {
  const MallocDispatch* dispatch = GetDispatch();
  return dispatch->malloc_enable();
}

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* heapprofd_pvalloc(size_t size) {
  return perfetto::profiling::wrap_pvalloc(g_heap_id, GetDispatch()->pvalloc,
                                           size);
}

void* heapprofd_valloc(size_t size) {
  return perfetto::profiling::wrap_valloc(g_heap_id, GetDispatch()->valloc,
                                          size);
}

#endif
