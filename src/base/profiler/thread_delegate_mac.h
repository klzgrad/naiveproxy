// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_THREAD_DELEGATE_MAC_H_
#define BASE_PROFILER_THREAD_DELEGATE_MAC_H_

#include <mach/mach.h>

#include "base/base_export.h"
#include "base/profiler/native_unwinder_mac.h"
#include "base/profiler/thread_delegate.h"
#include "base/sampling_heap_profiler/module_cache.h"
#include "base/threading/platform_thread.h"

namespace base {

// Platform- and thread-specific implementation in support of stack sampling on
// Mac.
class BASE_EXPORT ThreadDelegateMac : public ThreadDelegate {
 public:
  class ScopedSuspendThread : public ThreadDelegate::ScopedSuspendThread {
   public:
    explicit ScopedSuspendThread(mach_port_t thread_port);
    ~ScopedSuspendThread() override;

    ScopedSuspendThread(const ScopedSuspendThread&) = delete;
    ScopedSuspendThread& operator=(const ScopedSuspendThread&) = delete;

    bool WasSuccessful() const override;

   private:
    mach_port_t thread_port_;
  };

  ThreadDelegateMac(mach_port_t thread_port);
  ~ThreadDelegateMac() override;

  ThreadDelegateMac(const ThreadDelegateMac&) = delete;
  ThreadDelegateMac& operator=(const ThreadDelegateMac&) = delete;

  // ThreadDelegate
  std::unique_ptr<ThreadDelegate::ScopedSuspendThread>
  CreateScopedSuspendThread() override;
  bool GetThreadContext(x86_thread_state64_t* thread_context) override;
  uintptr_t GetStackBaseAddress() const override;
  bool CanCopyStack(uintptr_t stack_pointer) override;
  std::vector<uintptr_t*> GetRegistersToRewrite(
      x86_thread_state64_t* thread_context) override;

 private:
  // Weak reference: Mach port for thread being profiled.
  mach_port_t thread_port_;

  // The stack base address corresponding to |thread_port_|.
  const uintptr_t thread_stack_base_address_;
};

}  // namespace base

#endif  // BASE_PROFILER_THREAD_DELEGATE_MAC_H_
