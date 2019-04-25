// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/scheduler_worker_pool_impl.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "base/atomicops.h"
#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram.h"
#include "base/numerics/clamped_math.h"
#include "base/sequence_token.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/task_features.h"
#include "base/task/task_scheduler/scheduler_worker_pool_params.h"
#include "base/task/task_scheduler/task_tracker.h"
#include "base/task/task_traits.h"
#include "base/threading/platform_thread.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_restrictions.h"

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_windows_thread_environment.h"
#include "base/win/scoped_winrt_initializer.h"
#include "base/win/windows_version.h"
#endif  // defined(OS_WIN)

namespace base {
namespace internal {

namespace {

constexpr char kPoolNameSuffix[] = "Pool";
constexpr char kDetachDurationHistogramPrefix[] =
    "TaskScheduler.DetachDuration.";
constexpr char kNumTasksBeforeDetachHistogramPrefix[] =
    "TaskScheduler.NumTasksBeforeDetach.";
constexpr char kNumTasksBetweenWaitsHistogramPrefix[] =
    "TaskScheduler.NumTasksBetweenWaits.";
constexpr char kNumWorkersHistogramPrefix[] = "TaskScheduler.NumWorkers.";
constexpr char kNumActiveWorkersHistogramPrefix[] =
    "TaskScheduler.NumActiveWorkers.";
constexpr size_t kMaxNumberOfWorkers = 256;

// In a background pool:
// - Blocking calls take more time than in a foreground pool.
// - We want to minimize impact on foreground work, not maximize execution
//   throughput.
// For these reasons, the timeout to increase the maximum number of concurrent
// tasks when there is a MAY_BLOCK ScopedBlockingCall is *long*. It is not
// infinite because execution throughput should not be reduced forever if a task
// blocks forever.
//
// TODO(fdoray): On platforms without background pools, blocking in a
// BEST_EFFORT task should:
// 1. Increment the maximum number of concurrent tasks after a *short* timeout,
//    to allow scheduling of USER_VISIBLE/USER_BLOCKING tasks.
// 2. Increment the maximum number of concurrent BEST_EFFORT tasks after a
//    *long* timeout, because we only want to allow more BEST_EFFORT tasks to be
//    be scheduled concurrently when we believe that a BEST_EFFORT task is
//    blocked forever.
// Currently, only 1. is true as the configuration is per pool.
// TODO(https://crbug.com/927755): Fix racy condition when MayBlockThreshold ==
// BlockedWorkersPoll.
constexpr TimeDelta kForegroundMayBlockThreshold =
    TimeDelta::FromMilliseconds(1000);
constexpr TimeDelta kForegroundBlockedWorkersPoll =
    TimeDelta::FromMilliseconds(1200);
constexpr TimeDelta kBackgroundMayBlockThreshold = TimeDelta::FromSeconds(10);
constexpr TimeDelta kBackgroundBlockedWorkersPoll = TimeDelta::FromSeconds(12);

// Only used in DCHECKs.
bool ContainsWorker(const std::vector<scoped_refptr<SchedulerWorker>>& workers,
                    const SchedulerWorker* worker) {
  auto it = std::find_if(workers.begin(), workers.end(),
                         [worker](const scoped_refptr<SchedulerWorker>& i) {
                           return i.get() == worker;
                         });
  return it != workers.end();
}

}  // namespace

// Accumulates workers and executes actions on them upon destruction. Useful
// to satisfy locking requirements of worker actions.
class SchedulerWorkerPoolImpl::SchedulerWorkerActionExecutor {
 public:
  SchedulerWorkerActionExecutor(SchedulerWorkerPoolImpl* outer)
      : outer_(outer) {}

  ~SchedulerWorkerActionExecutor() { FlushImpl(); }

  void ScheduleWakeUp(scoped_refptr<SchedulerWorker> worker) {
    workers_to_wake_up_.AddWorker(std::move(worker));
  }

  void ScheduleStart(scoped_refptr<SchedulerWorker> worker) {
    workers_to_start_.AddWorker(std::move(worker));
  }

  void Flush(SchedulerLock* held_lock) {
    if (workers_to_wake_up_.empty() && workers_to_start_.empty())
      return;
    AutoSchedulerUnlock auto_unlock(*held_lock);
    FlushImpl();
    workers_to_wake_up_.clear();
    workers_to_start_.clear();
  }

 private:
  class WorkerContainer {
   public:
    WorkerContainer() = default;

    void AddWorker(scoped_refptr<SchedulerWorker> worker) {
      if (!worker)
        return;
      if (!first_worker_)
        first_worker_ = std::move(worker);
      else
        additional_workers_.push_back(std::move(worker));
    }

    template <typename Action>
    void ForEachWorker(Action action) {
      if (first_worker_) {
        action(first_worker_.get());
        for (scoped_refptr<SchedulerWorker> worker : additional_workers_)
          action(worker.get());
      } else {
        DCHECK(additional_workers_.empty());
      }
    }

    bool empty() const { return first_worker_ == nullptr; }

    void clear() {
      first_worker_.reset();
      additional_workers_.clear();
    }

   private:
    // The purpose of |first_worker| is to avoid a heap allocation by the vector
    // in the case where there is only one worker in the container.
    scoped_refptr<SchedulerWorker> first_worker_;
    std::vector<scoped_refptr<SchedulerWorker>> additional_workers_;

    DISALLOW_COPY_AND_ASSIGN(WorkerContainer);
  };

  void FlushImpl() {
    SchedulerLock::AssertNoLockHeldOnCurrentThread();

    // Wake up workers.
    workers_to_wake_up_.ForEachWorker(
        [](SchedulerWorker* worker) { worker->WakeUp(); });

    // Start workers. Happens after wake ups to prevent the case where a worker
    // enters its main function, is descheduled because it wasn't woken up yet,
    // and is woken up immediately after.
    workers_to_start_.ForEachWorker([&](SchedulerWorker* worker) {
      worker->Start(outer_->after_start().scheduler_worker_observer);
    });
  }

