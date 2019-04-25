// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_H_
#define BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_H_

#include "base/base_export.h"
#include "base/memory/ref_counted.h"
#include "base/task/task_scheduler/can_schedule_sequence_observer.h"
#include "base/task/task_scheduler/priority_queue.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/task.h"
#include "base/task/task_scheduler/tracked_ref.h"

namespace base {
namespace internal {

class TaskTracker;

// Interface for a worker pool.
class BASE_EXPORT SchedulerWorkerPool : public CanScheduleSequenceObserver {
 public:
  // Delegate interface for SchedulerWorkerPool.
  class BASE_EXPORT Delegate {
   public:
    virtual ~Delegate() = default;

    // Invoked when the Sequence in |sequence_and_transaction| is non-empty
    // after the SchedulerWorkerPool has run a task from it. The implementation
    // must return the pool in which the Sequence should be reenqueued.
    virtual SchedulerWorkerPool* GetWorkerPoolForTraits(
        const TaskTraits& traits) = 0;
  };

  ~SchedulerWorkerPool() override;

  // CanScheduleSequenceObserver:
  void OnCanScheduleSequence(scoped_refptr<Sequence> sequence) override = 0;

  // Posts |task| to be executed by this SchedulerWorkerPool as part of
  // the Sequence in |sequence_and_transaction|. This must only be called after
  // |task| has gone through TaskTracker::WillPostTask() and after |task|'s
  // delayed run time.
  void PostTaskWithSequenceNow(Task task,
                               SequenceAndTransaction sequence_and_transaction);

  // Registers the worker pool in TLS.
  void BindToCurrentThread();

  // Resets the worker pool in TLS.
  void UnbindFromCurrentThread();

  // Returns true if the worker pool is registered in TLS.
  bool IsBoundToCurrentThread() const;

  // Updates the position of the Sequence in |sequence_and_transaction| in
  // |shared_priority_queue| based on the Sequence's current traits.
  void UpdateSortKey(SequenceAndTransaction sequence_and_transaction);

  // Removes |sequence| from |priority_queue_|. Returns true if successful, or
  // false if |sequence| is not currently in |priority_queue_|, such as when a
  // worker is running a task from it.
  bool RemoveSequence(scoped_refptr<Sequence> sequence);

  // Prevents new tasks from starting to run and waits for currently running
  // tasks to complete their execution. It is guaranteed that no thread will do
  // work on behalf of this SchedulerWorkerPool after this returns. It is
  // invalid to post a task once this is called. TaskTracker::Flush() can be
  // called before this to complete existing tasks, which might otherwise post a
  // task during JoinForTesting(). This can only be called once.
  virtual void JoinForTesting() = 0;

  // Enqueues the Sequence in |sequence_and_transaction| which was previously in
  // a different worker pool into this worker pool's priority queue.
  virtual void ReEnqueueSequenceChangingPool(
      SequenceAndTransaction sequence_and_transaction) = 0;

  // Called when the Sequence in |sequence_and_transaction| can be scheduled.
  // It is expected that TaskTracker::RunNextTask() will be called with
  // the Sequence as argument after this is called.
  virtual void OnCanScheduleSequence(
      SequenceAndTransaction sequence_and_transaction) = 0;

  // Returns the maximum number of non-blocked tasks that can run concurrently
  // in this pool.
  //
  // TODO(fdoray): Remove this method. https://crbug.com/687264
  virtual size_t GetMaxConcurrentNonBlockedTasksDeprecated() const = 0;

  // Reports relevant metrics per implementation.
  virtual void ReportHeartbeatMetrics() const = 0;

 protected:
  SchedulerWorkerPool(TrackedRef<TaskTracker> task_tracker,
                      TrackedRef<Delegate> delegate);

  // Synchronizes accesses to all members of this class which are neither const,
  // atomic, nor immutable after start. Since this lock is a bottleneck to post
  // and schedule work, only simple data structure manipulations are allowed
  // within its scope (no thread creation or wake up).
  mutable SchedulerLock lock_;

  // PriorityQueue from which all threads of this worker pool get work.
  PriorityQueue priority_queue_ GUARDED_BY(lock_);

  const TrackedRef<TaskTracker> task_tracker_;
  const TrackedRef<Delegate> delegate_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerPool);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_H_
