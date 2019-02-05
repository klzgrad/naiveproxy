// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/sequence.h"

#include <utility>

#include "base/critical_closure.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/time/time.h"

namespace base {
namespace internal {

SequenceAndTransaction::SequenceAndTransaction(
    scoped_refptr<Sequence> sequence_in,
    Sequence::Transaction transaction_in)
    : sequence(std::move(sequence_in)),
      transaction(std::move(transaction_in)) {}

SequenceAndTransaction::SequenceAndTransaction(SequenceAndTransaction&& other) =
    default;

SequenceAndTransaction::~SequenceAndTransaction() = default;

Sequence::Transaction::Transaction(scoped_refptr<Sequence> sequence)
    : sequence_(sequence.get()) {
  sequence_->lock_.Acquire();
}

Sequence::Transaction::Transaction(Sequence::Transaction&& other)
    : sequence_(other.sequence()) {
  other.sequence_ = nullptr;
}

Sequence::Transaction::~Transaction() {
  if (sequence_) {
    sequence_->lock_.AssertAcquired();
    sequence_->lock_.Release();
  }
}

bool Sequence::Transaction::PushTask(Task task) {
  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(task.task);
  DCHECK(task.sequenced_time.is_null());
  task.sequenced_time = base::TimeTicks::Now();

  task.task = sequence_->traits_.shutdown_behavior() ==
                      TaskShutdownBehavior::BLOCK_SHUTDOWN
                  ? MakeCriticalClosure(std::move(task.task))
                  : std::move(task.task);

  sequence_->queue_.push(std::move(task));

  // Return true if the sequence was empty before the push.
  return sequence_->queue_.size() == 1;
}

Optional<Task> Sequence::Transaction::TakeTask() {
  DCHECK(!IsEmpty());
  DCHECK(sequence_->queue_.front().task);

  return std::move(sequence_->queue_.front());
}

bool Sequence::Transaction::Pop() {
  DCHECK(!IsEmpty());
  DCHECK(!sequence_->queue_.front().task);
  sequence_->queue_.pop();
  return IsEmpty();
}

SequenceSortKey Sequence::Transaction::GetSortKey() const {
  DCHECK(!IsEmpty());

  // Save the sequenced time of the next task in the sequence.
  base::TimeTicks next_task_sequenced_time =
      sequence_->queue_.front().sequenced_time;

  return SequenceSortKey(sequence_->traits_.priority(),
                         next_task_sequenced_time);
}

bool Sequence::Transaction::IsEmpty() const {
  return sequence_->queue_.empty();
}

void Sequence::Transaction::UpdatePriority(TaskPriority priority) {
  if (FeatureList::IsEnabled(kAllTasksUserBlocking))
    return;
  sequence_->traits_.UpdatePriority(priority);
}

void Sequence::SetHeapHandle(const HeapHandle& handle) {
  heap_handle_ = handle;
}

void Sequence::ClearHeapHandle() {
  heap_handle_ = HeapHandle();
}

Sequence::Sequence(
    const TaskTraits& traits,
    scoped_refptr<SchedulerParallelTaskRunner> scheduler_parallel_task_runner)
    : traits_(traits),
      scheduler_parallel_task_runner_(scheduler_parallel_task_runner) {}

Sequence::~Sequence() {
  if (scheduler_parallel_task_runner_) {
    scheduler_parallel_task_runner_->UnregisterSequence(this);
  }
}

Sequence::Transaction Sequence::BeginTransaction() {
  return Transaction(this);
}

// static
SequenceAndTransaction SequenceAndTransaction::FromSequence(
    scoped_refptr<Sequence> sequence) {
  DCHECK(sequence);
  Sequence::Transaction transaction(sequence->BeginTransaction());
  return SequenceAndTransaction(std::move(sequence), std::move(transaction));
}

}  // namespace internal
}  // namespace base
