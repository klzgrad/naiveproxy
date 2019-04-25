// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_MOVEABLE_AUTO_LOCK_H_
#define BASE_TASK_SEQUENCE_MANAGER_MOVEABLE_AUTO_LOCK_H_

#include "base/synchronization/lock.h"

namespace base {
namespace sequence_manager {

class MoveableAutoLock {
 public:
  explicit MoveableAutoLock(Lock& lock) : lock_(lock), moved_(false) {
    lock_.Acquire();
  }

  MoveableAutoLock(MoveableAutoLock&& other) noexcept
      : lock_(other.lock_), moved_(other.moved_) {
    lock_.AssertAcquired();
    other.moved_ = true;
  }

  ~MoveableAutoLock() {
    if (moved_)
      return;
    lock_.AssertAcquired();
    lock_.Release();
  }

 private:
  Lock& lock_;
  bool moved_;
  DISALLOW_COPY_AND_ASSIGN(MoveableAutoLock);
};

}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_MOVEABLE_AUTO_LOCK_H_
