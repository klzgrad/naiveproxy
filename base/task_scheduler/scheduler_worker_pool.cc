// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_worker_pool.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/lazy_instance.h"
#include "base/task_scheduler/delayed_task_manager.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/threading/thread_local.h"

namespace base {
namespace internal {

namespace {

// The number of SchedulerWorkerPool that are alive in this process. This
// variable should only be incremented when the SchedulerWorkerPool instances
// are brought up (on the main thread; before any tasks are posted) and
// decremented when the same instances are brought down (i.e., only when unit
// tests tear down the task environment and never in production). This makes the
// variable const while worker threads are up and as such it doesn't need to be
// atomic. It is used to tell when a task is posted from the main thread after
// the task environment was brought down in unit tests so that
// SchedulerWorkerPool bound TaskRunners can return false on PostTask, letting
// such callers know they should complete necessary work synchronously. Note:
// |!g_active_pools_count| is generally equivalent to
// |!TaskScheduler::GetInstance()| but has the advantage of being valid in
// task_scheduler unit tests that don't instantiate a full TaskScheduler.
int g_active_pools_count = 0;

// SchedulerWorkerPool that owns the current thread, if any.
LazyInstance<ThreadLocalPointer<const SchedulerWorkerPool>>::Leaky
    tls_current_worker_pool = LAZY_INSTANCE_INITIALIZER;

const SchedulerWorkerPool* GetCurrentWorkerPool() {
  return tls_current_worker_pool.Get().Get();
}

}  // namespace

// A task runner that runs tasks in parallel.
class SchedulerParallelTaskRunner : public TaskRunner {
 public:
  // Constructs a SchedulerParallelTaskRunner which can be used to post tasks so
  // long as |worker_pool| is alive.
  // TODO(robliao): Find a concrete way to manage |worker_pool|'s memory.
  SchedulerParallelTaskRunner(const TaskTraits& traits,
                              SchedulerWorkerPool* worker_pool)
      : traits_(traits), worker_pool_(worker_pool) {
    DCHECK(worker_pool_);
  }

  // TaskRunner:
  bool PostDelayedTask(const Location& from_here,
                       OnceClosure closure,
                       TimeDelta delay) override {
    if (!g_active_pools_count)
      return false;

    // Post the task as part of a one-off single-task Sequence.
    return worker_pool_->PostTaskWithSequence(
        std::make_unique<Task>(from_here, std::move(closure), traits_, delay),
        MakeRefCounted<Sequence>());
  }

  bool RunsTasksInCurrentSequence() const override {
    return GetCurrentWorkerPool() == worker_pool_;
  }

 private:
  ~SchedulerParallelTaskRunner() override = default;

  const TaskTraits traits_;
  SchedulerWorkerPool* const worker_pool_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerParallelTaskRunner);
};

// A task runner that runs tasks in sequence.
class SchedulerSequencedTaskRunner : public SequencedTaskRunner {
 public:
  // Constructs a SchedulerSequencedTaskRunner which can be used to post tasks
  // so long as |worker_pool| is alive.
  // TODO(robliao): Find a concrete way to manage |worker_pool|'s memory.
  SchedulerSequencedTaskRunner(const TaskTraits& traits,
                               SchedulerWorkerPool* worker_pool)
      : traits_(traits), worker_pool_(worker_pool) {
    DCHECK(worker_pool_);
  }

  // SequencedTaskRunner:
  bool PostDelayedTask(const Location& from_here,
                       OnceClosure closure,
                       TimeDelta delay) override {
    if (!g_active_pools_count)
      return false;

    std::unique_ptr<Task> task =
        std::make_unique<Task>(from_here, std::move(closure), traits_, delay);
    task->sequenced_task_runner_ref = this;

    // Post the task as part of |sequence_|.
    return worker_pool_->PostTaskWithSequence(std::move(task), sequence_);
  }

