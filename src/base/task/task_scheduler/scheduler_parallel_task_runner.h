// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SCHEDULER_PARALLEL_TASK_RUNNER_H_
#define BASE_TASK_TASK_SCHEDULER_SCHEDULER_PARALLEL_TASK_RUNNER_H_

#include "base/base_export.h"
#include "base/callback_forward.h"
#include "base/containers/flat_set.h"
#include "base/location.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_traits.h"
#include "base/task_runner.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"

namespace base {
namespace internal {

class Sequence;
class SchedulerTaskRunnerDelegate;

// A task runner that runs tasks in parallel.
class BASE_EXPORT SchedulerParallelTaskRunner : public TaskRunner {
 public:
  // Constructs a SchedulerParallelTaskRunner which can be used to post tasks.
  SchedulerParallelTaskRunner(
      const TaskTraits& traits,
      SchedulerTaskRunnerDelegate* scheduler_task_runner_delegate);

  // TaskRunner:
  bool PostDelayedTask(const Location& from_here,
                       OnceClosure closure,
                       TimeDelta delay) override;

  bool RunsTasksInCurrentSequence() const override;

  // Removes |sequence| from |sequences_|.
  void UnregisterSequence(Sequence* sequence);

 private:
  ~SchedulerParallelTaskRunner() override;

  const TaskTraits traits_;
  SchedulerTaskRunnerDelegate* const scheduler_task_runner_delegate_;

  SchedulerLock lock_;

  // List of alive Sequences instantiated by this SchedulerParallelTaskRunner.
  // Sequences are added when they are instantiated, and removed when they are
  // destroyed.
  base::flat_set<Sequence*> sequences_ GUARDED_BY(lock_);

  DISALLOW_COPY_AND_ASSIGN(SchedulerParallelTaskRunner);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SCHEDULER_PARALLEL_TASK_RUNNER_H_
