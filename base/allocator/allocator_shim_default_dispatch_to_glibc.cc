// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/allocator_shim.h"

#include <malloc.h>

// This translation unit defines a default dispatch for the allocator shim which
// routes allocations to libc functions.
// The code here is strongly inspired from tcmalloc's libc_override_glibc.h.

extern "C" {
void* __libc_malloc(size_t size);
void* __libc_calloc(size_t n, size_t size);
void* __libc_realloc(void* address, size_t size);
void* __libc_memalign(size_t alignment, size_t size);
void __libc_free(void* ptr);
}  // extern "C"

namespace {

using base::allocator::AllocatorDispatch;

void* GlibcMalloc(const AllocatorDispatch*, size_t size, void* context) {
  return __libc_malloc(size);
}

void* GlibcCalloc(const AllocatorDispatch*,
                  size_t n,
                  size_t size,
                  void* context) {
  return __libc_calloc(n, size);
}

void* GlibcRealloc(const AllocatorDispatch*,
                   void* address,
                   size_t size,
                   void* context) {
  return __libc_realloc(address, size);
}

void* GlibcMemalign(const AllocatorDispatch*,
                    size_t alignment,
                    size_t size,
                    void* context) {
  return __libc_memalign(alignment, size);
}

void GlibcFree(const AllocatorDispatch*, void* address, void* context) {
  __libc_free(address);
}

size_t GlibcGetSizeEstimate(const AllocatorDispatch*,
                            void* address,
                            void* context) {
  // TODO(siggi, primiano): malloc_usable_size may need redirection in the
  //     presence of interposing shims that divert allocations.
  return malloc_usable_size(address);
}

}  // namespace

const AllocatorDispatch AllocatorDispatch::default_dispatch = {
    &GlibcMalloc,          /* alloc_function */
    &GlibcCalloc,          /* alloc_zero_initialized_function */
    &GlibcMemalign,        /* alloc_aligned_function */
    &GlibcRealloc,         /* realloc_function */
    &GlibcFree,            /* free_function */
    &GlibcGetSizeEstimate, /* get_size_estimate_function */
    nullptr,               /* batch_malloc_function */
    nullptr,               /* batch_free_function */
    nullptr,               /* free_definite_size_function */
    nullptr,               /* next */
};