  bool PostNonNestableDelayedTask(const Location& from_here,
                                  OnceClosure closure,
                                  base::TimeDelta delay) override {
    // Tasks are never nested within the task scheduler.
    return PostDelayedTask(from_here, std::move(closure), delay);
  }

  bool RunsTasksInCurrentSequence() const override {
    return sequence_->token() == SequenceToken::GetForCurrentThread();
  }

 private:
  ~SchedulerSequencedTaskRunner() override = default;

  // Sequence for all Tasks posted through this TaskRunner.
  const scoped_refptr<Sequence> sequence_ = MakeRefCounted<Sequence>();

  const TaskTraits traits_;
  SchedulerWorkerPool* const worker_pool_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerSequencedTaskRunner);
};

scoped_refptr<TaskRunner> SchedulerWorkerPool::CreateTaskRunnerWithTraits(
    const TaskTraits& traits) {
  return MakeRefCounted<SchedulerParallelTaskRunner>(traits, this);
}

scoped_refptr<SequencedTaskRunner>
SchedulerWorkerPool::CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits) {
  return MakeRefCounted<SchedulerSequencedTaskRunner>(traits, this);
}

bool SchedulerWorkerPool::PostTaskWithSequence(
    std::unique_ptr<Task> task,
    scoped_refptr<Sequence> sequence) {
  DCHECK(task);
  DCHECK(sequence);

  if (!task_tracker_->WillPostTask(task.get()))
    return false;

  if (task->delayed_run_time.is_null()) {
    PostTaskWithSequenceNow(std::move(task), std::move(sequence));
  } else {
    // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
    // for details.
    CHECK(task->task);
    delayed_task_manager_->AddDelayedTask(
        std::move(task),
        BindOnce(
            [](scoped_refptr<Sequence> sequence,
               SchedulerWorkerPool* worker_pool, std::unique_ptr<Task> task) {
              worker_pool->PostTaskWithSequenceNow(std::move(task),
                                                   std::move(sequence));
            },
            std::move(sequence), Unretained(this)));
  }

  return true;
}

SchedulerWorkerPool::SchedulerWorkerPool(
    TaskTracker* task_tracker,
    DelayedTaskManager* delayed_task_manager)
    : task_tracker_(task_tracker), delayed_task_manager_(delayed_task_manager) {
  DCHECK(task_tracker_);
  DCHECK(delayed_task_manager_);
  ++g_active_pools_count;
}

SchedulerWorkerPool::~SchedulerWorkerPool() {
  --g_active_pools_count;
  DCHECK_GE(g_active_pools_count, 0);
}

void SchedulerWorkerPool::BindToCurrentThread() {
  DCHECK(!GetCurrentWorkerPool());
  tls_current_worker_pool.Get().Set(this);
}

void SchedulerWorkerPool::UnbindFromCurrentThread() {
  DCHECK(GetCurrentWorkerPool());
  tls_current_worker_pool.Get().Set(nullptr);
}

void SchedulerWorkerPool::PostTaskWithSequenceNow(
    std::unique_ptr<Task> task,
    scoped_refptr<Sequence> sequence) {
  DCHECK(task);
  DCHECK(sequence);

  // Confirm that |task| is ready to run (its delayed run time is either null or
  // in the past).
  DCHECK_LE(task->delayed_run_time, TimeTicks::Now());

  const bool sequence_was_empty = sequence->PushTask(std::move(task));
  if (sequence_was_empty) {
    // Try to schedule |sequence| if it was empty before |task| was inserted
    // into it. Otherwise, one of these must be true:
    // - |sequence| is already scheduled, or,
    // - The pool is running a Task from |sequence|. The pool is expected to
    //   reschedule |sequence| once it's done running the Task.
    sequence = task_tracker_->WillScheduleSequence(std::move(sequence), this);
    if (sequence)
      OnCanScheduleSequence(std::move(sequence));
  }
}

}  // namespace internal
}  // namespace base
