// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/test_utils.h"

#include <utility>

#include "base/bind.h"
#include "base/synchronization/condition_variable.h"
#include "base/task/thread_pool/pooled_parallel_task_runner.h"
#include "base/task/thread_pool/pooled_sequenced_task_runner.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {
namespace test {

MockWorkerThreadObserver::MockWorkerThreadObserver()
    : on_main_exit_cv_(lock_.CreateConditionVariable()) {}

MockWorkerThreadObserver::~MockWorkerThreadObserver() {
  WaitCallsOnMainExit();
}

void MockWorkerThreadObserver::AllowCallsOnMainExit(int num_calls) {
  CheckedAutoLock auto_lock(lock_);
  EXPECT_EQ(0, allowed_calls_on_main_exit_);
  allowed_calls_on_main_exit_ = num_calls;
}

void MockWorkerThreadObserver::WaitCallsOnMainExit() {
  CheckedAutoLock auto_lock(lock_);
  while (allowed_calls_on_main_exit_ != 0)
    on_main_exit_cv_->Wait();
}

void MockWorkerThreadObserver::OnWorkerThreadMainExit() {
  CheckedAutoLock auto_lock(lock_);
  EXPECT_GE(allowed_calls_on_main_exit_, 0);
  --allowed_calls_on_main_exit_;
  if (allowed_calls_on_main_exit_ == 0)
    on_main_exit_cv_->Signal();
}

scoped_refptr<Sequence> CreateSequenceWithTask(
    Task task,
    const TaskTraits& traits,
    scoped_refptr<TaskRunner> task_runner,
    TaskSourceExecutionMode execution_mode) {
  scoped_refptr<Sequence> sequence =
      MakeRefCounted<Sequence>(traits, task_runner.get(), execution_mode);
  sequence->BeginTransaction().PushTask(std::move(task));
  return sequence;
}

scoped_refptr<TaskRunner> CreateTaskRunnerWithExecutionMode(
    TaskSourceExecutionMode execution_mode,
    MockPooledTaskRunnerDelegate* mock_pooled_task_runner_delegate,
    const TaskTraits& traits) {
  switch (execution_mode) {
    case TaskSourceExecutionMode::kParallel:
      return CreateTaskRunnerWithTraits(traits,
                                        mock_pooled_task_runner_delegate);
    case TaskSourceExecutionMode::kSequenced:
      return CreateSequencedTaskRunnerWithTraits(
          traits, mock_pooled_task_runner_delegate);
    default:
      // Fall through.
      break;
  }
  ADD_FAILURE() << "Unexpected ExecutionMode";
  return nullptr;
}

scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(
    const TaskTraits& traits,
    MockPooledTaskRunnerDelegate* mock_pooled_task_runner_delegate) {
  return MakeRefCounted<PooledParallelTaskRunner>(
      traits, mock_pooled_task_runner_delegate);
}

scoped_refptr<SequencedTaskRunner> CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits,
    MockPooledTaskRunnerDelegate* mock_pooled_task_runner_delegate) {
  return MakeRefCounted<PooledSequencedTaskRunner>(
      traits, mock_pooled_task_runner_delegate);
}

// Waits on |event| in a scope where the blocking observer is null, to avoid
// affecting the max tasks in a thread group.
void WaitWithoutBlockingObserver(WaitableEvent* event) {
  internal::ScopedClearBlockingObserverForTesting clear_blocking_observer;
  ScopedAllowBaseSyncPrimitivesForTesting allow_base_sync_primitives;
  event->Wait();
}

MockPooledTaskRunnerDelegate::MockPooledTaskRunnerDelegate(
    TrackedRef<TaskTracker> task_tracker,
    DelayedTaskManager* delayed_task_manager)
    : task_tracker_(task_tracker),
      delayed_task_manager_(delayed_task_manager) {}

MockPooledTaskRunnerDelegate::~MockPooledTaskRunnerDelegate() = default;

bool MockPooledTaskRunnerDelegate::PostTaskWithSequence(
    Task task,
    scoped_refptr<Sequence> sequence) {
  // |thread_group_| must be initialized with SetThreadGroup() before
  // proceeding.
  DCHECK(thread_group_);
  DCHECK(task.task);
  DCHECK(sequence);

  if (!task_tracker_->WillPostTask(&task, sequence->shutdown_behavior()))
    return false;

  if (task.delayed_run_time.is_null()) {
    PostTaskWithSequenceNow(std::move(task), std::move(sequence));
  } else {
    // It's safe to take a ref on this pointer since the caller must have a ref
    // to the TaskRunner in order to post.
    scoped_refptr<TaskRunner> task_runner = sequence->task_runner();
    delayed_task_manager_->AddDelayedTask(
        std::move(task),
        BindOnce(
            [](scoped_refptr<Sequence> sequence,
               MockPooledTaskRunnerDelegate* self, Task task) {
              self->PostTaskWithSequenceNow(std::move(task),
                                            std::move(sequence));
            },
            std::move(sequence), Unretained(this)),
        std::move(task_runner));
  }

  return true;
}

void MockPooledTaskRunnerDelegate::PostTaskWithSequenceNow(
    Task task,
    scoped_refptr<Sequence> sequence) {
  auto transaction = sequence->BeginTransaction();
  const bool sequence_should_be_queued = transaction.WillPushTask();
  RegisteredTaskSource task_source;
  if (sequence_should_be_queued) {
    task_source = task_tracker_->WillQueueTaskSource(sequence);
    // We shouldn't push |task| if we're not allowed to queue |task_source|.
    if (!task_source)
      return;
  }
  transaction.PushTask(std::move(task));
  if (task_source) {
    thread_group_->PushTaskSourceAndWakeUpWorkers(
        {std::move(task_source), std::move(transaction)});
  }
}

bool MockPooledTaskRunnerDelegate::IsRunningPoolWithTraits(
    const TaskTraits& traits) const {
  // |thread_group_| must be initialized with SetThreadGroup() before
  // proceeding.
  DCHECK(thread_group_);

  return thread_group_->IsBoundToCurrentThread();
}

void MockPooledTaskRunnerDelegate::UpdatePriority(
    scoped_refptr<TaskSource> task_source,
    TaskPriority priority) {
  auto transaction = task_source->BeginTransaction();
  transaction.UpdatePriority(priority);
  thread_group_->UpdateSortKey(
      {std::move(task_source), std::move(transaction)});
}

void MockPooledTaskRunnerDelegate::SetThreadGroup(ThreadGroup* thread_group) {
  thread_group_ = thread_group;
}

RegisteredTaskSource QueueAndRunTaskSource(
    TaskTracker* task_tracker,
    scoped_refptr<TaskSource> task_source) {
  return task_tracker->RunAndPopNextTask(
      task_tracker->WillQueueTaskSource(std::move(task_source)));
}

void ShutdownTaskTracker(TaskTracker* task_tracker) {
  task_tracker->StartShutdown();
  task_tracker->CompleteShutdown();
}

}  // namespace test
}  // namespace internal
}  // namespace base
