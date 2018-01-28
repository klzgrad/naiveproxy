// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_worker_pool_impl.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram.h"
#include "base/sequence_token.h"
#include "base/strings/stringprintf.h"
#include "base/task_scheduler/scheduler_worker_pool_params.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/task_scheduler/task_traits.h"
#include "base/threading/platform_thread.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_restrictions.h"

namespace base {
namespace internal {

constexpr TimeDelta SchedulerWorkerPoolImpl::kBlockedWorkersPollPeriod;

namespace {

constexpr char kPoolNameSuffix[] = "Pool";
constexpr char kDetachDurationHistogramPrefix[] =
    "TaskScheduler.DetachDuration.";
constexpr char kNumTasksBeforeDetachHistogramPrefix[] =
    "TaskScheduler.NumTasksBeforeDetach.";
constexpr char kNumTasksBetweenWaitsHistogramPrefix[] =
    "TaskScheduler.NumTasksBetweenWaits.";
constexpr size_t kMaxNumberOfWorkers = 256;

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

class SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl
    : public SchedulerWorker::Delegate,
      public BlockingObserver {
 public:
  // |outer| owns the worker for which this delegate is constructed.
  SchedulerWorkerDelegateImpl(SchedulerWorkerPoolImpl* outer);
  ~SchedulerWorkerDelegateImpl() override;

  // SchedulerWorker::Delegate:
  void OnCanScheduleSequence(scoped_refptr<Sequence> sequence) override;
  void OnMainEntry(SchedulerWorker* worker) override;
  scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) override;
  void DidRunTask() override;
  void ReEnqueueSequence(scoped_refptr<Sequence> sequence) override;
  TimeDelta GetSleepTimeout() override;
  void OnMainExit(SchedulerWorker* worker) override;

  // Sets |is_on_idle_workers_stack_| to be true and DCHECKS that |worker|
  // is indeed on the idle workers stack.
  void SetIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker);

  // Sets |is_on_idle_workers_stack_| to be false and DCHECKS that |worker|
  // isn't on the idle workers stack.
  void UnSetIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker);

// DCHECKs that |worker| is on the idle workers stack and
// |is_on_idle_workers_stack_| is true.
#if DCHECK_IS_ON()
  void AssertIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker) const;
#else
  void AssertIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker) const {}
#endif

  // BlockingObserver:
  void BlockingStarted(BlockingType blocking_type) override;
  void BlockingTypeUpgraded() override;
  void BlockingEnded() override;

  void MayBlockEntered();
  void WillBlockEntered();

  // Returns true iff this worker has been within a MAY_BLOCK ScopedBlockingCall
  // for more than |outer_->MayBlockThreshold()|. The worker capacity must be
  // incremented if this returns true.
  bool MustIncrementWorkerCapacityLockRequired();

 private:
  // Returns true if |worker| is allowed to cleanup and remove itself from the
  // pool. Called from GetWork() when no work is available.
  bool CanCleanup(SchedulerWorker* worker);

  // Calls cleanup on |worker| and removes it from the pool.
  void CleanupLockRequired(SchedulerWorker* worker);

  // Called in GetWork() when a worker becomes idle.
  void OnWorkerBecomesIdleLockRequired(SchedulerWorker* worker);

  SchedulerWorkerPoolImpl* outer_;

  // Time of the last detach.
  TimeTicks last_detach_time_;

  // Number of tasks executed since the last time the
  // TaskScheduler.NumTasksBetweenWaits histogram was recorded.
  size_t num_tasks_since_last_wait_ = 0;

  // Number of tasks executed since the last time the
  // TaskScheduler.NumTasksBeforeDetach histogram was recorded.
  size_t num_tasks_since_last_detach_ = 0;

  // Whether the worker holding this delegate is on the idle worker's stack.
  // Access synchronized by |outer_->lock_|.
  bool is_on_idle_workers_stack_ = true;

  // Whether |outer_->worker_capacity_| was incremented due to a
  // ScopedBlockingCall on the thread. Access synchronized by |outer_->lock_|.
  bool incremented_worker_capacity_since_blocked_ = false;

  // Time when MayBlockScopeEntered() was last called. Reset when
  // BlockingScopeExited() is called. Access synchronized by |outer_->lock_|.
  TimeTicks may_block_start_time_;

  // Whether this worker is currently running a task (i.e. GetWork() has
  // returned a non-empty sequence and DidRunTask() hasn't been called yet).
  bool is_running_task_ = false;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerDelegateImpl);
};

