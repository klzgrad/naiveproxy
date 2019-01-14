// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/scheduler_lock.h"

#include <stdlib.h>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/rand_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/gtest_util.h"
#include "base/threading/platform_thread.h"
#include "base/threading/simple_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {
namespace {

// Adapted from base::Lock's BasicLockTestThread to make sure
// Acquire()/Release() don't crash.
class BasicLockTestThread : public SimpleThread {
 public:
  explicit BasicLockTestThread(SchedulerLock* lock)
      : SimpleThread("BasicLockTestThread"), lock_(lock), acquired_(0) {}

  int acquired() const { return acquired_; }

 private:
  void Run() override {
    for (int i = 0; i < 10; i++) {
      lock_->Acquire();
      acquired_++;
      lock_->Release();
    }
    for (int i = 0; i < 10; i++) {
      lock_->Acquire();
      acquired_++;
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(base::RandInt(0, 19)));
      lock_->Release();
    }
  }

  SchedulerLock* const lock_;
  int acquired_;

  DISALLOW_COPY_AND_ASSIGN(BasicLockTestThread);
};

class BasicLockAcquireAndWaitThread : public SimpleThread {
 public:
  explicit BasicLockAcquireAndWaitThread(SchedulerLock* lock)
      : SimpleThread("BasicLockAcquireAndWaitThread"),
        lock_(lock),
        lock_acquire_event_(WaitableEvent::ResetPolicy::AUTOMATIC,
                            WaitableEvent::InitialState::NOT_SIGNALED),
        main_thread_continue_event_(WaitableEvent::ResetPolicy::AUTOMATIC,
                                    WaitableEvent::InitialState::NOT_SIGNALED) {
  }

  void WaitForLockAcquisition() { lock_acquire_event_.Wait(); }

  void ContinueMain() { main_thread_continue_event_.Signal(); }

 private:
  void Run() override {
    lock_->Acquire();
    lock_acquire_event_.Signal();
    main_thread_continue_event_.Wait();
    lock_->Release();
  }

  SchedulerLock* const lock_;
  WaitableEvent lock_acquire_event_;
  WaitableEvent main_thread_continue_event_;

  DISALLOW_COPY_AND_ASSIGN(BasicLockAcquireAndWaitThread);
};

TEST(TaskSchedulerLock, Basic) {
  SchedulerLock lock;
  BasicLockTestThread thread(&lock);

  thread.Start();

  int acquired = 0;
  for (int i = 0; i < 5; i++) {
    lock.Acquire();
    acquired++;
    lock.Release();
  }
  for (int i = 0; i < 10; i++) {
    lock.Acquire();
    acquired++;
    PlatformThread::Sleep(TimeDelta::FromMilliseconds(base::RandInt(0, 19)));
    lock.Release();
  }
  for (int i = 0; i < 5; i++) {
    lock.Acquire();
    acquired++;
    PlatformThread::Sleep(TimeDelta::FromMilliseconds(base::RandInt(0, 19)));
    lock.Release();
  }

  thread.Join();

  EXPECT_EQ(acquired, 20);
  EXPECT_EQ(thread.acquired(), 20);
}

TEST(TaskSchedulerLock, AcquirePredecessor) {
  SchedulerLock predecessor;
  SchedulerLock lock(&predecessor);
  predecessor.Acquire();
  lock.Acquire();
  lock.Release();
  predecessor.Release();
}

TEST(TaskSchedulerLock, AcquirePredecessorWrongOrder) {
  SchedulerLock predecessor;
  SchedulerLock lock(&predecessor);
  EXPECT_DCHECK_DEATH({
    lock.Acquire();
    predecessor.Acquire();
  });
}

TEST(TaskSchedulerLock, AcquireNonPredecessor) {
  SchedulerLock lock1;
  SchedulerLock lock2;
  EXPECT_DCHECK_DEATH({
    lock1.Acquire();
    lock2.Acquire();
  });
}

TEST(TaskSchedulerLock, AcquireMultipleLocksInOrder) {
  SchedulerLock lock1;
  SchedulerLock lock2(&lock1);
  SchedulerLock lock3(&lock2);
  lock1.Acquire();
  lock2.Acquire();
  lock3.Acquire();
  lock3.Release();
  lock2.Release();
  lock1.Release();
}

