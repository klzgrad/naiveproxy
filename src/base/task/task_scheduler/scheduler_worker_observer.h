// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_OBSERVER_H_
#define BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_OBSERVER_H_

namespace base {

// Interface to observe entry and exit of the main function of a TaskScheduler
// worker.
class SchedulerWorkerObserver {
 public:
  virtual ~SchedulerWorkerObserver() = default;

  // Invoked at the beginning of the main function of a TaskScheduler worker,
  // before any task runs.
  virtual void OnSchedulerWorkerMainEntry() = 0;

  // Invoked at the end of the main function of a TaskScheduler worker, when it
  // can no longer run tasks.
  virtual void OnSchedulerWorkerMainExit() = 0;
};

}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_OBSERVER_H_
