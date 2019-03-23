// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_
#define BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/containers/stack.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/task_scheduler/priority_queue.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_scheduler/scheduler_worker.h"
#include "base/task/task_scheduler/scheduler_worker_pool.h"
#include "base/task/task_scheduler/scheduler_worker_stack.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/task.h"
#include "base/task/task_scheduler/tracked_ref.h"
#include "base/task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

class HistogramBase;
class SchedulerWorkerObserver;
class SchedulerWorkerPoolParams;

namespace internal {

class TaskTracker;

// A pool of workers that run Tasks.
//
// The pool doesn't create threads until Start() is called. Tasks can be posted
// at any time but will not run until after Start() is called.
//
// This class is thread-safe.
class BASE_EXPORT SchedulerWorkerPoolImpl : public SchedulerWorkerPool {
 public:
  enum class WorkerEnvironment {
    // No special worker environment required.
    NONE,
#if defined(OS_WIN)
    // Initialize a COM MTA on the worker.
    COM_MTA,
#endif  // defined(OS_WIN)
  };

  // Constructs a pool without workers.
  //
  // |histogram_label| is used to label the pool's histograms ("TaskScheduler."
  // + histogram_name + "." + |histogram_label| + extra suffixes), it must not
  // be empty. |pool_label| is used to label the pool's threads, it must not be
  // empty. |priority_hint| is the preferred thread priority; the actual thread
  // priority depends on shutdown state and platform capabilities.
  // |task_tracker| keeps track of tasks.
  SchedulerWorkerPoolImpl(StringPiece histogram_label,
                          StringPiece pool_label,
                          ThreadPriority priority_hint,
                          TrackedRef<TaskTracker> task_tracker,
                          TrackedRef<Delegate> delegate);

  // Creates workers following the |params| specification, allowing existing and
  // future tasks to run. The pool runs at most |max_best_effort_tasks|
  // unblocked BEST_EFFORT tasks concurrently, uses |service_thread_task_runner|
  // to monitor for blocked tasks, and, if specified, notifies
  // |scheduler_worker_observer| when a worker enters and exits its main
  // function (the observer must not be destroyed before JoinForTesting() has
  // returned). |worker_environment| specifies the environment in which tasks
  // are executed. |may_block_threshold| is the timeout after which a task in a
  // MAY_BLOCK ScopedBlockingCall is considered blocked (the pool will choose an
  // appropriate value if none is specified). Can only be called once. CHECKs on
  // failure.
  void Start(const SchedulerWorkerPoolParams& params,
             int max_best_effort_tasks,
             scoped_refptr<TaskRunner> service_thread_task_runner,
             SchedulerWorkerObserver* scheduler_worker_observer,
             WorkerEnvironment worker_environment,
             Optional<TimeDelta> may_block_threshold = Optional<TimeDelta>());

  // Destroying a SchedulerWorkerPoolImpl returned by Create() is not allowed in
  // production; it is always leaked. In tests, it can only be destroyed after
  // JoinForTesting() has returned.
  ~SchedulerWorkerPoolImpl() override;

  // SchedulerWorkerPool:
  void JoinForTesting() override;
  void ReEnqueueSequence(SequenceAndTransaction sequence_and_transaction,
                         bool is_changing_pools) override;

  const HistogramBase* num_tasks_before_detach_histogram() const {
    return num_tasks_before_detach_histogram_;
  }

  const HistogramBase* num_tasks_between_waits_histogram() const {
    return num_tasks_between_waits_histogram_;
  }

  const HistogramBase* num_workers_histogram() const {
    return num_workers_histogram_;
  }

  void GetHistograms(std::vector<const HistogramBase*>* histograms) const;

  // Returns the maximum number of non-blocked tasks that can run concurrently
  // in this pool.
  //
  // TODO(fdoray): Remove this method. https://crbug.com/687264
  size_t GetMaxConcurrentNonBlockedTasksDeprecated() const;

