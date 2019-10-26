// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_THREAD_DELEGATE_ANDROID_H_
#define BASE_PROFILER_THREAD_DELEGATE_ANDROID_H_

#include "base/base_export.h"
#include "base/profiler/thread_delegate.h"

namespace base {

// Platform- and thread-specific implementation in support of stack sampling on
// Android.
//
// TODO(charliea): Implement this class.
// See: https://crbug.com/988574
class BASE_EXPORT ThreadDelegateAndroid : public ThreadDelegate {
 public:
  class ScopedSuspendThread : public ThreadDelegate::ScopedSuspendThread {
   public:
    ScopedSuspendThread() = default;
    ~ScopedSuspendThread() override = default;

    ScopedSuspendThread(const ScopedSuspendThread&) = delete;
    ScopedSuspendThread& operator=(const ScopedSuspendThread&) = delete;

    bool WasSuccessful() const override;
  };

  ThreadDelegateAndroid() = default;
  ~ThreadDelegateAndroid() override = default;

  ThreadDelegateAndroid(const ThreadDelegateAndroid&) = delete;
  ThreadDelegateAndroid& operator=(const ThreadDelegateAndroid&) = delete;

  // ThreadDelegate
  std::unique_ptr<ThreadDelegate::ScopedSuspendThread>
  CreateScopedSuspendThread() override;
  bool GetThreadContext(RegisterContext* thread_context) override;
  uintptr_t GetStackBaseAddress() const override;
  bool CanCopyStack(uintptr_t stack_pointer) override;
  std::vector<uintptr_t*> GetRegistersToRewrite(
      RegisterContext* thread_context) override;
};

}  // namespace base

#endif  // BASE_PROFILER_THREAD_DELEGATE_ANDROID_H_