TEST(TaskSchedulerLock, AcquireMultipleLocksInTheMiddleOfAChain) {
  SchedulerLock lock1;
  SchedulerLock lock2(&lock1);
  SchedulerLock lock3(&lock2);
  lock2.Acquire();
  lock3.Acquire();
  lock3.Release();
  lock2.Release();
}

TEST(TaskSchedulerLock, AcquireMultipleLocksNoTransitivity) {
  SchedulerLock lock1;
  SchedulerLock lock2(&lock1);
  SchedulerLock lock3(&lock2);
  EXPECT_DCHECK_DEATH({
    lock1.Acquire();
    lock3.Acquire();
  });
}

TEST(TaskSchedulerLock, AcquireLocksDifferentThreadsSafely) {
  SchedulerLock lock1;
  SchedulerLock lock2;
  BasicLockAcquireAndWaitThread thread(&lock1);
  thread.Start();

  lock2.Acquire();
  thread.WaitForLockAcquisition();
  thread.ContinueMain();
  thread.Join();
  lock2.Release();
}

TEST(TaskSchedulerLock,
     AcquireLocksWithPredecessorDifferentThreadsSafelyPredecessorFirst) {
  // A lock and its predecessor may be safely acquired on different threads.
  // This Thread                Other Thread
  // predecessor.Acquire()
  //                            lock.Acquire()
  // predecessor.Release()
  //                            lock.Release()
  SchedulerLock predecessor;
  SchedulerLock lock(&predecessor);
  predecessor.Acquire();
  BasicLockAcquireAndWaitThread thread(&lock);
  thread.Start();
  thread.WaitForLockAcquisition();
  predecessor.Release();
  thread.ContinueMain();
  thread.Join();
}

TEST(TaskSchedulerLock,
     AcquireLocksWithPredecessorDifferentThreadsSafelyPredecessorLast) {
  // A lock and its predecessor may be safely acquired on different threads.
  // This Thread                Other Thread
  // lock.Acquire()
  //                            predecessor.Acquire()
  // lock.Release()
  //                            predecessor.Release()
  SchedulerLock predecessor;
  SchedulerLock lock(&predecessor);
  lock.Acquire();
  BasicLockAcquireAndWaitThread thread(&predecessor);
  thread.Start();
  thread.WaitForLockAcquisition();
  lock.Release();
  thread.ContinueMain();
  thread.Join();
}

TEST(TaskSchedulerLock,
     AcquireLocksWithPredecessorDifferentThreadsSafelyNoInterference) {
  // Acquisition of an unrelated lock on another thread should not affect a
  // legal lock acquisition with a predecessor on this thread.
  // This Thread                Other Thread
  // predecessor.Acquire()
  //                            unrelated.Acquire()
  // lock.Acquire()
  //                            unrelated.Release()
  // lock.Release()
  // predecessor.Release();
  SchedulerLock predecessor;
  SchedulerLock lock(&predecessor);
  predecessor.Acquire();
  SchedulerLock unrelated;
  BasicLockAcquireAndWaitThread thread(&unrelated);
  thread.Start();
  thread.WaitForLockAcquisition();
  lock.Acquire();
  thread.ContinueMain();
  thread.Join();
  lock.Release();
  predecessor.Release();
}

TEST(TaskSchedulerLock, SelfReferentialLock) {
  struct SelfReferentialLock {
    SelfReferentialLock() : lock(&lock) {}

    SchedulerLock lock;
  };

  EXPECT_DCHECK_DEATH({ SelfReferentialLock lock; });
}

TEST(TaskSchedulerLock, PredecessorCycle) {
  struct LockCycle {
    LockCycle() : lock1(&lock2), lock2(&lock1) {}

    SchedulerLock lock1;
    SchedulerLock lock2;
  };

  EXPECT_DCHECK_DEATH({ LockCycle cycle; });
}

TEST(TaskSchedulerLock, PredecessorLongerCycle) {
  struct LockCycle {
    LockCycle()
        : lock1(&lock5),
          lock2(&lock1),
          lock3(&lock2),
          lock4(&lock3),
          lock5(&lock4) {}

    SchedulerLock lock1;
    SchedulerLock lock2;
    SchedulerLock lock3;
    SchedulerLock lock4;
    SchedulerLock lock5;
  };

  EXPECT_DCHECK_DEATH({ LockCycle cycle; });
}

}  // namespace
}  // namespace internal
}  // namespace base