  // Waits until at least |n| workers are idle. Note that while workers are
  // disallowed from cleaning up during this call: tests using a custom
  // |suggested_reclaim_time_| need to be careful to invoke this swiftly after
  // unblocking the waited upon workers as: if a worker is already detached by
  // the time this is invoked, it will never make it onto the idle stack and
  // this call will hang.
  void WaitForWorkersIdleForTesting(size_t n);

  // Waits until all workers are idle.
  void WaitForAllWorkersIdleForTesting();

  // Waits until |n| workers have cleaned up (since the last call to
  // WaitForWorkersCleanedUpForTesting() or Start() if it wasn't called yet).
  void WaitForWorkersCleanedUpForTesting(size_t n);

  // Returns the number of workers in this worker pool.
  size_t NumberOfWorkersForTesting() const;

  // Returns |max_tasks_|.
  size_t GetMaxTasksForTesting() const;

  // Returns the number of workers that are idle (i.e. not running tasks).
  size_t NumberOfIdleWorkersForTesting() const;

  // Records number of worker and active workers.
  void ReportHeartbeatMetrics() const;

  // Updates the position of the Sequence in |sequence_and_transaction| in
  // |shared_priority_queue| based on the Sequence's current traits.
  void UpdateSortKey(SequenceAndTransaction sequence_and_transaction);

  // Removes |sequence| from |priority_queue_|. Returns true if successful, or
  // false if |sequence| is not currently in |priority_queue_|, such as when a
  // worker is running a task from it.
  bool RemoveSequence(scoped_refptr<Sequence> sequence);

 private:
  class SchedulerWorkerStarter;
  class SchedulerWorkerDelegateImpl;

  // Friend tests so that they can access |kBlockedWorkersPollPeriod| and
  // BlockedThreshold().
  friend class TaskSchedulerWorkerPoolBlockingTest;
  friend class TaskSchedulerWorkerPoolMayBlockTest;

  // SchedulerWorkerPool:
  void OnCanScheduleSequence(scoped_refptr<Sequence> sequence) override;
  void OnCanScheduleSequence(
      SequenceAndTransaction sequence_and_transaction) override;

  // Pushes the Sequence in |sequence_and_transaction| to |priority_queue_|.
  void PushSequenceToPriorityQueue(
      SequenceAndTransaction sequence_and_transaction);

  // Waits until at least |n| workers are idle.
  void WaitForWorkersIdleLockRequiredForTesting(size_t n)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Wakes up the last worker from this worker pool to go idle, if any.
  // Otherwise, creates and starts a worker, if permitted, and wakes it up.
  void WakeUpOneWorker();

  // Performs the same action as WakeUpOneWorker(), except:
  // - Asserts |lock_| is acquired rather than acquires it.
  // - Instead of starting a worker it creates, returns it and expects that the
  //   caller will start it once |lock_| is released.
  scoped_refptr<SchedulerWorker> WakeUpOneWorkerLockRequired()
      EXCLUSIVE_LOCKS_REQUIRED(lock_) WARN_UNUSED_RESULT;

  // Creates a worker, if needed, to maintain one idle worker, |max_tasks_|
  // permitting. Expects the caller to start the returned worker once |lock_| is
  // released.
  scoped_refptr<SchedulerWorker> MaintainAtLeastOneIdleWorkerLockRequired()
      EXCLUSIVE_LOCKS_REQUIRED(lock_) WARN_UNUSED_RESULT;

  // Returns true if worker cleanup is permitted.
  bool CanWorkerCleanupForTestingLockRequired() EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Creates a worker, adds it to the pool and returns it. Expects the caller to
  // start the returned worker. Cannot be called before Start().
  scoped_refptr<SchedulerWorker> CreateAndRegisterWorkerLockRequired()
      EXCLUSIVE_LOCKS_REQUIRED(lock_) WARN_UNUSED_RESULT;

