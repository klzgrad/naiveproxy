// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEFERRED_SEQUENCED_TASK_RUNNER_H_
#define BASE_DEFERRED_SEQUENCED_TASK_RUNNER_H_

#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace base {

// A DeferredSequencedTaskRunner is a subclass of SequencedTaskRunner that
// queues up all requests until the first call to Start() is issued.
class BASE_EXPORT DeferredSequencedTaskRunner : public SequencedTaskRunner {
 public:
  explicit DeferredSequencedTaskRunner(
      scoped_refptr<SequencedTaskRunner> target_runner);

  // TaskRunner implementation
  bool PostDelayedTask(const Location& from_here,
                       OnceClosure task,
                       TimeDelta delay) override;
  bool RunsTasksInCurrentSequence() const override;

  // SequencedTaskRunner implementation
  bool PostNonNestableDelayedTask(const Location& from_here,
                                  OnceClosure task,
                                  TimeDelta delay) override;

  // Start the execution - posts all queued tasks to the target executor. The
  // deferred tasks are posted with their initial delay, meaning that the task
  // execution delay is actually measured from Start.
  // Fails when called a second time.
  void Start();

 private:
  struct DeferredTask  {
    DeferredTask();
    DeferredTask(DeferredTask&& other);
    ~DeferredTask();
    DeferredTask& operator=(DeferredTask&& other);

    Location posted_from;
    OnceClosure task;
    // The delay this task was initially posted with.
    TimeDelta delay;
    bool is_non_nestable;
  };

  ~DeferredSequencedTaskRunner() override;

  // Creates a |Task| object and adds it to |deferred_tasks_queue_|.
  void QueueDeferredTask(const Location& from_here,
                         OnceClosure task,
                         TimeDelta delay,
                         bool is_non_nestable);

  // // Protects |started_| and |deferred_tasks_queue_|.
  mutable Lock lock_;

  bool started_;
  const scoped_refptr<SequencedTaskRunner> target_task_runner_;
  std::vector<DeferredTask> deferred_tasks_queue_;

  DISALLOW_COPY_AND_ASSIGN(DeferredSequencedTaskRunner);
};

}  // namespace base

#endif  // BASE_DEFERRED_SEQUENCED_TASK_RUNNER_H_
