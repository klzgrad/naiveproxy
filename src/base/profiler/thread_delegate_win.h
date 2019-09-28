// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_THREAD_DELEGATE_WIN_H_
#define BASE_PROFILER_THREAD_DELEGATE_WIN_H_

#include <windows.h>

#include "base/base_export.h"
#include "base/profiler/thread_delegate.h"
#include "base/threading/platform_thread.h"
#include "base/win/scoped_handle.h"

namespace base {

// Platform- and thread-specific implementation in support of stack sampling on
// Windows.
class BASE_EXPORT ThreadDelegateWin : public ThreadDelegate {
 public:
  class ScopedSuspendThread : public ThreadDelegate::ScopedSuspendThread {
   public:
    explicit ScopedSuspendThread(HANDLE thread_handle);
    ~ScopedSuspendThread() override;

    bool WasSuccessful() const override;

   private:
    HANDLE thread_handle_;
    bool was_successful_;

    DISALLOW_COPY_AND_ASSIGN(ScopedSuspendThread);
  };

  explicit ThreadDelegateWin(PlatformThreadId thread_id);
  ~ThreadDelegateWin() override;

  ThreadDelegateWin(const ThreadDelegateWin&) = delete;
  ThreadDelegateWin& operator=(const ThreadDelegateWin&) = delete;

  // ThreadDelegate
  std::unique_ptr<ThreadDelegate::ScopedSuspendThread>
  CreateScopedSuspendThread() override;
  bool GetThreadContext(CONTEXT* thread_context) override;
  uintptr_t GetStackBaseAddress() const override;
  bool CanCopyStack(uintptr_t stack_pointer) override;
  std::vector<uintptr_t*> GetRegistersToRewrite(
      CONTEXT* thread_context) override;

 private:
  win::ScopedHandle thread_handle_;
  const uintptr_t thread_stack_base_address_;
};

}  // namespace base

#endif  // BASE_PROFILER_THREAD_DELEGATE_WIN_H_