SchedulerWorkerPoolImpl::SchedulerWorkerPoolImpl(
    const std::string& name,
    ThreadPriority priority_hint,
    TaskTracker* task_tracker,
    DelayedTaskManager* delayed_task_manager)
    : SchedulerWorkerPool(task_tracker, delayed_task_manager),
      name_(name),
      priority_hint_(priority_hint),
      lock_(shared_priority_queue_.container_lock()),
      idle_workers_stack_cv_for_testing_(lock_.CreateConditionVariable()),
      join_for_testing_returned_(WaitableEvent::ResetPolicy::MANUAL,
                                 WaitableEvent::InitialState::NOT_SIGNALED),
      // Mimics the UMA_HISTOGRAM_LONG_TIMES macro.
      detach_duration_histogram_(Histogram::FactoryTimeGet(
          kDetachDurationHistogramPrefix + name_ + kPoolNameSuffix,
          TimeDelta::FromMilliseconds(1),
          TimeDelta::FromHours(1),
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      // Mimics the UMA_HISTOGRAM_COUNTS_1000 macro. When a worker runs more
      // than 1000 tasks before detaching, there is no need to know the exact
      // number of tasks that ran.
      num_tasks_before_detach_histogram_(Histogram::FactoryGet(
          kNumTasksBeforeDetachHistogramPrefix + name_ + kPoolNameSuffix,
          1,
          1000,
          50,
          HistogramBase::kUmaTargetedHistogramFlag)),
      // Mimics the UMA_HISTOGRAM_COUNTS_100 macro. A SchedulerWorker is
      // expected to run between zero and a few tens of tasks between waits.
      // When it runs more than 100 tasks, there is no need to know the exact
      // number of tasks that ran.
      num_tasks_between_waits_histogram_(Histogram::FactoryGet(
          kNumTasksBetweenWaitsHistogramPrefix + name_ + kPoolNameSuffix,
          1,
          100,
          50,
          HistogramBase::kUmaTargetedHistogramFlag)) {}

void SchedulerWorkerPoolImpl::Start(
    const SchedulerWorkerPoolParams& params,
    scoped_refptr<TaskRunner> service_thread_task_runner) {
  AutoSchedulerLock auto_lock(lock_);

  DCHECK(workers_.empty());

  worker_capacity_ = params.max_threads();
  initial_worker_capacity_ = worker_capacity_;
  suggested_reclaim_time_ = params.suggested_reclaim_time();
  backward_compatibility_ = params.backward_compatibility();

  service_thread_task_runner_ = std::move(service_thread_task_runner);

  // The initial number of workers is |num_wake_ups_before_start_| + 1 to try to
  // keep one at least one standby thread at all times (capacity permitting).
  const int num_initial_workers = std::min(num_wake_ups_before_start_ + 1,
                                           static_cast<int>(worker_capacity_));
  workers_.reserve(num_initial_workers);

  for (int index = 0; index < num_initial_workers; ++index) {
    SchedulerWorker* worker =
        CreateRegisterAndStartSchedulerWorkerLockRequired();

    // CHECK that the first worker can be started (assume that failure means
    // that threads can't be created on this machine).
    CHECK(worker || index > 0);

    if (worker) {
      SchedulerWorkerDelegateImpl* delegate =
          static_cast<SchedulerWorkerDelegateImpl*>(worker->delegate());
      if (index < num_wake_ups_before_start_) {
        delegate->UnSetIsOnIdleWorkersStackLockRequired(worker);
        worker->WakeUp();
      } else {
        idle_workers_stack_.Push(worker);
        delegate->AssertIsOnIdleWorkersStackLockRequired(worker);
      }
    }
  }
}

SchedulerWorkerPoolImpl::~SchedulerWorkerPoolImpl() {
  // SchedulerWorkerPool should never be deleted in production unless its
  // initialization failed.
#if DCHECK_IS_ON()
  AutoSchedulerLock auto_lock(lock_);
  DCHECK(join_for_testing_returned_.IsSignaled() || workers_.empty());
#endif
}

void SchedulerWorkerPoolImpl::OnCanScheduleSequence(
    scoped_refptr<Sequence> sequence) {
  const auto sequence_sort_key = sequence->GetSortKey();
  shared_priority_queue_.BeginTransaction()->Push(std::move(sequence),
                                                  sequence_sort_key);

  WakeUpOneWorker();
}

void SchedulerWorkerPoolImpl::GetHistograms(
    std::vector<const HistogramBase*>* histograms) const {
  histograms->push_back(detach_duration_histogram_);
  histograms->push_back(num_tasks_between_waits_histogram_);
}

int SchedulerWorkerPoolImpl::GetMaxConcurrentNonBlockedTasksDeprecated() const {
#if DCHECK_IS_ON()
  AutoSchedulerLock auto_lock(lock_);
  DCHECK_NE(initial_worker_capacity_, 0U)
      << "GetMaxConcurrentTasksDeprecated() should only be called after the "
      << "worker pool has started.";
#endif
  return initial_worker_capacity_;
}

void SchedulerWorkerPoolImpl::WaitForWorkersIdleForTesting(size_t n) {
  AutoSchedulerLock auto_lock(lock_);
  WaitForWorkersIdleLockRequiredForTesting(n);
}

void SchedulerWorkerPoolImpl::WaitForAllWorkersIdleForTesting() {
  AutoSchedulerLock auto_lock(lock_);
  WaitForWorkersIdleLockRequiredForTesting(workers_.size());
}

void SchedulerWorkerPoolImpl::JoinForTesting() {
#if DCHECK_IS_ON()
  join_for_testing_started_.Set();
#endif
  DCHECK(!CanWorkerCleanupForTesting() || suggested_reclaim_time_.is_max())
      << "Workers can cleanup during join.";

  decltype(workers_) workers_copy;
  {
    AutoSchedulerLock auto_lock(lock_);

    // Make a copy of the SchedulerWorkers so that we can call
    // SchedulerWorker::JoinForTesting() without holding |lock_| since
    // SchedulerWorkers may need to access |workers_|.
    workers_copy = workers_;
  }
  for (const auto& worker : workers_copy)
    worker->JoinForTesting();

#if DCHECK_IS_ON()
  AutoSchedulerLock auto_lock(lock_);
  DCHECK(workers_ == workers_copy);
#endif

  DCHECK(!join_for_testing_returned_.IsSignaled());
  join_for_testing_returned_.Signal();
}

void SchedulerWorkerPoolImpl::DisallowWorkerCleanupForTesting() {
  worker_cleanup_disallowed_.Set();
}

size_t SchedulerWorkerPoolImpl::NumberOfWorkersForTesting() {
  AutoSchedulerLock auto_lock(lock_);
  return workers_.size();
}

size_t SchedulerWorkerPoolImpl::GetWorkerCapacityForTesting() {
  AutoSchedulerLock auto_lock(lock_);
  return worker_capacity_;
}

size_t SchedulerWorkerPoolImpl::NumberOfIdleWorkersForTesting() {
  AutoSchedulerLock auto_lock(lock_);
  return idle_workers_stack_.Size();
}

void SchedulerWorkerPoolImpl::MaximizeMayBlockThresholdForTesting() {
  maximum_blocked_threshold_for_testing_.Set();
}

SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    SchedulerWorkerDelegateImpl(SchedulerWorkerPoolImpl* outer)
    : outer_(outer) {}

SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    ~SchedulerWorkerDelegateImpl() = default;

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    OnCanScheduleSequence(scoped_refptr<Sequence> sequence) {
  outer_->OnCanScheduleSequence(std::move(sequence));
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::OnMainEntry(
    SchedulerWorker* worker) {
  {
#if DCHECK_IS_ON()
    AutoSchedulerLock auto_lock(outer_->lock_);
    DCHECK(ContainsWorker(outer_->workers_, worker));
#endif
  }

  DCHECK_EQ(num_tasks_since_last_wait_, 0U);

  PlatformThread::SetName(
      StringPrintf("TaskScheduler%sWorker", outer_->name_.c_str()));

  outer_->BindToCurrentThread();
  SetBlockingObserverForCurrentThread(this);
}

scoped_refptr<Sequence>
SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::GetWork(
    SchedulerWorker* worker) {
  DCHECK(!is_running_task_);
  {
    AutoSchedulerLock auto_lock(outer_->lock_);

    DCHECK(ContainsWorker(outer_->workers_, worker));

    // Calling GetWork() when |is_on_idle_workers_stack_| is true indicates
    // that we must've reached GetWork() because of the WaitableEvent timing
    // out. In which case, we return no work and possibly cleanup the worker.
    DCHECK_EQ(is_on_idle_workers_stack_,
              outer_->idle_workers_stack_.Contains(worker));
    if (is_on_idle_workers_stack_) {
      if (CanCleanup(worker))
        CleanupLockRequired(worker);

      // Since we got here from timing out from the WaitableEvent rather than
      // waking up and completing tasks, we expect to have completed 0 tasks
      // since waiting.
      //
      // TODO(crbug.com/756898): Do not log this histogram when waking up due to
      // timeout.
      DCHECK_EQ(num_tasks_since_last_wait_, 0U);
      outer_->num_tasks_between_waits_histogram_->Add(
          num_tasks_since_last_wait_);
      return nullptr;
    }

    // Excess workers should not get work, until they are no longer excess (i.e.
    // worker capacity increases or another worker cleans up). This ensures that
    // if we have excess workers in the pool, they get a chance to no longer be
    // excess before being cleaned up.
    if (outer_->NumberOfExcessWorkersLockRequired() >
        outer_->idle_workers_stack_.Size()) {
      OnWorkerBecomesIdleLockRequired(worker);
      return nullptr;
    }
  }
  scoped_refptr<Sequence> sequence;
  {
    std::unique_ptr<PriorityQueue::Transaction> shared_transaction(
        outer_->shared_priority_queue_.BeginTransaction());

    if (shared_transaction->IsEmpty()) {
      // |shared_transaction| is kept alive while |worker| is added to
      // |idle_workers_stack_| to avoid this race:
      // 1. This thread creates a Transaction, finds |shared_priority_queue_|
      //    empty and ends the Transaction.
      // 2. Other thread creates a Transaction, inserts a Sequence into
      //    |shared_priority_queue_| and ends the Transaction. This can't happen
      //    if the Transaction of step 1 is still active because because there
      //    can only be one active Transaction per PriorityQueue at a time.
      // 3. Other thread calls WakeUpOneWorker(). No thread is woken up because
      //    |idle_workers_stack_| is empty.
      // 4. This thread adds itself to |idle_workers_stack_| and goes to sleep.
      //    No thread runs the Sequence inserted in step 2.
      AutoSchedulerLock auto_lock(outer_->lock_);

      OnWorkerBecomesIdleLockRequired(worker);
      return nullptr;
    }
    sequence = shared_transaction->PopSequence();
  }
  DCHECK(sequence);
#if DCHECK_IS_ON()
  {
    AutoSchedulerLock auto_lock(outer_->lock_);
    DCHECK(!outer_->idle_workers_stack_.Contains(worker));
  }
#endif

  is_running_task_ = true;
  return sequence;
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::DidRunTask() {
  DCHECK(may_block_start_time_.is_null());
  DCHECK(!incremented_worker_capacity_since_blocked_);
  DCHECK(is_running_task_);
  is_running_task_ = false;

  ++num_tasks_since_last_wait_;
  ++num_tasks_since_last_detach_;
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    ReEnqueueSequence(scoped_refptr<Sequence> sequence) {
  const SequenceSortKey sequence_sort_key = sequence->GetSortKey();
  outer_->shared_priority_queue_.BeginTransaction()->Push(std::move(sequence),
                                                          sequence_sort_key);
  // The thread calling this method will soon call GetWork(). Therefore, there
  // is no need to wake up a worker to run the sequence that was just inserted
  // into |outer_->shared_priority_queue_|.
}

TimeDelta SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    GetSleepTimeout() {
  return outer_->suggested_reclaim_time_;
}

bool SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::CanCleanup(
    SchedulerWorker* worker) {
  return worker != outer_->PeekAtIdleWorkersStackLockRequired() &&
         outer_->CanWorkerCleanupForTesting();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::CleanupLockRequired(
    SchedulerWorker* worker) {
  outer_->lock_.AssertAcquired();
  outer_->num_tasks_before_detach_histogram_->Add(num_tasks_since_last_detach_);
  outer_->cleanup_timestamps_.push(TimeTicks::Now());
  worker->Cleanup();
  outer_->RemoveFromIdleWorkersStackLockRequired(worker);

  // Remove the worker from |workers_|.
  auto worker_iter =
      std::find(outer_->workers_.begin(), outer_->workers_.end(), worker);
  DCHECK(worker_iter != outer_->workers_.end());
  outer_->workers_.erase(worker_iter);
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    OnWorkerBecomesIdleLockRequired(SchedulerWorker* worker) {
  outer_->lock_.AssertAcquired();
  // Record the TaskScheduler.NumTasksBetweenWaits histogram. After GetWork()
  // returns nullptr, the SchedulerWorker will perform a wait on its
  // WaitableEvent, so we record how many tasks were ran since the last wait
  // here.
  outer_->num_tasks_between_waits_histogram_->Add(num_tasks_since_last_wait_);
  num_tasks_since_last_wait_ = 0;
  outer_->AddToIdleWorkersStackLockRequired(worker);
  SetIsOnIdleWorkersStackLockRequired(worker);
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::OnMainExit(
    SchedulerWorker* worker) {
#if DCHECK_IS_ON()
  bool shutdown_complete = outer_->task_tracker_->IsShutdownComplete();
  AutoSchedulerLock auto_lock(outer_->lock_);

  // |worker| should already have been removed from the idle workers stack and
  // |workers_| by the time the thread is about to exit. (except in the cases
  // where the pool is no longer going to be used - in which case, it's fine for
  // there to be invalid workers in the pool.
  if (!shutdown_complete && !outer_->join_for_testing_started_.IsSet()) {
    DCHECK(!outer_->idle_workers_stack_.Contains(worker));
    DCHECK(!ContainsWorker(outer_->workers_, worker));
  }
#endif
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    SetIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker) {
  outer_->lock_.AssertAcquired();
  DCHECK(!is_on_idle_workers_stack_);
  DCHECK(outer_->idle_workers_stack_.Contains(worker));
  is_on_idle_workers_stack_ = true;
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    UnSetIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker) {
  outer_->lock_.AssertAcquired();
  DCHECK(is_on_idle_workers_stack_);
  DCHECK(!outer_->idle_workers_stack_.Contains(worker));
  is_on_idle_workers_stack_ = false;
}

#if DCHECK_IS_ON()
void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    AssertIsOnIdleWorkersStackLockRequired(SchedulerWorker* worker) const {
  outer_->lock_.AssertAcquired();
  DCHECK(is_on_idle_workers_stack_);
  DCHECK(outer_->idle_workers_stack_.Contains(worker));
}
#endif

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::BlockingStarted(
    BlockingType blocking_type) {
  // Blocking calls made outside of tasks should not influence the capacity
  // count as no task is running.
  if (!is_running_task_)
    return;

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
  {
    AutoSchedulerLock auto_lock(outer_->lock_);

    // Don't do anything if a MAY_BLOCK ScopedBlockingCall instantiated in the
    // same scope already caused the worker capacity to be incremented.
    if (incremented_worker_capacity_since_blocked_)
      return;

    // Cancel the effect of a MAY_BLOCK ScopedBlockingCall instantiated in the
    // same scope.
    if (!may_block_start_time_.is_null()) {
      may_block_start_time_ = TimeTicks();
      --outer_->num_pending_may_block_workers_;
    }
  }

  WillBlockEntered();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::BlockingEnded() {
  // Ignore blocking calls made outside of tasks.
  if (!is_running_task_)
    return;

  AutoSchedulerLock auto_lock(outer_->lock_);
  if (incremented_worker_capacity_since_blocked_) {
    outer_->DecrementWorkerCapacityLockRequired();
  } else {
    DCHECK(!may_block_start_time_.is_null());
    --outer_->num_pending_may_block_workers_;
  }

  incremented_worker_capacity_since_blocked_ = false;
  may_block_start_time_ = TimeTicks();
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::MayBlockEntered() {
  AutoSchedulerLock auto_lock(outer_->lock_);

  DCHECK(!incremented_worker_capacity_since_blocked_);
  DCHECK(may_block_start_time_.is_null());
  may_block_start_time_ = TimeTicks::Now();
  ++outer_->num_pending_may_block_workers_;

  if (!outer_->polling_worker_capacity_ &&
      outer_->ShouldPeriodicallyAdjustWorkerCapacityLockRequired()) {
    outer_->PostAdjustWorkerCapacityTaskLockRequired();
  }
}

void SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::WillBlockEntered() {
  std::unique_ptr<PriorityQueue::Transaction> shared_transaction(
      outer_->shared_priority_queue_.BeginTransaction());
  AutoSchedulerLock auto_lock(outer_->lock_);

  DCHECK(!incremented_worker_capacity_since_blocked_);
  DCHECK(may_block_start_time_.is_null());
  incremented_worker_capacity_since_blocked_ = true;
  outer_->IncrementWorkerCapacityLockRequired();

  // If the number of workers was less than the old worker capacity, PostTask
  // would've handled creating extra workers during WakeUpOneWorker. Therefore,
  // we don't need to do anything here.
  if (outer_->workers_.size() < outer_->worker_capacity_ - 1)
    return;

  if (shared_transaction->IsEmpty()) {
    outer_->MaintainAtLeastOneIdleWorkerLockRequired();
  } else {
    // TODO(crbug.com/757897): We may create extra workers in this case:
    // |workers.size()| was equal to the old |worker_capacity_|, we had multiple
    // ScopedBlockingCalls in parallel and we had work on the PQ.
    outer_->WakeUpOneWorkerLockRequired();
  }
}

bool SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::
    MustIncrementWorkerCapacityLockRequired() {
  outer_->lock_.AssertAcquired();

  if (!incremented_worker_capacity_since_blocked_ &&
      !may_block_start_time_.is_null() &&
      TimeTicks::Now() - may_block_start_time_ >= outer_->MayBlockThreshold()) {
    incremented_worker_capacity_since_blocked_ = true;

    // Reset |may_block_start_time_| so that BlockingScopeExited() knows that it
    // doesn't have to decrement |outer_->num_pending_may_block_workers_|.
    may_block_start_time_ = TimeTicks();
    --outer_->num_pending_may_block_workers_;

    return true;
  }

  return false;
}

void SchedulerWorkerPoolImpl::WaitForWorkersIdleLockRequiredForTesting(
    size_t n) {
  lock_.AssertAcquired();
  while (idle_workers_stack_.Size() < n)
    idle_workers_stack_cv_for_testing_->Wait();
}

void SchedulerWorkerPoolImpl::WakeUpOneWorkerLockRequired() {
  lock_.AssertAcquired();

  if (workers_.empty()) {
    ++num_wake_ups_before_start_;
    return;
  }

  // Ensure that there is one worker that can run tasks on top of the idle
  // stack, capacity permitting.
  MaintainAtLeastOneIdleWorkerLockRequired();

  // If the worker on top of the idle stack can run tasks, wake it up.
  if (NumberOfExcessWorkersLockRequired() < idle_workers_stack_.Size()) {
    SchedulerWorker* worker = idle_workers_stack_.Pop();
    if (worker) {
      SchedulerWorkerDelegateImpl* delegate =
          static_cast<SchedulerWorkerDelegateImpl*>(worker->delegate());
      delegate->UnSetIsOnIdleWorkersStackLockRequired(worker);
      worker->WakeUp();
    }
  }

  // Ensure that there is one worker that can run tasks on top of the idle
  // stack, capacity permitting.
  MaintainAtLeastOneIdleWorkerLockRequired();

  if (!polling_worker_capacity_ &&
      ShouldPeriodicallyAdjustWorkerCapacityLockRequired()) {
    PostAdjustWorkerCapacityTaskLockRequired();
  }
}

void SchedulerWorkerPoolImpl::WakeUpOneWorker() {
  AutoSchedulerLock auto_lock(lock_);
  WakeUpOneWorkerLockRequired();
}

void SchedulerWorkerPoolImpl::MaintainAtLeastOneIdleWorkerLockRequired() {
  lock_.AssertAcquired();

  if (workers_.size() == kMaxNumberOfWorkers)
    return;
  DCHECK_LT(workers_.size(), kMaxNumberOfWorkers);

  if (idle_workers_stack_.IsEmpty() && workers_.size() < worker_capacity_) {
    SchedulerWorker* new_worker =
        CreateRegisterAndStartSchedulerWorkerLockRequired();
    if (new_worker)
      idle_workers_stack_.Push(new_worker);
  }
}

void SchedulerWorkerPoolImpl::AddToIdleWorkersStackLockRequired(
    SchedulerWorker* worker) {
  lock_.AssertAcquired();

  DCHECK(!idle_workers_stack_.Contains(worker));
  idle_workers_stack_.Push(worker);

  DCHECK_LE(idle_workers_stack_.Size(), workers_.size());

  idle_workers_stack_cv_for_testing_->Broadcast();
}

const SchedulerWorker*
SchedulerWorkerPoolImpl::PeekAtIdleWorkersStackLockRequired() const {
  lock_.AssertAcquired();
  return idle_workers_stack_.Peek();
}

void SchedulerWorkerPoolImpl::RemoveFromIdleWorkersStackLockRequired(
    SchedulerWorker* worker) {
  lock_.AssertAcquired();
  idle_workers_stack_.Remove(worker);
}

bool SchedulerWorkerPoolImpl::CanWorkerCleanupForTesting() {
  return !worker_cleanup_disallowed_.IsSet();
}

SchedulerWorker*
SchedulerWorkerPoolImpl::CreateRegisterAndStartSchedulerWorkerLockRequired() {
  lock_.AssertAcquired();

  DCHECK_LT(workers_.size(), worker_capacity_);
  DCHECK_LT(workers_.size(), kMaxNumberOfWorkers);
  // SchedulerWorker needs |lock_| as a predecessor for its thread lock
  // because in WakeUpOneWorker, |lock_| is first acquired and then
  // the thread lock is acquired when WakeUp is called on the worker.
  scoped_refptr<SchedulerWorker> worker = MakeRefCounted<SchedulerWorker>(
      priority_hint_, std::make_unique<SchedulerWorkerDelegateImpl>(this),
      task_tracker_, &lock_, backward_compatibility_);

  if (!worker->Start())
    return nullptr;

  workers_.push_back(worker);
  DCHECK_LE(workers_.size(), worker_capacity_);

  if (!cleanup_timestamps_.empty()) {
    detach_duration_histogram_->AddTime(TimeTicks::Now() -
                                        cleanup_timestamps_.top());
    cleanup_timestamps_.pop();
  }
  return worker.get();
}

size_t SchedulerWorkerPoolImpl::NumberOfExcessWorkersLockRequired() const {
  lock_.AssertAcquired();
  return std::max<int>(0, workers_.size() - worker_capacity_);
}

void SchedulerWorkerPoolImpl::AdjustWorkerCapacity() {
  std::unique_ptr<PriorityQueue::Transaction> shared_transaction(
      shared_priority_queue_.BeginTransaction());
  AutoSchedulerLock auto_lock(lock_);

  const size_t original_worker_capacity = worker_capacity_;

  // Increment worker capacity for each worker that has been within a MAY_BLOCK
  // ScopedBlockingCall for more than MayBlockThreshold().
  for (scoped_refptr<SchedulerWorker> worker : workers_) {
    // The delegates of workers inside a SchedulerWorkerPoolImpl should be
    // SchedulerWorkerDelegateImpls.
    SchedulerWorkerDelegateImpl* delegate =
        static_cast<SchedulerWorkerDelegateImpl*>(worker->delegate());
    if (delegate->MustIncrementWorkerCapacityLockRequired())
      IncrementWorkerCapacityLockRequired();
  }

  // Wake up a worker per pending sequence, capacity permitting.
  const size_t num_pending_sequences = shared_transaction->Size();
  const size_t num_wake_ups_needed = std::min(
      worker_capacity_ - original_worker_capacity, num_pending_sequences);

  for (size_t i = 0; i < num_wake_ups_needed; ++i)
    WakeUpOneWorkerLockRequired();

  MaintainAtLeastOneIdleWorkerLockRequired();
}

TimeDelta SchedulerWorkerPoolImpl::MayBlockThreshold() const {
  if (maximum_blocked_threshold_for_testing_.IsSet())
    return TimeDelta::Max();
  // This value was set unscientifically based on intuition and may be adjusted
  // in the future. This value is smaller than |kBlockedWorkersPollPeriod|
  // because we hope than when multiple workers block around the same time, a
  // single AdjustWorkerCapacity() call will perform all the necessary capacity
  // adjustments.
  return TimeDelta::FromMilliseconds(10);
}

void SchedulerWorkerPoolImpl::PostAdjustWorkerCapacityTaskLockRequired() {
  lock_.AssertAcquired();

  polling_worker_capacity_ = true;

  service_thread_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](SchedulerWorkerPoolImpl* worker_pool) {
            worker_pool->AdjustWorkerCapacity();

            AutoSchedulerLock auto_lock(worker_pool->lock_);
            DCHECK(worker_pool->polling_worker_capacity_);

            if (worker_pool
                    ->ShouldPeriodicallyAdjustWorkerCapacityLockRequired()) {
              worker_pool->PostAdjustWorkerCapacityTaskLockRequired();
            } else {
              worker_pool->polling_worker_capacity_ = false;
            }
          },
          Unretained(this)),
      kBlockedWorkersPollPeriod);
}

bool SchedulerWorkerPoolImpl::
    ShouldPeriodicallyAdjustWorkerCapacityLockRequired() {
  lock_.AssertAcquired();
  // AdjustWorkerCapacity() must be periodically called when (1) there are no
  // idle workers that can do work (2) there are workers that are within the
  // scope of a MAY_BLOCK ScopedBlockingCall but haven't cause a capacity
  // increment yet.
  //
  // - When (1) is false: A newly posted task will run on one of the idle
  //   workers that are allowed to do work. There is no hurry to increase
  //   capacity.
  // - When (2) is false: AdjustWorkerCapacity() would be a no-op.
  const int idle_workers_that_can_do_work =
      idle_workers_stack_.Size() - NumberOfExcessWorkersLockRequired();
  return idle_workers_that_can_do_work <= 0 &&
         num_pending_may_block_workers_ > 0;
}

void SchedulerWorkerPoolImpl::DecrementWorkerCapacityLockRequired() {
  lock_.AssertAcquired();
  --worker_capacity_;
}

void SchedulerWorkerPoolImpl::IncrementWorkerCapacityLockRequired() {
  lock_.AssertAcquired();
  ++worker_capacity_;
}

}  // namespace internal
}  // namespace base
