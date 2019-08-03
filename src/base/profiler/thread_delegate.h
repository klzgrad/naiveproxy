// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_THREAD_DELEGATE_H_
#define BASE_PROFILER_THREAD_DELEGATE_H_

#include <vector>

#include "base/base_export.h"
#include "base/profiler/frame.h"
#include "base/profiler/register_context.h"

namespace base {

// Platform-specific thread and stack manipulation delegate, for use by the
// platform-independent stack copying/walking implementation in
// StackSamplerImpl.
//
// IMPORTANT NOTE: Most methods in this interface are invoked while the target
// thread is suspended so must not do any allocation from the heap, including
// indirectly via use of DCHECK/CHECK or other logging statements. Otherwise the
// implementation can deadlock on heap locks acquired by the target thread
// before it was suspended. These functions are commented with "NO HEAP
// ALLOCATIONS".
class BASE_EXPORT ThreadDelegate {
 public:
  // Implementations of this interface should suspend the thread for the
  // object's lifetime. NO HEAP ALLOCATIONS between the time the thread is
  // suspended and resumed.
  class BASE_EXPORT ScopedSuspendThread {
   public:
    ScopedSuspendThread() = default;
    virtual ~ScopedSuspendThread() = default;

    ScopedSuspendThread(const ScopedSuspendThread&) = delete;
    ScopedSuspendThread& operator=(const ScopedSuspendThread&) = delete;

    virtual bool WasSuccessful() const = 0;
  };

  ThreadDelegate() = default;
  virtual ~ThreadDelegate() = default;

  ThreadDelegate(const ThreadDelegate&) = delete;
  ThreadDelegate& operator=(const ThreadDelegate&) = delete;

  // Creates an object that holds the thread suspended for its lifetime.
  virtual std::unique_ptr<ScopedSuspendThread> CreateScopedSuspendThread() = 0;

  // Gets the register context for the thread.
  // NO HEAP ALLOCATIONS.
  virtual bool GetThreadContext(RegisterContext* thread_context) = 0;

  // Gets the base address of the thread's stack.
  virtual uintptr_t GetStackBaseAddress() const = 0;

  // Returns true if the thread's stack can be copied, where the bottom address
  // of the thread is at |stack_pointer|.
  // NO HEAP ALLOCATIONS.
  virtual bool CanCopyStack(uintptr_t stack_pointer) = 0;

  // Returns a list of registers that should be rewritten to point into the
  // stack copy, if they originally pointed into the original stack.
  // May heap allocate.
  virtual std::vector<uintptr_t*> GetRegistersToRewrite(
      RegisterContext* thread_context) = 0;
};

}  // namespace base

#endif  // BASE_PROFILER_THREAD_DELEGATE_H_
