// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_
#define BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_

#include <stddef.h>

#include <atomic>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool/sequence_sort_key.h"
#include "base/task/thread_pool/task.h"
#include "base/task/thread_pool/task_source.h"

namespace base {
namespace internal {

// A JobTaskSource generates many Tasks from a single RepeatingClosure.
//
// Derived classes control the intended concurrency with GetMaxConcurrency().
// Increase in concurrency is not supported and should never happen.
// TODO(etiennep): Support concurrency increase.
class BASE_EXPORT JobTaskSource : public TaskSource {
 public:
  JobTaskSource(const Location& from_here,
                base::RepeatingClosure task,
                const TaskTraits& traits);

  // TaskSource:
  RunIntent WillRunTask() override;
  ExecutionEnvironment GetExecutionEnvironment() override;
  size_t GetRemainingConcurrency() const override;

 protected:
  ~JobTaskSource() override;

  // Returns the maximum number of tasks from this TaskSource that can run
  // concurrently. The implementation can only return values lower than or equal
  // to previously returned values.
  virtual size_t GetMaxConcurrency() const = 0;

 private:
  // TaskSource:
  Optional<Task> TakeTask() override;
  bool DidProcessTask(RunResult run_result) override;
  SequenceSortKey GetSortKey() const override;
  void Clear() override;

  // The current number of workers concurrently running tasks from this
  // TaskSource. "memory_order_relaxed" is sufficient to access this variable as
  // no other state is synchronized with it.
  std::atomic_size_t worker_count_{0U};

  const Location from_here_;
  base::RepeatingClosure worker_task_;
  const TimeTicks queue_time_;

  DISALLOW_COPY_AND_ASSIGN(JobTaskSource);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_THREAD_POOL_JOB_TASK_SOURCE_H_
