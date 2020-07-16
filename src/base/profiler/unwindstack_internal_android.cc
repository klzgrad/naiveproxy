// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/unwindstack_internal_android.h"

#include <string.h>

#include "base/logging.h"

namespace base {

UnwindStackMemoryAndroid::UnwindStackMemoryAndroid(uintptr_t stack_ptr,
                                                   uintptr_t stack_top)
    : stack_ptr_(stack_ptr), stack_top_(stack_top) {
  DCHECK_LE(stack_ptr_, stack_top_);
}

UnwindStackMemoryAndroid::~UnwindStackMemoryAndroid() = default;

size_t UnwindStackMemoryAndroid::Read(uint64_t addr, void* dst, size_t size) {
  if (addr < stack_ptr_)
    return 0;
  if (size >= stack_top_ || addr > stack_top_ - size)
    return 0;
  memcpy(dst, reinterpret_cast<void*>(addr), size);
  return size;
}

}  // namespace base