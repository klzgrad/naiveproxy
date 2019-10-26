// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_
#define BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_

#include <stddef.h>

#include <atomic>
#include <limits>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/task/post_job.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool/sequence_sort_key.h"
#include "base/task/thread_pool/task.h"
#include "base/task/thread_pool/task_source.h"

namespace base {
namespace internal {

class PooledTaskRunnerDelegate;

// A JobTaskSource generates many Tasks from a single RepeatingClosure.
//
// Derived classes control the intended concurrency with GetMaxConcurrency().
class BASE_EXPORT JobTaskSource : public TaskSource {
 public:
  JobTaskSource(const Location& from_here,
                const TaskTraits& traits,
                RepeatingCallback<void(experimental::JobDelegate*)> worker_task,
                RepeatingCallback<size_t()> max_concurrency_callback,
                PooledTaskRunnerDelegate* delegate);

  // Notifies this task source that max concurrency was increased, and the
  // number of worker should be adjusted.
  void NotifyConcurrencyIncrease();

  // TaskSource:
  ExecutionEnvironment GetExecutionEnvironment() override;
  size_t GetRemainingConcurrency() const override;

  // Returns the maximum number of tasks from this TaskSource that can run
  // concurrently.
  size_t GetMaxConcurrency() const;

#if DCHECK_IS_ON()
  size_t GetConcurrencyIncreaseVersion() const;
  // Returns true if the concurrency version was updated above
  // |recorded_version|, or false on timeout.
  bool WaitForConcurrencyIncreaseUpdate(size_t recorded_version);
#endif  // DCHECK_IS_ON()

 private:
  static constexpr size_t kInvalidWorkerCount =
      std::numeric_limits<size_t>::max();

  ~JobTaskSource() override;

  // TaskSource:
  RunStatus WillRunTask() override;
  Optional<Task> TakeTask(TaskSource::Transaction* transaction) override;
  Optional<Task> Clear(TaskSource::Transaction* transaction) override;
  bool DidProcessTask(TaskSource::Transaction* transaction) override;
  SequenceSortKey GetSortKey() const override;

  // The current number of workers concurrently running tasks from this
  // TaskSource.
  std::atomic_size_t worker_count_{0U};

  const Location from_here_;
  base::RepeatingCallback<size_t()> max_concurrency_callback_;
  base::RepeatingClosure worker_task_;
  const TimeTicks queue_time_;
  PooledTaskRunnerDelegate* delegate_;

#if DCHECK_IS_ON()
  // Synchronizes accesses to |increase_version_|.
  mutable Lock version_lock_;
  // Signaled whenever increase_version_ is updated.
  ConditionVariable version_condition_{&version_lock_};
  // Incremented every time max concurrency is increased.
  size_t increase_version_ GUARDED_BY(version_lock_) = 0;
#endif  // DCHECK_IS_ON()

  DISALLOW_COPY_AND_ASSIGN(JobTaskSource);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_
