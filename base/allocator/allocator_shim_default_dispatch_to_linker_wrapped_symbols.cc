// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <malloc.h>

#include "base/allocator/allocator_shim.h"
#include "build/build_config.h"

#if defined(OS_ANDROID) && __ANDROID_API__ < 17
#include <dlfcn.h>
#endif

// This translation unit defines a default dispatch for the allocator shim which
// routes allocations to the original libc functions when using the link-time
// -Wl,-wrap,malloc approach (see README.md).
// The __real_X functions here are special symbols that the linker will relocate
// against the real "X" undefined symbol, so that __real_malloc becomes the
// equivalent of what an undefined malloc symbol reference would have been.
// This is the counterpart of allocator_shim_override_linker_wrapped_symbols.h,
// which routes the __wrap_X functions into the shim.

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void* __real_memalign(size_t, size_t);
void* __real_free(void*);
}  // extern "C"

namespace {

using base::allocator::AllocatorDispatch;

void* RealMalloc(const AllocatorDispatch*, size_t size, void* context) {
  return __real_malloc(size);
}

void* RealCalloc(const AllocatorDispatch*,
                 size_t n,
                 size_t size,
                 void* context) {
  return __real_calloc(n, size);
}

void* RealRealloc(const AllocatorDispatch*,
                  void* address,
                  size_t size,
                  void* context) {
  return __real_realloc(address, size);
}

void* RealMemalign(const AllocatorDispatch*,
                   size_t alignment,
                   size_t size,
                   void* context) {
  return __real_memalign(alignment, size);
}

void RealFree(const AllocatorDispatch*, void* address, void* context) {
  __real_free(address);
}

#if defined(OS_ANDROID) && __ANDROID_API__ < 17
size_t DummyMallocUsableSize(const void*) { return 0; }
#endif

size_t RealSizeEstimate(const AllocatorDispatch*,
                        void* address,
                        void* context) {
#if defined(OS_ANDROID)
#if __ANDROID_API__ < 17
  // malloc_usable_size() is available only starting from API 17.
  // TODO(dskiba): remove once we start building against 17+.
  using MallocUsableSizeFunction = decltype(malloc_usable_size)*;
  static MallocUsableSizeFunction usable_size_function = nullptr;
  if (!usable_size_function) {
    void* function_ptr = dlsym(RTLD_DEFAULT, "malloc_usable_size");
    if (function_ptr) {
      usable_size_function = reinterpret_cast<MallocUsableSizeFunction>(
          function_ptr);
    } else {
      usable_size_function = &DummyMallocUsableSize;
    }
  }
  return usable_size_function(address);
#else
  return malloc_usable_size(address);
#endif
#endif  // OS_ANDROID

  // TODO(primiano): This should be redirected to malloc_usable_size or
  //     the like.
  return 0;
}

}  // namespace

const AllocatorDispatch AllocatorDispatch::default_dispatch = {
    &RealMalloc,       /* alloc_function */
    &RealCalloc,       /* alloc_zero_initialized_function */
    &RealMemalign,     /* alloc_aligned_function */
    &RealRealloc,      /* realloc_function */
    &RealFree,         /* free_function */
    &RealSizeEstimate, /* get_size_estimate_function */
    nullptr,           /* batch_malloc_function */
    nullptr,           /* batch_free_function */
    nullptr,           /* free_definite_size_function */
    nullptr,           /* next */
};
