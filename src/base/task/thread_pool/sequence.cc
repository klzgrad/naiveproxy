// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/sequence.h"

#include <utility>

#include "base/critical_closure.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/time/time.h"

namespace base {
namespace internal {

Sequence::Transaction::Transaction(Sequence* sequence)
    : TaskSource::Transaction(sequence) {}

Sequence::Transaction::Transaction(Sequence::Transaction&& other) = default;

Sequence::Transaction::~Transaction() = default;

bool Sequence::Transaction::WillPushTask() const {
  // If the sequence is empty before a Task is inserted into it and the pool is
  // not running any task from this sequence, it should be queued.
  // Otherwise, one of these must be true:
  // - The Sequence is already queued, or,
  // - A thread is running a Task from the Sequence. It is expected to reenqueue
  //   the Sequence once it's done running the Task.
  return sequence()->queue_.empty() && !sequence()->has_worker_;
}

void Sequence::Transaction::PushTask(Task task) {
  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(task.task);
  DCHECK(task.queue_time.is_null());

  bool should_be_queued = WillPushTask();
  task.queue_time = base::TimeTicks::Now();

  task.task = sequence()->traits_.shutdown_behavior() ==
                      TaskShutdownBehavior::BLOCK_SHUTDOWN
                  ? MakeCriticalClosure(std::move(task.task))
                  : std::move(task.task);

  sequence()->queue_.push(std::move(task));

  // AddRef() matched by manual Release() when the sequence has no more tasks
  // to run (in DidRunTask() or Clear()).
  if (should_be_queued && sequence()->task_runner())
    sequence()->task_runner()->AddRef();
}

Optional<Task> Sequence::TakeTask() {
  DCHECK(!has_worker_);
  DCHECK(!queue_.empty());
  DCHECK(queue_.front().task);

  has_worker_ = true;
  auto next_task = std::move(queue_.front());
  queue_.pop();
  return std::move(next_task);
}

bool Sequence::DidRunTask() {
  DCHECK(has_worker_);
  has_worker_ = false;
  if (queue_.empty()) {
    ReleaseTaskRunner();
    return false;
  }
  return true;
}

SequenceSortKey Sequence::GetSortKey() const {
  DCHECK(!queue_.empty());
  return SequenceSortKey(traits_.priority(), queue_.front().queue_time);
}

void Sequence::Clear() {
  bool queue_was_empty = queue_.empty();
  while (!queue_.empty())
    queue_.pop();
  if (!queue_was_empty) {
    // No member access after this point, ReleaseTaskRunner() might have deleted
    // |this|.
    ReleaseTaskRunner();
  }
}

void Sequence::ReleaseTaskRunner() {
  if (!task_runner())
    return;
  if (execution_mode() == TaskSourceExecutionMode::kParallel) {
    static_cast<PooledParallelTaskRunner*>(task_runner())
        ->UnregisterSequence(this);
  }
  // No member access after this point, releasing |task_runner()| might delete
  // |this|.
  task_runner()->Release();
}

Sequence::Sequence(const TaskTraits& traits,
                   TaskRunner* task_runner,
                   TaskSourceExecutionMode execution_mode)
    : TaskSource(traits, task_runner, execution_mode) {}

Sequence::~Sequence() = default;

Sequence::Transaction Sequence::BeginTransaction() {
  return Transaction(this);
}

ExecutionEnvironment Sequence::GetExecutionEnvironment() {
  return {token_, &sequence_local_storage_};
}

}  // namespace internal
}  // namespace base