  SchedulerWorkerPoolImpl* const outer_;

  WorkerContainer workers_to_wake_up_;
  WorkerContainer workers_to_start_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerActionExecutor);
};

class SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl
    : public SchedulerWorker::Delegate,
      public BlockingObserver {
 public:
  // |outer| owns the worker for which this delegate is constructed.
  SchedulerWorkerDelegateImpl(TrackedRef<SchedulerWorkerPoolImpl> outer);
  ~SchedulerWorkerDelegateImpl() override;

  // SchedulerWorker::Delegate:
  void OnCanScheduleSequence(scoped_refptr<Sequence> sequence) override;
  SchedulerWorker::ThreadLabel GetThreadLabel() const override;
  void OnMainEntry(const SchedulerWorker* worker) override;
  scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) override;
  void DidRunTask(scoped_refptr<Sequence> sequence) override;
  TimeDelta GetSleepTimeout() override;
  void OnMainExit(SchedulerWorker* worker) override;

  // BlockingObserver:
  void BlockingStarted(BlockingType blocking_type) override;
  void BlockingTypeUpgraded() override;
  void BlockingEnded() override;

  void MayBlockEntered();
  void WillBlockEntered();

  // Returns true iff the worker can get work. Cleans up the worker or puts it
  // on the idle stack if it can't get work.
  bool CanGetWorkLockRequired(SchedulerWorker* worker)
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_);

  // Returns true iff this worker has been within a MAY_BLOCK ScopedBlockingCall
  // for more than |may_block_threshold|. The max tasks must be
  // incremented if this returns true.
  bool MustIncrementMaxTasksLockRequired()
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_);

  bool is_running_best_effort_task_lock_required() const
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_) {
    return read_any().is_running_best_effort_task;
  }

  // Exposed for AnnotateSchedulerLockAcquired in
  // SchedulerWorkerPoolImpl::AdjustMaxTasks()
  const SchedulerLock& lock() const LOCK_RETURNED(outer_->lock_) {
    return outer_->lock_;
  }

 private:
  // Returns true if |worker| is allowed to cleanup and remove itself from the
  // pool. Called from GetWork() when no work is available.
  bool CanCleanupLockRequired(const SchedulerWorker* worker) const
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_);

  // Calls cleanup on |worker| and removes it from the pool. Called from
  // GetWork() when no work is available and CanCleanupLockRequired() returns
  // true.
  void CleanupLockRequired(SchedulerWorker* worker)
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_);

  // Called in GetWork() when a worker becomes idle.
  void OnWorkerBecomesIdleLockRequired(SchedulerWorker* worker)
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_);

  // Accessed only from the worker thread.
  struct WorkerOnly {
    // Number of tasks executed since the last time the
    // TaskScheduler.NumTasksBetweenWaits histogram was recorded.
    size_t num_tasks_since_last_wait = 0;

    // Number of tasks executed since the last time the
    // TaskScheduler.NumTasksBeforeDetach histogram was recorded.
    size_t num_tasks_since_last_detach = 0;

    // Whether the worker is currently running a task (i.e. GetWork() has
    // returned a non-empty sequence and DidRunTask() hasn't been called yet).
    bool is_running_task = false;

#if defined(OS_WIN)
    std::unique_ptr<win::ScopedWindowsThreadEnvironment> win_thread_environment;
#endif  // defined(OS_WIN)
  } worker_only_;

  // Writes from the worker thread protected by |outer_->lock_|. Reads from any
  // thread, protected by |outer_->lock_| when not on the worker thread.
  struct WriteWorkerReadAny {
    // Whether the worker is currently running a TaskPriority::BEST_EFFORT task.
    bool is_running_best_effort_task = false;

    // Time when MayBlockScopeEntered() was last called. Reset when
    // BlockingScopeExited() is called.
    TimeTicks may_block_start_time;
  } write_worker_read_any_;

  WorkerOnly& worker_only() {
    DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
    return worker_only_;
  }

  WriteWorkerReadAny& write_worker() EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_) {
    DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
    return write_worker_read_any_;
  }

  const WriteWorkerReadAny& read_any() const
      EXCLUSIVE_LOCKS_REQUIRED(outer_->lock_) {
    return write_worker_read_any_;
  }

  const WriteWorkerReadAny& read_worker() const {
    DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
    return write_worker_read_any_;
  }

  const TrackedRef<SchedulerWorkerPoolImpl> outer_;

  // Whether |outer_->max_tasks_| was incremented due to a ScopedBlockingCall on
  // the thread.
  bool incremented_max_tasks_since_blocked_ GUARDED_BY(outer_->lock_) = false;

  // Verifies that specific calls are always made from the worker thread.
  THREAD_CHECKER(worker_thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerDelegateImpl);
};

