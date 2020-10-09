// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/lock_impl.h"

#include "base/debug/activity_tracker.h"

#include <windows.h>

namespace base {
namespace internal {

LockImpl::LockImpl() : native_handle_(SRWLOCK_INIT) {}

LockImpl::~LockImpl() = default;

bool LockImpl::Try() {
  return !!::TryAcquireSRWLockExclusive(
      reinterpret_cast<PSRWLOCK>(&native_handle_));
}

void LockImpl::Lock() {
  // The ScopedLockAcquireActivity below is relatively expensive and so its
  // actions can become significant due to the very large number of locks that
  // tend to be used throughout the build. It is also not needed unless the lock
  // is contended.
  //
  // To avoid this cost in the vast majority of the calls, simply "try" the lock
  // first and only do the (tracked) blocking call if that fails. |Try()| is
  // cheap, as it doesn't call into the kernel.
  if (Try())
    return;

  base::debug::ScopedLockAcquireActivity lock_activity(this);
  ::AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&native_handle_));
}

}  // namespace internal
}  // namespace base
