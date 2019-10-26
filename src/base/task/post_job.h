// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_POST_JOB_H_
#define BASE_TASK_POST_JOB_H_

#include "base/base_export.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"

namespace base {
namespace internal {
class JobTaskSource;
class PooledTaskRunnerDelegate;
}
namespace experimental {

// Delegate that's passed to Job's worker task, providing an entry point to
// communicate with the scheduler.
class BASE_EXPORT JobDelegate {
 public:
  // A JobDelegate is instantiated for each worker task that is run.
  // |task_source| is the task source whose worker task is running with this
  // delegate and |pooled_task_runner_delegate| provides communication with the
  // thread pool.
  JobDelegate(internal::JobTaskSource* task_source,
              internal::PooledTaskRunnerDelegate* pooled_task_runner_delegate);
  ~JobDelegate();

  // Returns true if this thread should return from the worker task on the
  // current thread ASAP. Workers should periodically invoke ShouldYield (or
  // YieldIfNeeded()) as often as is reasonable.
  bool ShouldYield();

  // If ShouldYield(), this will pause the current thread (allowing it to be
  // replaced in the pool); no-ops otherwise. If it pauses, it will resume and
  // return from this call whenever higher priority work completes.
  // Prefer ShouldYield() over this (only use YieldIfNeeded() when unwinding
  // the stack is not possible).
  void YieldIfNeeded();

  // Notifies the scheduler that max concurrency was increased, and the number
  // of worker should be adjusted.
  void NotifyConcurrencyIncrease();

 private:
  // Verifies that either max concurrency is lower or equal to
  // |expected_max_concurrency|, or there is an increase version update
  // triggered by NotifyConcurrencyIncrease().
  void AssertExpectedConcurrency(size_t expected_max_concurrency);

  internal::JobTaskSource* const task_source_;
  internal::PooledTaskRunnerDelegate* const pooled_task_runner_delegate_;

#if DCHECK_IS_ON()
  // Used in AssertExpectedConcurrency(), see that method's impl for details.
  // Value of max concurrency recorded before running the worker task.
  size_t recorded_max_concurrency_;
  // Value of the increase version recorded before running the worker task.
  size_t recorded_increase_version_;
  // Value returned by the last call to ShouldYield().
  bool last_should_yield_ = false;
#endif

  DISALLOW_COPY_AND_ASSIGN(JobDelegate);
};

}  // namespace experimental
}  // namespace base

#endif  // BASE_TASK_POST_JOB_H_