  // Returns the number of workers in the pool that should not run tasks due to
  // the pool being over capacity.
  size_t NumberOfExcessWorkersLockRequired() const
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Examines the list of SchedulerWorkers and increments |max_tasks_| for each
  // worker that has been within the scope of a MAY_BLOCK ScopedBlockingCall for
  // more than BlockedThreshold().
  void AdjustMaxTasks();

  // Returns the threshold after which the max tasks is increased to compensate
  // for a worker that is within a MAY_BLOCK ScopedBlockingCall.
  TimeDelta MayBlockThreshold() const;

  // Starts calling AdjustMaxTasks() periodically on
  // |service_thread_task_runner_|.
  void ScheduleAdjustMaxTasks();

  // Returns true if ScheduleAdjustMaxTasks() must be called.
  bool MustScheduleAdjustMaxTasksLockRequired() EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Calls AdjustMaxTasks() and schedules it again as necessary. May only be
  // called from the service thread.
  void AdjustMaxTasksFunction();

  // Returns true if AdjustMaxTasks() should periodically be called on
  // |service_thread_task_runner_|.
  bool ShouldPeriodicallyAdjustMaxTasksLockRequired()
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Increments/decrements the number of tasks that can run in this pool.
  // |is_running_best_effort_task| indicates whether the worker causing the
  // change is currently running a TaskPriority::BEST_EFFORT task.
  void DecrementMaxTasksLockRequired(bool is_running_best_effort_task)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void IncrementMaxTasksLockRequired(bool is_running_best_effort_task)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Values set at Start() and never modified afterwards.
  struct InitializedInStart {
    InitializedInStart();
    ~InitializedInStart();

#if DCHECK_IS_ON()
    // Set after all members of this struct are set.
    bool initialized = false;
#endif

    // Initial value of |max_tasks_|.
    size_t initial_max_tasks = 0;

    // Suggested reclaim time for workers.
    TimeDelta suggested_reclaim_time;

    SchedulerBackwardCompatibility backward_compatibility;

    // Environment to be initialized per worker.
    WorkerEnvironment worker_environment = WorkerEnvironment::NONE;

    scoped_refptr<TaskRunner> service_thread_task_runner;

    // Optional observer notified when a worker enters and exits its main.
    SchedulerWorkerObserver* scheduler_worker_observer = nullptr;

    // Threshold after which the max tasks is increased to compensate for a
    // worker that is within a MAY_BLOCK ScopedBlockingCall.
    TimeDelta may_block_threshold;

    // The period between calls to AdjustMaxTasks() when the pool is at
    // capacity.
    TimeDelta blocked_workers_poll_period;
  } initialized_in_start_;

  InitializedInStart& in_start() {
#if DCHECK_IS_ON()
    DCHECK(!initialized_in_start_.initialized);
#endif
    return initialized_in_start_;
  }
  const InitializedInStart& after_start() const {
#if DCHECK_IS_ON()
    DCHECK(initialized_in_start_.initialized);
#endif
    return initialized_in_start_;
  }

  const std::string pool_label_;
  const ThreadPriority priority_hint_;

  // PriorityQueue from which all threads of this worker pool get work.
  PriorityQueue priority_queue_;

  // Synchronizes accesses to all members of this class which are neither const,
  // atomic, nor InitializedInStart. Has |priority_queue_|'s lock as its
  // predecessor so that a worker can be pushed to |idle_workers_stack_| within
  // the scope of a Transaction (more details in GetWork()).
  mutable SchedulerLock lock_;

  // All workers owned by this worker pool.
  std::vector<scoped_refptr<SchedulerWorker>> workers_ GUARDED_BY(lock_);

  // The maximum number of tasks that can run concurrently in this pool. Workers
  // can be added as needed up until there are |max_tasks_| workers.
  size_t max_tasks_ GUARDED_BY(lock_) = 0;

