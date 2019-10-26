// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/job_task_source.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/task/thread_pool/pooled_task_runner_delegate.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/time/time_override.h"

namespace base {
namespace internal {

JobTaskSource::JobTaskSource(
    const Location& from_here,
    const TaskTraits& traits,
    RepeatingCallback<void(experimental::JobDelegate*)> worker_task,
    RepeatingCallback<size_t()> max_concurrency_callback,
    PooledTaskRunnerDelegate* delegate)
    : TaskSource(traits, nullptr, TaskSourceExecutionMode::kJob),
      from_here_(from_here),
      max_concurrency_callback_(std::move(max_concurrency_callback)),
      worker_task_(base::BindRepeating(
          [](JobTaskSource* self,
             const RepeatingCallback<void(experimental::JobDelegate*)>&
                 worker_task) {
            // Each worker task has its own delegate with associated state.
            experimental::JobDelegate job_delegate{self, self->delegate_};
            worker_task.Run(&job_delegate);
          },
          base::Unretained(this),
          std::move(worker_task))),
      queue_time_(TimeTicks::Now()),
      delegate_(delegate) {
  DCHECK(delegate_);
}

JobTaskSource::~JobTaskSource() {
#if DCHECK_IS_ON()
  auto worker_count = worker_count_.load(std::memory_order_relaxed);
  // Make sure there's no outstanding active run operation left.
  DCHECK(worker_count == 0U || worker_count == kInvalidWorkerCount)
      << worker_count;
#endif
}

ExecutionEnvironment JobTaskSource::GetExecutionEnvironment() {
  return {SequenceToken::Create(), nullptr};
}

TaskSource::RunStatus JobTaskSource::WillRunTask() {
  // When this call is caused by an increase of max concurrency followed by an
  // associated NotifyConcurrencyIncrease(), the priority queue lock guarantees
  // an happens-after relation with NotifyConcurrencyIncrease(). The memory
  // operations on |worker_count| below and in DidProcessTask() use
  // std::memory_order_release and std::memory_order_acquire respectively to
  // establish a Release-Acquire ordering. This ensures that all memory
  // side-effects made before this point, including an increase of max
  // concurrency followed by NotifyConcurrencyIncrease() are visible to a
  // DidProcessTask() call which is ordered after this one.
  const size_t max_concurrency = GetMaxConcurrency();
  size_t worker_count_before_add =
      worker_count_.load(std::memory_order_relaxed);

  // std::memory_order_release on success to make the newest |max_concurrency|
  // visible to a thread that calls DidProcessTask() containing a matching
  // std::memory_order_acquire.
  while (worker_count_before_add < max_concurrency &&
         !worker_count_.compare_exchange_weak(
             worker_count_before_add, worker_count_before_add + 1,
             std::memory_order_release, std::memory_order_relaxed)) {
  }
  // Don't allow this worker to run the task if either:
  //   A) |worker_count_| is already at |max_concurrency|.
  //   B) |max_concurrency| was lowered below or to |worker_count_|.
  //   C) |worker_count_| was invalidated.
  if (worker_count_before_add >= max_concurrency) {
    // The caller is prevented from running a task from this TaskSource.
    return RunStatus::kDisallowed;
  }

  DCHECK_LT(worker_count_before_add, max_concurrency);
  return max_concurrency == worker_count_before_add + 1
             ? RunStatus::kAllowedSaturated
             : RunStatus::kAllowedNotSaturated;
}

size_t JobTaskSource::GetRemainingConcurrency() const {
  // std::memory_order_relaxed is sufficient because no other state is
  // synchronized with GetRemainingConcurrency().
  const size_t worker_count = worker_count_.load(std::memory_order_relaxed);
  const size_t max_concurrency = GetMaxConcurrency();
  // Avoid underflows.
  if (worker_count > max_concurrency)
    return 0;
  return max_concurrency - worker_count;
}

void JobTaskSource::NotifyConcurrencyIncrease() {
#if DCHECK_IS_ON()
  {
    AutoLock auto_lock(version_lock_);
    ++increase_version_;
    version_condition_.Broadcast();
  }
#endif  // DCHECK_IS_ON()
  // Make sure the task source is in the queue if not already.
  // Caveat: it's possible but unlikely that the task source has already reached
  // its intended concurrency and doesn't need to be enqueued if there
  // previously were too many worker. For simplicity, the task source is always
  // enqueued and will get discarded if already saturated when it is popped from
  // the priority queue.
  delegate_->EnqueueJobTaskSource(this);
}

size_t JobTaskSource::GetMaxConcurrency() const {
  return max_concurrency_callback_.Run();
}

#if DCHECK_IS_ON()

size_t JobTaskSource::GetConcurrencyIncreaseVersion() const {
  AutoLock auto_lock(version_lock_);
  return increase_version_;
}

bool JobTaskSource::WaitForConcurrencyIncreaseUpdate(size_t recorded_version) {
  AutoLock auto_lock(version_lock_);
  constexpr TimeDelta timeout = TimeDelta::FromSeconds(1);
  const base::TimeTicks start_time = subtle::TimeTicksNowIgnoringOverride();
  do {
    DCHECK_LE(recorded_version, increase_version_);
    if (recorded_version != increase_version_)
      return true;
    // Waiting is acceptable because it is in DCHECK-only code.
    ScopedAllowBaseSyncPrimitivesOutsideBlockingScope
        allow_base_sync_primitives;
    version_condition_.TimedWait(timeout);
  } while (subtle::TimeTicksNowIgnoringOverride() - start_time < timeout);
  return false;
}

#endif  // DCHECK_IS_ON()

Optional<Task> JobTaskSource::TakeTask(TaskSource::Transaction* transaction) {
  DCHECK_GT(worker_count_.load(std::memory_order_relaxed), 0U);
  DCHECK(worker_task_);
  return base::make_optional<Task>(from_here_, worker_task_, TimeDelta());
}

bool JobTaskSource::DidProcessTask(TaskSource::Transaction* transaction) {
  size_t worker_count_before_sub =
      worker_count_.load(std::memory_order_relaxed);

  // std::memory_order_acquire on |worker_count_| is necessary to establish
  // Release-Acquire ordering (see WillRunTask()).
  // When the task source needs to be queued, either because the current task
  // yielded or because of NotifyConcurrencyIncrease(), one of the following is
  // true:
  //   A) The JobTaskSource is already in the queue (no worker picked up the
  //      extra work yet): Incorrectly returning false is fine and the memory
  //      barrier may be ineffective.
  //   B) The JobTaskSource() is no longer in the queue: The Release-Acquire
  //      ordering with WillRunTask() established by |worker_count| ensures that
  //      the upcoming call for GetMaxConcurrency() happens-after any
  //      NotifyConcurrencyIncrease() that happened-before WillRunTask(). If
  //      this task completed because it yielded, this barrier guarantees that
  //      it sees an up-to-date concurrency value and correctly re-enqueues.
  //
  // Note that stale values the other way around (incorrectly re-enqueuing) are
  // not an issue because the queues support empty task sources.
  while (worker_count_before_sub != kInvalidWorkerCount &&
         !worker_count_.compare_exchange_weak(
             worker_count_before_sub, worker_count_before_sub - 1,
             std::memory_order_acquire, std::memory_order_relaxed)) {
  }
  if (worker_count_before_sub == kInvalidWorkerCount)
    return false;

  DCHECK_GT(worker_count_before_sub, 0U);

  // Re-enqueue the TaskSource if the task ran and the worker count is below the
  // max concurrency.
  return worker_count_before_sub <= GetMaxConcurrency();
}

SequenceSortKey JobTaskSource::GetSortKey() const {
  return SequenceSortKey(traits_.priority(), queue_time_);
}

Optional<Task> JobTaskSource::Clear(TaskSource::Transaction* transaction) {
  // Invalidate |worker_count_| so that further calls to WillRunTask() never
  // succeed. std::memory_order_relaxed is sufficient because this task source
  // never needs to be re-enqueued after Clear().
  size_t worker_count_before_store =
      worker_count_.exchange(kInvalidWorkerCount, std::memory_order_relaxed);
  DCHECK_GT(worker_count_before_store, 0U);
  // Nothing is cleared since other workers might still racily run tasks. For
  // simplicity, the destructor will take care of it once all references are
  // released.
  return base::make_optional<Task>(from_here_, DoNothing(), TimeDelta());
}

}  // namespace internal
}  // namespace base
