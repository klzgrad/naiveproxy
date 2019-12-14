// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include "base/profiler/thread_delegate_android.h"

#include "build/build_config.h"

// IMPORTANT NOTE: Some functions within this implementation are invoked while
// the target thread is suspended so it must not do any allocation from the
// heap, including indirectly via use of DCHECK/CHECK or other logging
// statements. Otherwise this code can deadlock on heap locks acquired by the
// target thread before it was suspended. These functions are commented with "NO
// HEAP ALLOCATIONS".

namespace base {

namespace {

uintptr_t GetThreadStackBaseAddress(PlatformThreadId thread_id) {
  pthread_attr_t attr;
  pthread_getattr_np(thread_id, &attr);
  void* base_address;
  size_t size;
  pthread_attr_getstack(&attr, &base_address, &size);
  return reinterpret_cast<uintptr_t>(base_address);
}

}  // namespace

ThreadDelegateAndroid::ThreadDelegateAndroid(PlatformThreadId thread_id)
    : thread_stack_base_address_(GetThreadStackBaseAddress(thread_id)) {}

uintptr_t ThreadDelegateAndroid::GetStackBaseAddress() const {
  return thread_stack_base_address_;
}

std::vector<uintptr_t*> ThreadDelegateAndroid::GetRegistersToRewrite(
    RegisterContext* thread_context) {
#if defined(ARCH_CPU_ARM_FAMILY) && defined(ARCH_CPU_32_BITS)
  return {
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r0),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r1),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r2),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r3),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r4),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r5),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r6),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r7),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r8),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r9),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_r10),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_fp),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_ip),
      reinterpret_cast<uintptr_t*>(&thread_context->arm_sp),
      // arm_lr and arm_pc do not require rewriting because they contain
      // addresses of executable code, not addresses in the stack.
  };
#else  // #if defined(ARCH_CPU_ARM_FAMILY) && defined(ARCH_CPU_32_BITS)
  return {};
#endif
}

}  // namespace base
