// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_
#define BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/containers/stack.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/condition_variable.h"
#include "base/task_runner.h"
#include "base/task_scheduler/priority_queue.h"
#include "base/task_scheduler/scheduler_lock.h"
#include "base/task_scheduler/scheduler_worker.h"
#include "base/task_scheduler/scheduler_worker_pool.h"
#include "base/task_scheduler/scheduler_worker_stack.h"
#include "base/task_scheduler/sequence.h"
#include "base/task_scheduler/task.h"
#include "base/time/time.h"

namespace base {

class HistogramBase;
class SchedulerWorkerPoolParams;

namespace internal {

class DelayedTaskManager;
class TaskTracker;

// A pool of workers that run Tasks.
//
// The pool doesn't create threads until Start() is called. Tasks can be posted
// at any time but will not run until after Start() is called.
//
// This class is thread-safe.
class BASE_EXPORT SchedulerWorkerPoolImpl : public SchedulerWorkerPool {
 public:
  // Constructs a pool without workers.
  //
  // |name| is used to label the pool's threads ("TaskScheduler" + |name| +
  // index) and histograms ("TaskScheduler." + histogram name + "." + |name| +
  // extra suffixes). |priority_hint| is the preferred thread priority; the
  // actual thread priority depends on shutdown state and platform capabilities.
  // |task_tracker| keeps track of tasks. |delayed_task_manager| handles tasks
  // posted with a delay.
  SchedulerWorkerPoolImpl(
      const std::string& name,
      ThreadPriority priority_hint,
      TaskTracker* task_tracker,
      DelayedTaskManager* delayed_task_manager);

  // Creates workers following the |params| specification, allowing existing and
  // future tasks to run. Uses |service_thread_task_runner| to monitor for
  // blocked threads in the pool. Can only be called once. CHECKs on failure.
  void Start(const SchedulerWorkerPoolParams& params,
             scoped_refptr<TaskRunner> service_thread_task_runner);

  // Destroying a SchedulerWorkerPoolImpl returned by Create() is not allowed in
  // production; it is always leaked. In tests, it can only be destroyed after
  // JoinForTesting() has returned.
  ~SchedulerWorkerPoolImpl() override;

  // SchedulerWorkerPool:
  void JoinForTesting() override;

  const HistogramBase* num_tasks_before_detach_histogram() const {
    return num_tasks_before_detach_histogram_;
  }

  const HistogramBase* num_tasks_between_waits_histogram() const {
    return num_tasks_between_waits_histogram_;
  }

  void GetHistograms(std::vector<const HistogramBase*>* histograms) const;

  // Returns the maximum number of non-blocked tasks that can run concurrently
  // in this pool.
  //
  // TODO(fdoray): Remove this method. https://crbug.com/687264
  int GetMaxConcurrentNonBlockedTasksDeprecated() const;

  // Waits until at least |n| workers are idle.
  void WaitForWorkersIdleForTesting(size_t n);

  // Waits until all workers are idle.
  void WaitForAllWorkersIdleForTesting();

  // Disallows worker cleanup. If the suggested reclaim time is not
  // TimeDelta::Max(), the test must call this before JoinForTesting() to reduce
  // the chance of thread detachment during the process of joining all of the
  // threads, and as a result, threads running after JoinForTesting().
  void DisallowWorkerCleanupForTesting();

  // Returns the number of workers in this worker pool.
  size_t NumberOfWorkersForTesting();

  // Returns |worker_capacity_|.
  size_t GetWorkerCapacityForTesting();

  // Returns the number of workers that are idle (i.e. not running tasks).
  size_t NumberOfIdleWorkersForTesting();

  // Sets the MayBlock waiting threshold to TimeDelta::Max().
  void MaximizeMayBlockThresholdForTesting();

 private:
  class SchedulerWorkerDelegateImpl;

  // Friend tests so that they can access |kBlockedWorkersPollPeriod| and
  // BlockedThreshold().
  friend class TaskSchedulerWorkerPoolBlockingTest;
  friend class TaskSchedulerWorkerPoolMayBlockTest;

  // The period between calls to AdjustWorkerCapacity() when the pool is at
  // capacity. This value was set unscientifically based on intuition and may be
  // adjusted in the future.
  static constexpr TimeDelta kBlockedWorkersPollPeriod =
      TimeDelta::FromMilliseconds(50);

  SchedulerWorkerPoolImpl(const SchedulerWorkerPoolParams& params,
                          TaskTracker* task_tracker,
                          DelayedTaskManager* delayed_task_manager);

  // SchedulerWorkerPool:
  void OnCanScheduleSequence(scoped_refptr<Sequence> sequence) override;

  // Waits until at least |n| workers are idle. |lock_| must be held to call
  // this function.
  void WaitForWorkersIdleLockRequiredForTesting(size_t n);

  // Wakes up the last worker from this worker pool to go idle, if any.
  void WakeUpOneWorker();

  // Performs the same action as WakeUpOneWorker() except asserts |lock_| is
  // acquired rather than acquires it.
  void WakeUpOneWorkerLockRequired();

  // Adds a worker, if needed, to maintain one idle worker, |worker_capacity_|
  // permitting.
  void MaintainAtLeastOneIdleWorkerLockRequired();

  // Adds |worker| to |idle_workers_stack_|.
  void AddToIdleWorkersStackLockRequired(SchedulerWorker* worker);

  // Peeks from |idle_workers_stack_|.
  const SchedulerWorker* PeekAtIdleWorkersStackLockRequired() const;

