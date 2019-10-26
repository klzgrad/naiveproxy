// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/post_job.h"

#include "base/task/thread_pool/job_task_source.h"
#include "base/task/thread_pool/pooled_task_runner_delegate.h"

namespace base {
namespace experimental {

JobDelegate::JobDelegate(
    internal::JobTaskSource* task_source,
    internal::PooledTaskRunnerDelegate* pooled_task_runner_delegate)
    : task_source_(task_source),
      pooled_task_runner_delegate_(pooled_task_runner_delegate) {
  DCHECK(task_source_);
  DCHECK(pooled_task_runner_delegate_);
#if DCHECK_IS_ON()
  recorded_increase_version_ = task_source_->GetConcurrencyIncreaseVersion();
  // Record max concurrency before running the worker task.
  recorded_max_concurrency_ = task_source_->GetMaxConcurrency();
#endif  // DCHECK_IS_ON()
}

JobDelegate::~JobDelegate() {
#if DCHECK_IS_ON()
  // When ShouldYield() returns false, the worker task is expected to do
  // work before returning.
  size_t expected_max_concurrency = recorded_max_concurrency_;
  if (!last_should_yield_ && expected_max_concurrency > 0)
    --expected_max_concurrency;
  AssertExpectedConcurrency(expected_max_concurrency);
#endif  // DCHECK_IS_ON()
}

bool JobDelegate::ShouldYield() {
#if DCHECK_IS_ON()
  // ShouldYield() shouldn't be called again after returning true.
  DCHECK(!last_should_yield_);
  AssertExpectedConcurrency(recorded_max_concurrency_);
#endif  // DCHECK_IS_ON()
  const bool should_yield =
      pooled_task_runner_delegate_->ShouldYield(task_source_);

#if DCHECK_IS_ON()
  last_should_yield_ = should_yield;
#endif  // DCHECK_IS_ON()
  return should_yield;
}

void JobDelegate::YieldIfNeeded() {
  // TODO(crbug.com/839091): Implement this.
}

void JobDelegate::NotifyConcurrencyIncrease() {
  task_source_->NotifyConcurrencyIncrease();
}

void JobDelegate::AssertExpectedConcurrency(size_t expected_max_concurrency) {
  // In dcheck builds, verify that max concurrency falls in one of the following
  // cases:
  // 1) max concurrency behaves normally and is below or equals the expected
  //    value.
  // 2) max concurrency increased above the expected value, which implies
  //    there are new work items that the associated worker task didn't see and
  //    NotifyConcurrencyIncrease() should be called to adjust the number of
  //    worker.
  //   a) NotifyConcurrencyIncrease() was already called and the recorded
  //      concurrency version is out of date, i.e. less than the actual version.
  //   b) NotifyConcurrencyIncrease() has not yet been called, in which case the
  //      function waits for an imminent increase of the concurrency version.
  // This prevent ill-formed GetMaxConcurrency() implementations that:
  // - Don't decrease with the number of remaining work items.
  // - Don't return an up-to-date value.
#if DCHECK_IS_ON()
  // Case 1:
  const size_t max_concurrency = task_source_->GetMaxConcurrency();
  if (max_concurrency <= expected_max_concurrency)
    return;

  // Case 2a:
  const size_t actual_version = task_source_->GetConcurrencyIncreaseVersion();
  DCHECK_LE(recorded_increase_version_, actual_version);
  if (recorded_increase_version_ < actual_version)
    return;

  // Case 2b:
  const bool updated = task_source_->WaitForConcurrencyIncreaseUpdate(
      recorded_increase_version_);
  DCHECK(updated)
      << "Value returned by |max_concurrency_callback| is expected to "
         "decrease, unless NotifyConcurrencyIncrease() is called.";

  recorded_increase_version_ = task_source_->GetConcurrencyIncreaseVersion();
  recorded_max_concurrency_ = task_source_->GetMaxConcurrency();
#endif  // DCHECK_IS_ON()
}

}  // namespace experimental
}  // namespace base