  // The maximum number of best-effort tasks that can run concurrently in this
  // pool.
  int max_best_effort_tasks_ GUARDED_BY(lock_) = 0;

  // The number of best-effort tasks that are currently running in this pool.
  int num_running_best_effort_tasks_ GUARDED_BY(lock_) = 0;

  // Number of workers that are within the scope of a MAY_BLOCK
  // ScopedBlockingCall but haven't caused a max task increase yet.
  int num_pending_may_block_workers_ GUARDED_BY(lock_) = 0;

  // Number of workers that are running a TaskPriority::BEST_EFFORT task and are
  // within the scope of a MAY_BLOCK ScopedBlockingCall but haven't caused a max
  // task increase yet.
  int num_pending_best_effort_may_block_workers_ GUARDED_BY(lock_) = 0;

  // Stack of idle workers. Initially, all workers are on this stack. A worker
  // is removed from the stack before its WakeUp() function is called and when
  // it receives work from GetWork() (a worker calls GetWork() when its sleep
  // timeout expires, even if its WakeUp() method hasn't been called). A worker
  // is pushed on this stack when it receives nullptr from GetWork().
  SchedulerWorkerStack idle_workers_stack_ GUARDED_BY(lock_);

  // Signaled when a worker is added to the idle workers stack.
  std::unique_ptr<ConditionVariable> idle_workers_stack_cv_for_testing_
      GUARDED_BY(lock_);

  // Number of wake ups that occurred before Start(). Never modified after
  // Start() (i.e. can be read without synchronization after Start()).
  int num_wake_ups_before_start_ GUARDED_BY(lock_) = 0;

  // Stack that contains the timestamps of when workers get cleaned up.
  // Timestamps get popped off the stack as new workers are added.
  base::stack<TimeTicks, std::vector<TimeTicks>> cleanup_timestamps_
      GUARDED_BY(lock_);

  // Whether we are currently polling for necessary adjustments to |max_tasks_|.
  bool polling_max_tasks_ GUARDED_BY(lock_) = false;

  // Indicates to the delegates that workers are not permitted to cleanup.
  bool worker_cleanup_disallowed_for_testing_ GUARDED_BY(lock_) = false;

  // Counts the number of workers cleaned up since the last call to
  // WaitForWorkersCleanedUpForTesting() (or Start() if it wasn't called yet).
  // |some_workers_cleaned_up_for_testing_| is true if this was ever
  // incremented. Tests with a custom |suggested_reclaim_time_| can wait on a
  // specific number of workers being cleaned up via
  // WaitForWorkersCleanedUpForTesting().
  size_t num_workers_cleaned_up_for_testing_ GUARDED_BY(lock_) = 0;
#if DCHECK_IS_ON()
  bool some_workers_cleaned_up_for_testing_ GUARDED_BY(lock_) = false;
#endif

  // Signaled, if non-null, when |num_workers_cleaned_up_for_testing_| is
  // incremented.
  std::unique_ptr<ConditionVariable> num_workers_cleaned_up_for_testing_cv_
      GUARDED_BY(lock_);

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

  // TaskScheduler.NumWorkers.[worker pool name] histogram.
  // Intentionally leaked.
  HistogramBase* const num_workers_histogram_;

  // TaskScheduler.NumActiveWorkers.[worker pool name] histogram.
  // Intentionally leaked.
  HistogramBase* const num_active_workers_histogram_;

  // Ensures recently cleaned up workers (ref.
  // SchedulerWorkerDelegateImpl::CleanupLockRequired()) had time to exit as
  // they have a raw reference to |this| (and to TaskTracker) which can
  // otherwise result in racy use-after-frees per no longer being part of
  // |workers_| and hence not being explicitly joined in JoinForTesting():
  // https://crbug.com/810464. Uses AtomicRefCount to make its only public
  // method thread-safe.
  TrackedRefFactory<SchedulerWorkerPoolImpl> tracked_ref_factory_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerPoolImpl);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_IMPL_H_
