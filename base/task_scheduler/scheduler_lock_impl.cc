// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_lock_impl.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/synchronization/condition_variable.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_local_storage.h"

namespace base {
namespace internal {

namespace {

class SafeAcquisitionTracker {
 public:
  SafeAcquisitionTracker() : tls_acquired_locks_(&OnTLSDestroy) {}

  void RegisterLock(
      const SchedulerLockImpl* const lock,
      const SchedulerLockImpl* const predecessor) {
    DCHECK_NE(lock, predecessor) << "Reentrant locks are unsupported.";
    AutoLock auto_lock(allowed_predecessor_map_lock_);
    allowed_predecessor_map_[lock] = predecessor;
    AssertSafePredecessor(lock);
  }

  void UnregisterLock(const SchedulerLockImpl* const lock) {
    AutoLock auto_lock(allowed_predecessor_map_lock_);
    allowed_predecessor_map_.erase(lock);
  }

  void RecordAcquisition(const SchedulerLockImpl* const lock) {
    AssertSafeAcquire(lock);
    GetAcquiredLocksOnCurrentThread()->push_back(lock);
  }

  void RecordRelease(const SchedulerLockImpl* const lock) {
    LockVector* acquired_locks = GetAcquiredLocksOnCurrentThread();
    const auto iter_at_lock =
        std::find(acquired_locks->begin(), acquired_locks->end(), lock);
    DCHECK(iter_at_lock != acquired_locks->end());
    acquired_locks->erase(iter_at_lock);
  }

 private:
  using LockVector = std::vector<const SchedulerLockImpl*>;
  using PredecessorMap = std::unordered_map<
      const SchedulerLockImpl*, const SchedulerLockImpl*>;

  // This asserts that the lock is safe to acquire. This means that this should
  // be run before actually recording the acquisition.
  void AssertSafeAcquire(const SchedulerLockImpl* const lock) {
    const LockVector* acquired_locks = GetAcquiredLocksOnCurrentThread();

    // If the thread currently holds no locks, this is inherently safe.
    if (acquired_locks->empty())
      return;

    // Otherwise, make sure that the previous lock acquired is an allowed
    // predecessor.
    AutoLock auto_lock(allowed_predecessor_map_lock_);
    // Using at() is exception-safe here as |lock| was registered already.
    const SchedulerLockImpl* allowed_predecessor =
        allowed_predecessor_map_.at(lock);
    DCHECK_EQ(acquired_locks->back(), allowed_predecessor);
  }

  // Asserts that |lock|'s registered predecessor is safe. Because
  // SchedulerLocks are registered at construction time and any predecessor
  // specified on a SchedulerLock must already exist, the first registered
  // SchedulerLock in a potential chain must have a null predecessor and is thus
  // cycle-free. Any subsequent SchedulerLock with a predecessor must come from
  // the set of registered SchedulerLocks. Since the registered SchedulerLocks
  // only contain cycle-free SchedulerLocks, this subsequent SchedulerLock is
  // itself cycle-free and may be safely added to the registered SchedulerLock
  // set.
  void AssertSafePredecessor(const SchedulerLockImpl* lock) const {
    allowed_predecessor_map_lock_.AssertAcquired();
    // Using at() is exception-safe here as |lock| was registered already.
    const SchedulerLockImpl* predecessor = allowed_predecessor_map_.at(lock);
    if (predecessor) {
      DCHECK(allowed_predecessor_map_.find(predecessor) !=
             allowed_predecessor_map_.end())
          << "SchedulerLock was registered before its predecessor. "
          << "Potential cycle detected";
    }
  }

  LockVector* GetAcquiredLocksOnCurrentThread() {
    if (!tls_acquired_locks_.Get())
      tls_acquired_locks_.Set(new LockVector);

    return reinterpret_cast<LockVector*>(tls_acquired_locks_.Get());
  }

  static void OnTLSDestroy(void* value) {
    delete reinterpret_cast<LockVector*>(value);
  }

  // Synchronizes access to |allowed_predecessor_map_|.
  Lock allowed_predecessor_map_lock_;

  // A map of allowed predecessors.
  PredecessorMap allowed_predecessor_map_;

  // A thread-local slot holding a vector of locks currently acquired on the
  // current thread.
  ThreadLocalStorage::Slot tls_acquired_locks_;

  DISALLOW_COPY_AND_ASSIGN(SafeAcquisitionTracker);
};

LazyInstance<SafeAcquisitionTracker>::Leaky g_safe_acquisition_tracker =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

SchedulerLockImpl::SchedulerLockImpl() : SchedulerLockImpl(nullptr) {}

SchedulerLockImpl::SchedulerLockImpl(const SchedulerLockImpl* predecessor) {
  g_safe_acquisition_tracker.Get().RegisterLock(this, predecessor);
}

SchedulerLockImpl::~SchedulerLockImpl() {
  g_safe_acquisition_tracker.Get().UnregisterLock(this);
}

void SchedulerLockImpl::Acquire() {
  lock_.Acquire();
  g_safe_acquisition_tracker.Get().RecordAcquisition(this);
}

void SchedulerLockImpl::Release() {
  lock_.Release();
  g_safe_acquisition_tracker.Get().RecordRelease(this);
}

void SchedulerLockImpl::AssertAcquired() const {
  lock_.AssertAcquired();
}

std::unique_ptr<ConditionVariable>
SchedulerLockImpl::CreateConditionVariable() {
  return std::unique_ptr<ConditionVariable>(new ConditionVariable(&lock_));
}

}  // namespace internal
}  // base
