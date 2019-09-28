// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/job_task_source.h"

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/task/thread_pool/thread_pool_clock.h"
#include "base/time/time.h"

namespace base {
namespace internal {

JobTaskSource::JobTaskSource(const Location& from_here,
                             base::RepeatingClosure worker_task,
                             const TaskTraits& traits)
    : TaskSource(traits, nullptr, TaskSourceExecutionMode::kJob),
      from_here_(from_here),
      worker_task_(std::move(worker_task)),
      queue_time_(ThreadPoolClock::Now()) {}

JobTaskSource::~JobTaskSource() = default;

ExecutionEnvironment JobTaskSource::GetExecutionEnvironment() {
  return {SequenceToken::Create(), nullptr};
}

TaskSource::RunIntent JobTaskSource::WillRunTask() {
  const size_t max_concurrency = GetMaxConcurrency();
  const size_t worker_count_initial =
      worker_count_.load(std::memory_order_relaxed);
  // Don't allow this worker to run the task if either:
  //   A) |worker_count_| is already at |max_concurrency|.
  //   B) |max_concurrency| was lowered below or to |worker_count_|.
  if (worker_count_initial >= max_concurrency) {
    // The caller receives an invalid RunIntent and should skip this TaskSource.
    return RunIntent();
  }
  const size_t worker_count_before_add =
      worker_count_.fetch_add(1, std::memory_order_relaxed);
  // WillRunTask() has external synchronization to prevent concurrent calls and
  // it is the only place where |worker_count_| is incremented. Therefore, the
  // second reading of |worker_count_| from WillRunTask() cannot be greater than
  // the first reading. However, since DidProcessTask() can decrement
  // |worker_count_| concurrently with WillRunTask(), the second reading can be
  // lower than the first reading.
  DCHECK_LE(worker_count_before_add, worker_count_initial);
  DCHECK_LT(worker_count_before_add, max_concurrency);
  return MakeRunIntent(max_concurrency == worker_count_before_add + 1
                           ? Saturated::kYes
                           : Saturated::kNo);
}

size_t JobTaskSource::GetRemainingConcurrency() const {
  return GetMaxConcurrency() - worker_count_.load(std::memory_order_relaxed);
}

Optional<Task> JobTaskSource::TakeTask() {
  DCHECK_GT(worker_count_.load(std::memory_order_relaxed), 0U);
  DCHECK(worker_task_);
  return base::make_optional<Task>(from_here_, worker_task_, TimeDelta());
}

bool JobTaskSource::DidProcessTask(RunResult run_result) {
  size_t worker_count_before_sub =
      worker_count_.fetch_sub(1, std::memory_order_relaxed);
  DCHECK_GT(worker_count_before_sub, 0U);
  // Re-enqueue the TaskSource if the task ran and the worker count is below the
  // max concurrency.
  const bool must_be_queued =
      run_result == RunResult::kSkippedAtShutdown
          ? false
          : worker_count_before_sub <= GetMaxConcurrency();
  return must_be_queued;
}

SequenceSortKey JobTaskSource::GetSortKey() const {
  return SequenceSortKey(traits_.priority(), queue_time_);
}

void JobTaskSource::Clear() {
  worker_task_.Reset();
}

}  // namespace internal
}  // namespace base