  // Removes |worker| from |idle_workers_stack_|.
  void RemoveFromIdleWorkersStackLockRequired(SchedulerWorker* worker);

  // Returns true if worker cleanup is permitted.
  bool CanWorkerCleanupForTesting();

  // Tries to add a new SchedulerWorker to the pool. Returns the new
  // SchedulerWorker on success, nullptr otherwise. Cannot be called before
  // Start(). Must be called under the protection of |lock_|.
  SchedulerWorker* CreateRegisterAndStartSchedulerWorkerLockRequired();

  // Returns the number of workers in the pool that should not run tasks due to
  // the pool being over worker capacity.
  size_t NumberOfExcessWorkersLockRequired() const;

  // Examines the list of SchedulerWorkers and increments |worker_capacity_| for
  // each worker that has been within the scope of a MAY_BLOCK
  // ScopedBlockingCall for more than BlockedThreshold().
  void AdjustWorkerCapacity();

  // Returns the threshold after which the worker capacity is increased to
  // compensate for a worker that is within a MAY_BLOCK ScopedBlockingCall.
  TimeDelta MayBlockThreshold() const;

  // Starts calling AdjustWorkerCapacity() periodically on
  // |service_thread_task_runner_|.
  void PostAdjustWorkerCapacityTaskLockRequired();

  // Returns true if AdjustWorkerCapacity() should periodically be called on
  // |service_thread_task_runner_|.
  bool ShouldPeriodicallyAdjustWorkerCapacityLockRequired();

  void DecrementWorkerCapacityLockRequired();
  void IncrementWorkerCapacityLockRequired();

  const std::string name_;
  const ThreadPriority priority_hint_;

  // PriorityQueue from which all threads of this worker pool get work.
  PriorityQueue shared_priority_queue_;

  // Suggested reclaim time for workers. Initialized by Start(). Never modified
  // afterwards (i.e. can be read without synchronization after Start()).
  TimeDelta suggested_reclaim_time_;

  SchedulerBackwardCompatibility backward_compatibility_;

  // Synchronizes accesses to |workers_|, |worker_capacity_|,
  // |num_pending_may_block_workers_|, |idle_workers_stack_|,
  // |idle_workers_stack_cv_for_testing_|, |num_wake_ups_before_start_|,
  // |cleanup_timestamps_|, |polling_worker_capacity_|,
  // |SchedulerWorkerDelegateImpl::is_on_idle_workers_stack_|,
  // |SchedulerWorkerDelegateImpl::incremented_worker_capacity_since_blocked_|
  // and |SchedulerWorkerDelegateImpl::may_block_start_time_|. Has
  // |shared_priority_queue_|'s lock as its predecessor so that a worker can be
  // pushed to |idle_workers_stack_| within the scope of a Transaction (more
  // details in GetWork()).
  mutable SchedulerLock lock_;

  // All workers owned by this worker pool.
  std::vector<scoped_refptr<SchedulerWorker>> workers_;

  // Workers can be added as needed up until there are |worker_capacity_|
  // workers.
  size_t worker_capacity_ = 0;

  // Initial value of |worker_capacity_| as set in Start().
  size_t initial_worker_capacity_ = 0;

  // Number workers that are within the scope of a MAY_BLOCK ScopedBlockingCall
  // but haven't caused a worker capacity increase yet.
  int num_pending_may_block_workers_ = 0;

  // Stack of idle workers. Initially, all workers are on this stack. A worker
  // is removed from the stack before its WakeUp() function is called and when
  // it receives work from GetWork() (a worker calls GetWork() when its sleep
  // timeout expires, even if its WakeUp() method hasn't been called). A worker
  // is pushed on this stack when it receives nullptr from GetWork().
  SchedulerWorkerStack idle_workers_stack_;

  // Signaled when a worker is added to the idle workers stack.
  std::unique_ptr<ConditionVariable> idle_workers_stack_cv_for_testing_;

  // Number of wake ups that occurred before Start(). Never modified after
  // Start() (i.e. can be read without synchronization after Start()).
  int num_wake_ups_before_start_ = 0;

  // Stack that contains the timestamps of when workers get cleaned up.
  // Timestamps get popped off the stack as new workers are added.
  base::stack<TimeTicks, std::vector<TimeTicks>> cleanup_timestamps_;

  // Whether we are currently polling for necessary adjustments to
  // |worker_capacity_|.
  bool polling_worker_capacity_ = false;

  // Used for testing and makes MayBlockThreshold() return the maximum
  // TimeDelta.
  AtomicFlag maximum_blocked_threshold_for_testing_;

  // Signaled once JoinForTesting() has returned.
  WaitableEvent join_for_testing_returned_;

  // Indicates to the delegates that workers are not permitted to cleanup.
  AtomicFlag worker_cleanup_disallowed_;

#if DCHECK_IS_ON()
  // Set at the start of JoinForTesting().
  AtomicFlag join_for_testing_started_;
#endif

  // TaskScheduler.DetachDuration.[worker pool name] histogram. Intentionally
  // leaked.
  HistogramBase* const detach_duration_histogram_;

  // TaskScheduler.NumTasksBeforeDetach.[worker pool name] histogram.
  // Intentionally leaked.
  HistogramBase* const num_tasks_before_detach_histogram_;

  // TaskScheduler.NumTasksBetweenWaits.[worker pool name] histogram.
  // Intentionally leaked.
  HistogramBase* const num_tasks_between_waits_histogram_;

  scoped_refptr<TaskRunner> service_thread_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerPoolImpl);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_
