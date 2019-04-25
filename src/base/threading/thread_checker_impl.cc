// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread_checker_impl.h"

#include "base/threading/thread_task_runner_handle.h"

namespace base {

ThreadCheckerImpl::ThreadCheckerImpl() {
  AutoLock auto_lock(lock_);
  EnsureAssigned();
}

ThreadCheckerImpl::~ThreadCheckerImpl() = default;

bool ThreadCheckerImpl::CalledOnValidThread() const {
  AutoLock auto_lock(lock_);
  EnsureAssigned();

  // Always return true when called from the task from which this
  // ThreadCheckerImpl was assigned to a thread.
  if (task_token_ == TaskToken::GetForCurrentThread())
    return true;

  // If this ThreadCheckerImpl is bound to a valid SequenceToken, it must be
  // equal to the current SequenceToken and there must be a registered
  // ThreadTaskRunnerHandle. Otherwise, the fact that the current task runs on
  // the thread to which this ThreadCheckerImpl is bound is fortuitous.
  if (sequence_token_.IsValid() &&
      (sequence_token_ != SequenceToken::GetForCurrentThread() ||
       !ThreadTaskRunnerHandle::IsSet())) {
    return false;
  }

  return thread_id_ == PlatformThread::CurrentRef();
}

void ThreadCheckerImpl::DetachFromThread() {
  AutoLock auto_lock(lock_);
  thread_id_ = PlatformThreadRef();
  task_token_ = TaskToken();
  sequence_token_ = SequenceToken();
}

void ThreadCheckerImpl::EnsureAssigned() const {
  lock_.AssertAcquired();
  if (!thread_id_.is_null())
    return;

  thread_id_ = PlatformThread::CurrentRef();
  task_token_ = TaskToken::GetForCurrentThread();
  sequence_token_ = SequenceToken::GetForCurrentThread();
}

}  // namespace base
