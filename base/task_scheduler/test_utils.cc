// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/test_utils.h"

#include <utility>

#include "base/task_scheduler/scheduler_worker_pool.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {
namespace test {

MockSchedulerWorkerObserver::MockSchedulerWorkerObserver() = default;
MockSchedulerWorkerObserver::~MockSchedulerWorkerObserver() = default;

scoped_refptr<Sequence> CreateSequenceWithTask(Task task) {
  scoped_refptr<Sequence> sequence = MakeRefCounted<Sequence>();
  sequence->PushTask(std::move(task));
  return sequence;
}

scoped_refptr<TaskRunner> CreateTaskRunnerWithExecutionMode(
    SchedulerWorkerPool* worker_pool,
    test::ExecutionMode execution_mode) {
  // Allow tasks posted to the returned TaskRunner to wait on a WaitableEvent.
  const TaskTraits traits = {WithBaseSyncPrimitives()};
  switch (execution_mode) {
    case test::ExecutionMode::PARALLEL:
      return worker_pool->CreateTaskRunnerWithTraits(traits);
    case test::ExecutionMode::SEQUENCED:
      return worker_pool->CreateSequencedTaskRunnerWithTraits(traits);
    default:
      // Fall through.
      break;
  }
  ADD_FAILURE() << "Unexpected ExecutionMode";
  return nullptr;
}

}  // namespace test
}  // namespace internal
}  // namespace base
