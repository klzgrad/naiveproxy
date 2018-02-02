// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/cancelable_task_tracker.h"

#include <stddef.h>

#include <utility>

#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/cancellation_flag.h"
#include "base/task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace base {

namespace {

void RunIfNotCanceled(const CancellationFlag* flag, OnceClosure task) {
  if (!flag->IsSet())
    std::move(task).Run();
}

void RunIfNotCanceledThenUntrack(const CancellationFlag* flag,
                                 OnceClosure task,
                                 OnceClosure untrack) {
  RunIfNotCanceled(flag, std::move(task));
  std::move(untrack).Run();
}

bool IsCanceled(const CancellationFlag* flag,
                ScopedClosureRunner* cleanup_runner) {
  return flag->IsSet();
}

void RunAndDeleteFlag(OnceClosure closure, const CancellationFlag* flag) {
  std::move(closure).Run();
  delete flag;
}

void RunOrPostToTaskRunner(TaskRunner* task_runner, OnceClosure closure) {
  if (task_runner->RunsTasksInCurrentSequence())
    std::move(closure).Run();
  else
    task_runner->PostTask(FROM_HERE, std::move(closure));
}

}  // namespace

// static
const CancelableTaskTracker::TaskId CancelableTaskTracker::kBadTaskId = 0;

CancelableTaskTracker::CancelableTaskTracker()
    : next_id_(1),weak_factory_(this) {}

CancelableTaskTracker::~CancelableTaskTracker() {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  TryCancelAll();
}

CancelableTaskTracker::TaskId CancelableTaskTracker::PostTask(
    TaskRunner* task_runner,
    const Location& from_here,
    OnceClosure task) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  return PostTaskAndReply(task_runner, from_here, std::move(task),
                          BindOnce(&DoNothing));
}

CancelableTaskTracker::TaskId CancelableTaskTracker::PostTaskAndReply(
    TaskRunner* task_runner,
    const Location& from_here,
    OnceClosure task,
    OnceClosure reply) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  // We need a SequencedTaskRunnerHandle to run |reply|.
  DCHECK(SequencedTaskRunnerHandle::IsSet());

  // Owned by reply callback below.
  CancellationFlag* flag = new CancellationFlag();

  TaskId id = next_id_;
  next_id_++;  // int64_t is big enough that we ignore the potential overflow.

  OnceClosure untrack_closure =
      BindOnce(&CancelableTaskTracker::Untrack, weak_factory_.GetWeakPtr(), id);
  bool success = task_runner->PostTaskAndReply(
      from_here, BindOnce(&RunIfNotCanceled, flag, std::move(task)),
      BindOnce(&RunIfNotCanceledThenUntrack, Owned(flag), std::move(reply),
               std::move(untrack_closure)));

  if (!success)
    return kBadTaskId;

  Track(id, flag);
  return id;
}

CancelableTaskTracker::TaskId CancelableTaskTracker::NewTrackedTaskId(
    IsCanceledCallback* is_canceled_cb) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  DCHECK(SequencedTaskRunnerHandle::IsSet());

  TaskId id = next_id_;
  next_id_++;  // int64_t is big enough that we ignore the potential overflow.

  // Will be deleted by |untrack_and_delete_flag| after Untrack().
  CancellationFlag* flag = new CancellationFlag();

  OnceClosure untrack_and_delete_flag = BindOnce(
      &RunAndDeleteFlag,
      BindOnce(&CancelableTaskTracker::Untrack, weak_factory_.GetWeakPtr(), id),
      flag);

  // Will always run |untrack_and_delete_flag| on current sequence.
  ScopedClosureRunner* untrack_and_delete_flag_runner =
      new ScopedClosureRunner(Bind(
          &RunOrPostToTaskRunner, RetainedRef(SequencedTaskRunnerHandle::Get()),
          Passed(&untrack_and_delete_flag)));

  *is_canceled_cb =
      Bind(&IsCanceled, flag, Owned(untrack_and_delete_flag_runner));

  Track(id, flag);
  return id;
}

void CancelableTaskTracker::TryCancel(TaskId id) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  const auto it = task_flags_.find(id);
  if (it == task_flags_.end()) {
    // Two possibilities:
    //
    //   1. The task has already been untracked.
    //   2. The TaskId is bad or unknown.
    //
    // Since this function is best-effort, it's OK to ignore these.
    return;
  }
  it->second->Set();
}

void CancelableTaskTracker::TryCancelAll() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  for (const auto& it : task_flags_)
    it.second->Set();
}

bool CancelableTaskTracker::HasTrackedTasks() const {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  return !task_flags_.empty();
}

void CancelableTaskTracker::Track(TaskId id, CancellationFlag* flag) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  bool success = task_flags_.insert(std::make_pair(id, flag)).second;
  DCHECK(success);
}

void CancelableTaskTracker::Untrack(TaskId id) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  size_t num = task_flags_.erase(id);
  DCHECK_EQ(1u, num);
}

}  // namespace base