SchedulerWorkerPoolImpl::SchedulerWorkerPoolImpl(
    StringPiece histogram_label,
    StringPiece pool_label,
    ThreadPriority priority_hint,
    TrackedRef<TaskTracker> task_tracker,
    TrackedRef<Delegate> delegate)
    : SchedulerWorkerPool(std::move(task_tracker), std::move(delegate)),
      pool_label_(pool_label.as_string()),
      priority_hint_(priority_hint),
      idle_workers_stack_cv_for_testing_(lock_.CreateConditionVariable()),
      // Mimics the UMA_HISTOGRAM_LONG_TIMES macro.
      detach_duration_histogram_(Histogram::FactoryTimeGet(
          JoinString({kDetachDurationHistogramPrefix, histogram_label,
                      kPoolNameSuffix},
                     ""),
          TimeDelta::FromMilliseconds(1),
          TimeDelta::FromHours(1),
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      // Mimics the UMA_HISTOGRAM_COUNTS_1000 macro. When a worker runs more
      // than 1000 tasks before detaching, there is no need to know the exact
      // number of tasks that ran.
      num_tasks_before_detach_histogram_(Histogram::FactoryGet(
          JoinString({kNumTasksBeforeDetachHistogramPrefix, histogram_label,
                      kPoolNameSuffix},
                     ""),
          1,
          1000,
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      // Mimics the UMA_HISTOGRAM_COUNTS_100 macro. A SchedulerWorker is
      // expected to run between zero and a few tens of tasks between waits.
      // When it runs more than 100 tasks, there is no need to know the exact
      // number of tasks that ran.
      num_tasks_between_waits_histogram_(Histogram::FactoryGet(
          JoinString({kNumTasksBetweenWaitsHistogramPrefix, histogram_label,
                      kPoolNameSuffix},
                     ""),
          1,
          100,
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      // Mimics the UMA_HISTOGRAM_COUNTS_100 macro. A SchedulerWorkerPool is
      // expected to run between zero and a few tens of workers.
      // When it runs more than 100 worker, there is no need to know the exact
      // number of workers that ran.
      num_workers_histogram_(Histogram::FactoryGet(
          JoinString(
              {kNumWorkersHistogramPrefix, histogram_label, kPoolNameSuffix},
              ""),
          1,
          100,
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      num_active_workers_histogram_(
          Histogram::FactoryGet(JoinString({kNumActiveWorkersHistogramPrefix,
                                            histogram_label, kPoolNameSuffix},
                                           ""),
                                1,
                                100,
                                50,
                                HistogramBase::kUmaTargetedHistogramFlag)),
      tracked_ref_factory_(this) {
  DCHECK(!histogram_label.empty());
  DCHECK(!pool_label_.empty());
}

void SchedulerWorkerPoolImpl::Start(
    const SchedulerWorkerPoolParams& params,
    int max_best_effort_tasks,
    scoped_refptr<TaskRunner> service_thread_task_runner,
    SchedulerWorkerObserver* scheduler_worker_observer,
    WorkerEnvironment worker_environment,
    Optional<TimeDelta> may_block_threshold) {
  SchedulerWorkerActionExecutor executor(this);

  AutoSchedulerLock auto_lock(lock_);

  DCHECK(workers_.empty());

  in_start().may_block_without_delay =
      FeatureList::IsEnabled(kMayBlockWithoutDelay);
  in_start().may_block_threshold =
      may_block_threshold ? may_block_threshold.value()
                          : (priority_hint_ == ThreadPriority::NORMAL
                                 ? kForegroundMayBlockThreshold
                                 : kBackgroundMayBlockThreshold);
  in_start().blocked_workers_poll_period =
      priority_hint_ == ThreadPriority::NORMAL ? kForegroundBlockedWorkersPoll
                                               : kBackgroundBlockedWorkersPoll;

  max_tasks_ = params.max_tasks();
  DCHECK_GE(max_tasks_, 1U);
  in_start().initial_max_tasks = max_tasks_;
  DCHECK_LE(in_start().initial_max_tasks, kMaxNumberOfWorkers);
  max_best_effort_tasks_ = max_best_effort_tasks;
  in_start().suggested_reclaim_time = params.suggested_reclaim_time();
  in_start().backward_compatibility = params.backward_compatibility();
  in_start().worker_environment = worker_environment;
  in_start().service_thread_task_runner = std::move(service_thread_task_runner);
  in_start().scheduler_worker_observer = scheduler_worker_observer;

#if DCHECK_IS_ON()
  in_start().initialized = true;
#endif

  EnsureEnoughWorkersLockRequired(&executor);
}

SchedulerWorkerPoolImpl::~SchedulerWorkerPoolImpl() {
  // SchedulerWorkerPool should only ever be deleted:
  //  1) In tests, after JoinForTesting().
  //  2) In production, iff initialization failed.
  // In both cases |workers_| should be empty.
  DCHECK(workers_.empty());
}

void SchedulerWorkerPoolImpl::OnCanScheduleSequence(
    scoped_refptr<Sequence> sequence) {
  OnCanScheduleSequence(
      SequenceAndTransaction::FromSequence(std::move(sequence)));
}

void SchedulerWorkerPoolImpl::OnCanScheduleSequence(
    SequenceAndTransaction sequence_and_transaction) {
  PushSequenceAndWakeUpWorkers(std::move(sequence_and_transaction));
}

void SchedulerWorkerPoolImpl::PushSequenceAndWakeUpWorkers(
    SequenceAndTransaction sequence_and_transaction) {
  bool must_schedule_adjust_max_tasks;
  SchedulerWorkerActionExecutor executor(this);
  {
    AutoSchedulerLock auto_lock(lock_);
    priority_queue_.Push(std::move(sequence_and_transaction.sequence),
                         sequence_and_transaction.transaction.GetSortKey());
    EnsureEnoughWorkersLockRequired(&executor);
    must_schedule_adjust_max_tasks = MustScheduleAdjustMaxTasksLockRequired();
    // Terminate the Sequence transaction at the end of this scope to avoid
    // holding a lock when calling ScheduleAdjustMaxTasks().
    auto terminate_sequence_transaction = std::move(sequence_and_transaction);
  }
  if (must_schedule_adjust_max_tasks)
    ScheduleAdjustMaxTasks();
}

size_t SchedulerWorkerPoolImpl::GetMaxConcurrentNonBlockedTasksDeprecated()
    const {
#if DCHECK_IS_ON()
  AutoSchedulerLock auto_lock(lock_);
  DCHECK_NE(after_start().initial_max_tasks, 0U)
      << "GetMaxConcurrentTasksDeprecated() should only be called after the "
      << "worker pool has started.";
#endif
  return after_start().initial_max_tasks;
}

void SchedulerWorkerPoolImpl::WaitForWorkersIdleForTesting(size_t n) {
  AutoSchedulerLock auto_lock(lock_);

#if DCHECK_IS_ON()
  DCHECK(!some_workers_cleaned_up_for_testing_)
      << "Workers detached prior to waiting for a specific number of idle "
         "workers. Doing the wait under such conditions is flaky. Consider "
         "setting the suggested reclaim time to TimeDelta::Max() in Start().";
#endif

  WaitForWorkersIdleLockRequiredForTesting(n);
}

void SchedulerWorkerPoolImpl::WaitForAllWorkersIdleForTesting() {
  AutoSchedulerLock auto_lock(lock_);
  WaitForWorkersIdleLockRequiredForTesting(workers_.size());
}

void SchedulerWorkerPoolImpl::WaitForWorkersCleanedUpForTesting(size_t n) {
  AutoSchedulerLock auto_lock(lock_);

  if (!num_workers_cleaned_up_for_testing_cv_)
    num_workers_cleaned_up_for_testing_cv_ = lock_.CreateConditionVariable();

  while (num_workers_cleaned_up_for_testing_ < n)
    num_workers_cleaned_up_for_testing_cv_->Wait();

  num_workers_cleaned_up_for_testing_ = 0;
}

void SchedulerWorkerPoolImpl::JoinForTesting() {
#if DCHECK_IS_ON()
  join_for_testing_started_.Set();
#endif

  decltype(workers_) workers_copy;
  {
    AutoSchedulerLock auto_lock(lock_);
    priority_queue_.EnableFlushSequencesOnDestroyForTesting();

    DCHECK_GT(workers_.size(), size_t(0)) << "Joined an unstarted worker pool.";

    // Ensure SchedulerWorkers in |workers_| do not attempt to cleanup while
    // being joined.
    worker_cleanup_disallowed_for_testing_ = true;

    // Make a copy of the SchedulerWorkers so that we can call
    // SchedulerWorker::JoinForTesting() without holding |lock_| since
    // SchedulerWorkers may need to access |workers_|.
    workers_copy = workers_;
  }
  for (const auto& worker : workers_copy)
    worker->JoinForTesting();

  AutoSchedulerLock auto_lock(lock_);
  DCHECK(workers_ == workers_copy);
  // Release |workers_| to clear their TrackedRef against |this|.
  workers_.clear();
}

void SchedulerWorkerPoolImpl::ReEnqueueSequenceChangingPool(
    SequenceAndTransaction sequence_and_transaction) {
  PushSequenceAndWakeUpWorkers(std::move(sequence_and_transaction));
}

size_t SchedulerWorkerPoolImpl::NumberOfWorkersForTesting() const {
  AutoSchedulerLock auto_lock(lock_);
  return workers_.size();
}

size_t SchedulerWorkerPoolImpl::GetMaxTasksForTesting() const {
  AutoSchedulerLock auto_lock(lock_);
  return max_tasks_;
}

size_t SchedulerWorkerPoolImpl::NumberOfIdleWorkersForTesting() const {
  AutoSchedulerLock auto_lock(lock_);
  return idle_workers_stack_.Size();
}

void SchedulerWorkerPoolImpl::ReportHeartbeatMetrics() const {
  AutoSchedulerLock auto_lock(lock_);
  num_workers_histogram_->Add(workers_.size());

  num_active_workers_histogram_->Add(workers_.size() -
                                     idle_workers_stack_.Size());
}

SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    SchedulerWorkerDelegateImpl(TrackedRef<SchedulerWorkerPoolImpl> outer)
    : outer_(std::move(outer)) {
  // Bound in OnMainEntry().
  DETACH_FROM_THREAD(worker_thread_checker_);
}

// OnMainExit() handles the thread-affine cleanup; SchedulerWorkerDelegateImpl
// can thereafter safely be deleted from any thread.
SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    ~SchedulerWorkerDelegateImpl() = default;

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    OnCanScheduleSequence(scoped_refptr<Sequence> sequence) {
  outer_->OnCanScheduleSequence(std::move(sequence));
}

SchedulerWorker::ThreadLabel
SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::GetThreadLabel() const {
  return SchedulerWorker::ThreadLabel::POOLED;
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::OnMainEntry(
    const SchedulerWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);

  {
#if DCHECK_IS_ON()
    AutoSchedulerLock auto_lock(outer_->lock_);
    DCHECK(ContainsWorker(outer_->workers_, worker));
#endif
  }

#if defined(OS_WIN)
  if (outer_->after_start().worker_environment == WorkerEnvironment::COM_MTA) {
    if (win::GetVersion() >= win::VERSION_WIN8) {
      worker_only().win_thread_environment =
          std::make_unique<win::ScopedWinrtInitializer>();
    } else {
      worker_only().win_thread_environment =
          std::make_unique<win::ScopedCOMInitializer>(
              win::ScopedCOMInitializer::kMTA);
    }
    DCHECK(worker_only().win_thread_environment->Succeeded());
  }
#endif  // defined(OS_WIN)

  DCHECK_EQ(worker_only().num_tasks_since_last_wait, 0U);

  PlatformThread::SetName(
      StringPrintf("TaskScheduler%sWorker", outer_->pool_label_.c_str()));

  outer_->BindToCurrentThread();
  SetBlockingObserverForCurrentThread(this);
}

scoped_refptr<Sequence>
SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::GetWork(
    SchedulerWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(!worker_only().is_running_task);
  DCHECK(!read_worker().is_running_best_effort_task);

  SchedulerWorkerActionExecutor executor(outer_.get());
  AutoSchedulerLock auto_lock(outer_->lock_);

  DCHECK(ContainsWorker(outer_->workers_, worker));

  // Use this opportunity, before assigning work to this worker, to create/wake
  // additional workers if needed (doing this here allows us to reduce
  // potentially expensive create/wake directly on PostTask()).
  outer_->EnsureEnoughWorkersLockRequired(&executor);
  executor.Flush(&outer_->lock_);

  if (!CanGetWorkLockRequired(worker))
    return nullptr;

  if (outer_->priority_queue_.IsEmpty()) {
    OnWorkerBecomesIdleLockRequired(worker);
    return nullptr;
  }

  // Enforce that no more than |max_best_effort_tasks_| BEST_EFFORT tasks run
  // concurrently.
  const bool next_sequence_is_best_effort =
      outer_->priority_queue_.PeekSortKey().priority() ==
      TaskPriority::BEST_EFFORT;
  if (next_sequence_is_best_effort && outer_->num_running_best_effort_tasks_ >=
                                          outer_->max_best_effort_tasks_) {
    OnWorkerBecomesIdleLockRequired(worker);
    return nullptr;
  }

  // Running task bookkeeping.
  worker_only().is_running_task = true;
  ++outer_->num_running_tasks_;
  DCHECK(!outer_->idle_workers_stack_.Contains(worker));

  // Running BEST_EFFORT task bookkeeping.
  if (next_sequence_is_best_effort) {
    write_worker().is_running_best_effort_task = true;
    ++outer_->num_running_best_effort_tasks_;
  }

  // Pop the Sequence from which to run a task from the PriorityQueue.
  scoped_refptr<Sequence> sequence = outer_->priority_queue_.PopSequence();

  // Sanity check: A worker should not get work if the number of awake workers
  // is more than the *desired* number of awake workers. It should instead be
  // added to the idle stack.
  DCHECK_LE(outer_->GetNumAwakeWorkersLockRequired(),
            outer_->GetDesiredNumAwakeWorkersLockRequired());

  return sequence;
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::DidRunTask(
    scoped_refptr<Sequence> sequence) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);
  DCHECK(read_worker().may_block_start_time.is_null());

  ++worker_only().num_tasks_since_last_wait;
  ++worker_only().num_tasks_since_last_detach;

  // A transaction to the Sequence to reenqueue, if any. Instantiated here as
  // |Sequence::lock_| is a UniversalPredecessor and must always be acquired
  // prior to acquiring a second lock
  Optional<SequenceAndTransaction> sequence_to_reenqueue_and_transaction;
  if (sequence) {
    sequence_to_reenqueue_and_transaction.emplace(
        SequenceAndTransaction::FromSequence(std::move(sequence)));
  }

  // The pool in which to reenqueue the Sequence. Initialized below and used
  // outside the lock after.
  SchedulerWorkerPool* destination_pool;

  {
    AutoSchedulerLock auto_lock(outer_->lock_);

    DCHECK(!incremented_max_tasks_since_blocked_);

    // Running task bookkeeping.
    DCHECK_GT(outer_->num_running_tasks_, 0U);
    --outer_->num_running_tasks_;
    worker_only().is_running_task = false;

    // Running BEST_EFFORT task bookkeeping.
    if (read_worker().is_running_best_effort_task) {
      DCHECK_GT(outer_->num_running_best_effort_tasks_, 0U);
      --outer_->num_running_best_effort_tasks_;
      write_worker().is_running_best_effort_task = false;
    }

    if (!sequence_to_reenqueue_and_transaction)
      return;

    // Decide in which pool the Sequence should be reenqueued.
    destination_pool = outer_->delegate_->GetWorkerPoolForTraits(
        sequence_to_reenqueue_and_transaction->transaction.traits());

    // If the Sequence should be reenqueued in the current pool, reenqueue it
    // *before* releasing the lock. Note: No wake up needed because the current
    // worker will pop a Sequence from the PriorityQueue after this returns.
    if (outer_ == destination_pool) {
      outer_->priority_queue_.Push(
          std::move(sequence_to_reenqueue_and_transaction->sequence),
          sequence_to_reenqueue_and_transaction->transaction.GetSortKey());
      return;
    }
  }

  // If the Sequence should be reenqueued in a different pool, reenqueue it
  // *after* releasing the lock.
  destination_pool->ReEnqueueSequenceChangingPool(
      std::move(sequence_to_reenqueue_and_transaction.value()));
}

TimeDelta
SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::GetSleepTimeout() {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  // Sleep for an extra 10% to avoid the following pathological case:

  //   0) A task is running on a timer which matches
  //      |after_start().suggested_reclaim_time|.
  //   1) The timer fires and this worker is created by
  //      MaintainAtLeastOneIdleWorkerLockRequired() because the last idle
  //      worker was assigned the task.
  //   2) This worker begins sleeping |after_start().suggested_reclaim_time| (on
  //      top of the idle stack).
  //   3) The task assigned to the other worker completes and the worker goes
  //      back on the idle stack (this worker is now second on the idle stack;
  //      its GetLastUsedTime() is set to Now()).
  //   4) The sleep in (2) expires. Since (3) was fast this worker is likely to
  //      have been second on the idle stack long enough for
  //      CanCleanupLockRequired() to be satisfied in which case this worker is
  //      cleaned up.
  //   5) The timer fires at roughly the same time and we're back to (1) if (4)
  //      resulted in a clean up; causing thread churn.
  //
  //   Sleeping 10% longer in (2) makes it much less likely that (4) occurs
  //   before (5). In that case (5) will cause (3) and refresh this worker's
  //   GetLastUsedTime(), making CanCleanupLockRequired() return false in (4)
  //   and avoiding churn.
  //
  //   Of course the same problem arises if in (0) the timer matches
  //   |after_start().suggested_reclaim_time * 1.1| but it's expected that any
  //   timer slower than |after_start().suggested_reclaim_time| will cause such
  //   churn during long idle periods. If this is a problem in practice, the
  //   standby thread configuration and algorithm should be revisited.
  return outer_->after_start().suggested_reclaim_time * 1.1;
}

bool SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    CanCleanupLockRequired(const SchedulerWorker* worker) const {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);

  const TimeTicks last_used_time = worker->GetLastUsedTime();
  return !last_used_time.is_null() &&
         TimeTicks::Now() - last_used_time >=
             outer_->after_start().suggested_reclaim_time &&
         (outer_->workers_.size() > outer_->after_start().initial_max_tasks ||
          !FeatureList::IsEnabled(kNoDetachBelowInitialCapacity)) &&
         LIKELY(!outer_->worker_cleanup_disallowed_for_testing_);
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::CleanupLockRequired(
    SchedulerWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);

  outer_->num_tasks_before_detach_histogram_->Add(
      worker_only().num_tasks_since_last_detach);
  outer_->cleanup_timestamps_.push(TimeTicks::Now());
  worker->Cleanup();
  outer_->idle_workers_stack_.Remove(worker);

  // Remove the worker from |workers_|.
  auto worker_iter =
      std::find(outer_->workers_.begin(), outer_->workers_.end(), worker);
  DCHECK(worker_iter != outer_->workers_.end());
  outer_->workers_.erase(worker_iter);

  ++outer_->num_workers_cleaned_up_for_testing_;
#if DCHECK_IS_ON()
  outer_->some_workers_cleaned_up_for_testing_ = true;
#endif
  if (outer_->num_workers_cleaned_up_for_testing_cv_)
    outer_->num_workers_cleaned_up_for_testing_cv_->Signal();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    OnWorkerBecomesIdleLockRequired(SchedulerWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);

  // Record the TaskScheduler.NumTasksBetweenWaits histogram. After GetWork()
  // returns nullptr, the SchedulerWorker will perform a wait on its
  // WaitableEvent, so we record how many tasks were ran since the last wait
  // here.
  outer_->num_tasks_between_waits_histogram_->Add(
      worker_only().num_tasks_since_last_wait);
  worker_only().num_tasks_since_last_wait = 0;

  // Add the worker to the idle stack.
  DCHECK(!outer_->idle_workers_stack_.Contains(worker));
  outer_->idle_workers_stack_.Push(worker);
  DCHECK_LE(outer_->idle_workers_stack_.Size(), outer_->workers_.size());
  outer_->idle_workers_stack_cv_for_testing_->Broadcast();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::OnMainExit(
    SchedulerWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);

#if DCHECK_IS_ON()
  {
    bool shutdown_complete = outer_->task_tracker_->IsShutdownComplete();
    AutoSchedulerLock auto_lock(outer_->lock_);

    // |worker| should already have been removed from the idle workers stack and
    // |workers_| by the time the thread is about to exit. (except in the cases
    // where the pool is no longer going to be used - in which case, it's fine
    // for there to be invalid workers in the pool.
    if (!shutdown_complete && !outer_->join_for_testing_started_.IsSet()) {
      DCHECK(!outer_->idle_workers_stack_.Contains(worker));
      DCHECK(!ContainsWorker(outer_->workers_, worker));
    }
  }
#endif

#if defined(OS_WIN)
  worker_only().win_thread_environment.reset();
#endif  // defined(OS_WIN)
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::BlockingStarted(
    BlockingType blocking_type) {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);

  // MayBlock with no delay reuses WillBlock implementation.
  if (outer_->after_start().may_block_without_delay)
    blocking_type = BlockingType::WILL_BLOCK;

  switch (blocking_type) {
    case BlockingType::MAY_BLOCK:
      MayBlockEntered();
      break;
    case BlockingType::WILL_BLOCK:
      WillBlockEntered();
      break;
  }
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    BlockingTypeUpgraded() {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);

  // The blocking type always being WILL_BLOCK in this experiment, it should
  // never be considered "upgraded".
  if (outer_->after_start().may_block_without_delay)
    return;

  {
    AutoSchedulerLock auto_lock(outer_->lock_);

    // Don't do anything if a MAY_BLOCK ScopedBlockingCall instantiated in the
    // same scope already caused the max tasks to be incremented.
    if (incremented_max_tasks_since_blocked_)
      return;

    // Cancel the effect of a MAY_BLOCK ScopedBlockingCall instantiated in the
    // same scope.
    if (!read_worker().may_block_start_time.is_null()) {
      write_worker().may_block_start_time = TimeTicks();
      --outer_->num_unresolved_may_block_;
      if (read_worker().is_running_best_effort_task)
        --outer_->num_unresolved_best_effort_may_block_;
    }
  }

  WillBlockEntered();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::BlockingEnded() {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);

  AutoSchedulerLock auto_lock(outer_->lock_);
  if (incremented_max_tasks_since_blocked_) {
    outer_->DecrementMaxTasksLockRequired(
        read_worker().is_running_best_effort_task);
  } else {
    DCHECK(!read_worker().may_block_start_time.is_null());
    --outer_->num_unresolved_may_block_;
    if (read_worker().is_running_best_effort_task)
      --outer_->num_unresolved_best_effort_may_block_;
  }

  incremented_max_tasks_since_blocked_ = false;
  write_worker().may_block_start_time = TimeTicks();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::MayBlockEntered() {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);

  bool must_schedule_adjust_max_tasks = false;
  {
    AutoSchedulerLock auto_lock(outer_->lock_);

    DCHECK(!incremented_max_tasks_since_blocked_);
    DCHECK(read_worker().may_block_start_time.is_null());
    write_worker().may_block_start_time = TimeTicks::Now();
    ++outer_->num_unresolved_may_block_;
    if (read_worker().is_running_best_effort_task)
      ++outer_->num_unresolved_best_effort_may_block_;

    must_schedule_adjust_max_tasks =
        outer_->MustScheduleAdjustMaxTasksLockRequired();
  }
  if (must_schedule_adjust_max_tasks)
    outer_->ScheduleAdjustMaxTasks();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::WillBlockEntered() {
  DCHECK_CALLED_ON_VALID_THREAD(worker_thread_checker_);
  DCHECK(worker_only().is_running_task);

  SchedulerWorkerActionExecutor executor(outer_.get());
  AutoSchedulerLock auto_lock(outer_->lock_);

  DCHECK(!incremented_max_tasks_since_blocked_);
  DCHECK(read_worker().may_block_start_time.is_null());
  incremented_max_tasks_since_blocked_ = true;
  outer_->IncrementMaxTasksLockRequired(
      read_worker().is_running_best_effort_task);
  outer_->EnsureEnoughWorkersLockRequired(&executor);
}

bool SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    CanGetWorkLockRequired(SchedulerWorker* worker) {
  // To avoid searching through the idle stack : use GetLastUsedTime() not being
  // null (or being directly on top of the idle stack) as a proxy for being on
  // the idle stack.
  const bool is_on_idle_workers_stack =
      outer_->idle_workers_stack_.Peek() == worker ||
      !worker->GetLastUsedTime().is_null();
  DCHECK_EQ(is_on_idle_workers_stack,
            outer_->idle_workers_stack_.Contains(worker));

  if (is_on_idle_workers_stack) {
    if (CanCleanupLockRequired(worker))
      CleanupLockRequired(worker);
    return false;
  }

  // Excess workers should not get work, until they are no longer excess (i.e.
  // max tasks increases). This ensures that if we have excess workers in the
  // pool, they get a chance to no longer be excess before being cleaned up.
  if (outer_->GetNumAwakeWorkersLockRequired() >
      outer_->GetDesiredNumAwakeWorkersLockRequired()) {
    OnWorkerBecomesIdleLockRequired(worker);
    return false;
  }

  return true;
}

bool SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    MustIncrementMaxTasksLockRequired() {
  if (!incremented_max_tasks_since_blocked_ &&
      !read_any().may_block_start_time.is_null() &&
      TimeTicks::Now() - read_any().may_block_start_time >=
          outer_->after_start().may_block_threshold) {
    incremented_max_tasks_since_blocked_ = true;

    --outer_->num_unresolved_may_block_;
    if (read_any().is_running_best_effort_task)
      --outer_->num_unresolved_best_effort_may_block_;

    return true;
  }

  return false;
}

void SchedulerWorkerPoolImpl::WaitForWorkersIdleLockRequiredForTesting(
    size_t n) {
  // Make sure workers do not cleanup while watching the idle count.
  AutoReset<bool> ban_cleanups(&worker_cleanup_disallowed_for_testing_, true);

  while (idle_workers_stack_.Size() < n)
    idle_workers_stack_cv_for_testing_->Wait();
}

void SchedulerWorkerPoolImpl::MaintainAtLeastOneIdleWorkerLockRequired(
    SchedulerWorkerActionExecutor* executor) {
  if (workers_.size() == kMaxNumberOfWorkers)
    return;
  DCHECK_LT(workers_.size(), kMaxNumberOfWorkers);

  if (!idle_workers_stack_.IsEmpty())
    return;

  if (workers_.size() >= max_tasks_)
    return;

  scoped_refptr<SchedulerWorker> new_worker =
      CreateAndRegisterWorkerLockRequired(executor);
  DCHECK(new_worker);
  idle_workers_stack_.Push(new_worker.get());
}

scoped_refptr<SchedulerWorker>
SchedulerWorkerPoolImpl::CreateAndRegisterWorkerLockRequired(
    SchedulerWorkerActionExecutor* executor) {
  DCHECK_LT(workers_.size(), max_tasks_);
  DCHECK_LT(workers_.size(), kMaxNumberOfWorkers);
  DCHECK(idle_workers_stack_.IsEmpty());

  // SchedulerWorker needs |lock_| as a predecessor for its thread lock
  // because in WakeUpOneWorker, |lock_| is first acquired and then
  // the thread lock is acquired when WakeUp is called on the worker.
  scoped_refptr<SchedulerWorker> worker = MakeRefCounted<SchedulerWorker>(
      priority_hint_,
      std::make_unique<SchedulerWorkerDelegateImpl>(
          tracked_ref_factory_.GetTrackedRef()),
      task_tracker_, &lock_, after_start().backward_compatibility);

  workers_.push_back(worker);
  executor->ScheduleStart(worker);
  DCHECK_LE(workers_.size(), max_tasks_);

  if (!cleanup_timestamps_.empty()) {
    detach_duration_histogram_->AddTime(TimeTicks::Now() -
                                        cleanup_timestamps_.top());
    cleanup_timestamps_.pop();
  }

  return worker;
}

size_t SchedulerWorkerPoolImpl::GetNumAwakeWorkersLockRequired() const {
  DCHECK_GE(workers_.size(), idle_workers_stack_.Size());
  size_t num_awake_workers = workers_.size() - idle_workers_stack_.Size();
  DCHECK_GE(num_awake_workers, num_running_tasks_);
  return num_awake_workers;
}

size_t SchedulerWorkerPoolImpl::GetDesiredNumAwakeWorkersLockRequired() const {
  const size_t num_running_or_queued_best_effort_sequences =
      num_running_best_effort_tasks_ +
      priority_queue_.GetNumSequencesWithPriority(TaskPriority::BEST_EFFORT);
  const size_t num_running_or_queued_foreground_sequences =
      num_running_tasks_ + priority_queue_.Size() -
      num_running_or_queued_best_effort_sequences;

  const size_t workers_for_best_effort_sequences = std::min(
      num_running_or_queued_best_effort_sequences, max_best_effort_tasks_);
  const size_t workers_for_foreground_sequences =
      num_running_or_queued_foreground_sequences;

  return std::min(
      {workers_for_best_effort_sequences + workers_for_foreground_sequences,
       max_tasks_, kMaxNumberOfWorkers});
}

void SchedulerWorkerPoolImpl::EnsureEnoughWorkersLockRequired(
    SchedulerWorkerActionExecutor* executor) {
  // Don't do anything if the pool isn't started.
  if (max_tasks_ == 0)
    return;

  const size_t desired_num_awake_workers =
      GetDesiredNumAwakeWorkersLockRequired();
  const size_t num_awake_workers = GetNumAwakeWorkersLockRequired();

  size_t num_workers_to_wake_up =
      ClampSub(desired_num_awake_workers, num_awake_workers);
  num_workers_to_wake_up = std::min(num_workers_to_wake_up, size_t(2U));

  // Wake up the appropriate number of workers.
  for (size_t i = 0; i < num_workers_to_wake_up; ++i) {
    MaintainAtLeastOneIdleWorkerLockRequired(executor);
    SchedulerWorker* worker_to_wakeup = idle_workers_stack_.Pop();
    DCHECK(worker_to_wakeup);
    executor->ScheduleWakeUp(worker_to_wakeup);
  }

  // In the case where the loop above didn't wake up any worker and we don't
  // have excess workers, the idle worker should be maintained. This happens
  // when called from the last worker awake, or a recent increase in |max_tasks|
  // now makes it possible to keep an idle worker.
  if (desired_num_awake_workers == num_awake_workers)
    MaintainAtLeastOneIdleWorkerLockRequired(executor);
}

void SchedulerWorkerPoolImpl::AdjustMaxTasks() {
  DCHECK(
      after_start().service_thread_task_runner->RunsTasksInCurrentSequence());

  SchedulerWorkerActionExecutor executor(this);
  AutoSchedulerLock auto_lock(lock_);

  // Increment max tasks for each worker that has been within a MAY_BLOCK
  // ScopedBlockingCall for more than may_block_threshold.
  for (scoped_refptr<SchedulerWorker> worker : workers_) {
    // The delegates of workers inside a SchedulerWorkerPoolImpl should be
    // SchedulerWorkerDelegateImpls.
    SchedulerWorkerDelegateImpl* delegate =
        static_cast<SchedulerWorkerDelegateImpl*>(worker->delegate());
    AnnotateAcquiredLockAlias annotate(lock_, delegate->lock());
    if (delegate->MustIncrementMaxTasksLockRequired()) {
      IncrementMaxTasksLockRequired(
          delegate->is_running_best_effort_task_lock_required());
    }
  }

  // Wake up workers according to the updated |max_tasks_|.
  EnsureEnoughWorkersLockRequired(&executor);
}

void SchedulerWorkerPoolImpl::ScheduleAdjustMaxTasks() {
  // |polling_max_tasks_| can't change before the task posted below runs. Skip
  // check on NaCl to avoid unsafe reference acquisition warning.
#if !defined(OS_NACL)
  DCHECK(TS_UNCHECKED_READ(polling_max_tasks_));
#endif

  after_start().service_thread_task_runner->PostDelayedTask(
      FROM_HERE,
      BindOnce(&SchedulerWorkerPoolImpl::AdjustMaxTasksFunction,
               Unretained(this)),
      after_start().blocked_workers_poll_period);
}

bool SchedulerWorkerPoolImpl::MustScheduleAdjustMaxTasksLockRequired() {
  if (polling_max_tasks_ || !ShouldPeriodicallyAdjustMaxTasksLockRequired())
    return false;
  polling_max_tasks_ = true;
  return true;
}

void SchedulerWorkerPoolImpl::AdjustMaxTasksFunction() {
  DCHECK(
      after_start().service_thread_task_runner->RunsTasksInCurrentSequence());

  AdjustMaxTasks();
  {
    AutoSchedulerLock auto_lock(lock_);
    DCHECK(polling_max_tasks_);

    if (!ShouldPeriodicallyAdjustMaxTasksLockRequired()) {
      polling_max_tasks_ = false;
      return;
    }
  }
  ScheduleAdjustMaxTasks();
}

bool SchedulerWorkerPoolImpl::ShouldPeriodicallyAdjustMaxTasksLockRequired() {
  // AdjustMaxTasks() should be scheduled to periodically adjust |max_tasks_|
  // and |max_best_effort_tasks_| when (1) the concurrency limits are not large
  // enough to accommodate all queued and running sequences and an idle worker
  // and (2) there are unresolved MAY_BLOCK ScopedBlockingCalls.
  // - When (1) is false: No worker would be created or woken up if the
  //   concurrency limits were increased, so there is no hurry to increase them.
  // - When (2) is false: The concurrency limits could not be increased by
  //   AdjustMaxTasks().

  const size_t num_running_or_queued_best_effort_sequences =
      num_running_best_effort_tasks_ +
      priority_queue_.GetNumSequencesWithPriority(TaskPriority::BEST_EFFORT);
  if (num_running_or_queued_best_effort_sequences > max_best_effort_tasks_ &&
      num_unresolved_best_effort_may_block_ > 0) {
    return true;
  }

  const size_t num_running_or_queued_sequences =
      num_running_tasks_ + priority_queue_.Size();
  constexpr size_t kIdleWorker = 1;
  return num_running_or_queued_sequences + kIdleWorker > max_tasks_ &&
         num_unresolved_may_block_ > 0;
}

void SchedulerWorkerPoolImpl::DecrementMaxTasksLockRequired(
    bool is_running_best_effort_task) {
  --max_tasks_;
  if (is_running_best_effort_task)
    --max_best_effort_tasks_;
}

void SchedulerWorkerPoolImpl::IncrementMaxTasksLockRequired(
    bool is_running_best_effort_task) {
  ++max_tasks_;
  if (is_running_best_effort_task)
    ++max_best_effort_tasks_;
}

SchedulerWorkerPoolImpl::InitializedInStart::InitializedInStart() = default;
SchedulerWorkerPoolImpl::InitializedInStart::~InitializedInStart() = default;

}  // namespace internal
}  // namespace base
