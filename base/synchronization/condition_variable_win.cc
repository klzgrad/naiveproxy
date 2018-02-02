// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/condition_variable.h"

#include "base/synchronization/lock.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"

namespace base {

ConditionVariable::ConditionVariable(Lock* user_lock)
    : srwlock_(user_lock->lock_.native_handle())
#if DCHECK_IS_ON()
    , user_lock_(user_lock)
#endif
{
  DCHECK(user_lock);
  InitializeConditionVariable(&cv_);
}

ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::Wait() {
  TimedWait(TimeDelta::FromMilliseconds(INFINITE));
}

void ConditionVariable::TimedWait(const TimeDelta& max_time) {
  internal::AssertBaseSyncPrimitivesAllowed();
  ScopedBlockingCall scoped_blocking_call(BlockingType::MAY_BLOCK);
  DWORD timeout = static_cast<DWORD>(max_time.InMilliseconds());

#if DCHECK_IS_ON()
  user_lock_->CheckHeldAndUnmark();
#endif

  if (!SleepConditionVariableSRW(&cv_, srwlock_, timeout, 0)) {
    // On failure, we only expect the CV to timeout. Any other error value means
    // that we've unexpectedly woken up.
    // Note that WAIT_TIMEOUT != ERROR_TIMEOUT. WAIT_TIMEOUT is used with the
    // WaitFor* family of functions as a direct return value. ERROR_TIMEOUT is
    // used with GetLastError().
    DCHECK_EQ(static_cast<DWORD>(ERROR_TIMEOUT), GetLastError());
  }

#if DCHECK_IS_ON()
  user_lock_->CheckUnheldAndMark();
#endif
}

void ConditionVariable::Broadcast() {
  WakeAllConditionVariable(&cv_);
}

void ConditionVariable::Signal() {
  WakeConditionVariable(&cv_);
}

}  // namespace base
