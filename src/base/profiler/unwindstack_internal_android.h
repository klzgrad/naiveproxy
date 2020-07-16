// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_UNWINDSTACK_INTERNAL_ANDROID_H_
#define BASE_PROFILER_UNWINDSTACK_INTERNAL_ANDROID_H_

#include "third_party/libunwindstack/src/libunwindstack/include/unwindstack/Maps.h"
#include "third_party/libunwindstack/src/libunwindstack/include/unwindstack/Memory.h"

// Avoid including this file directly in a header as it leaks headers from
// libunwindstack. In particular, it's not to be included directly or
// transitively from native_unwinder_android.h

namespace base {

// Implementation of unwindstack::Memory that restricts memory access to a stack
// buffer, used by NativeUnwinderAndroid. While unwinding, only memory accesses
// within the stack should be performed to restore registers.
class UnwindStackMemoryAndroid : public unwindstack::Memory {
 public:
  UnwindStackMemoryAndroid(uintptr_t stack_ptr, uintptr_t stack_top);
  ~UnwindStackMemoryAndroid() override;

  size_t Read(uint64_t addr, void* dst, size_t size) override;

 private:
  const uintptr_t stack_ptr_;
  const uintptr_t stack_top_;
};

}  // namespace base

#endif  // BASE_PROFILER_UNWINDSTACK_INTERNAL_ANDROID_H_