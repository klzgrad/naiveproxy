// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_PARAMS_H_
#define BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_PARAMS_H_

#include "base/task_scheduler/scheduler_worker_params.h"
#include "base/time/time.h"

namespace base {

class BASE_EXPORT SchedulerWorkerPoolParams final {
 public:
  // Constructs a set of params used to initialize a pool. The pool will contain
  // up to |max_threads|. |suggested_reclaim_time| sets a suggestion on when to
  // reclaim idle threads. The pool is free to ignore this value for performance
  // or correctness reasons. |backward_compatibility| indicates whether backward
  // compatibility is enabled.
  SchedulerWorkerPoolParams(
      int max_threads,
      TimeDelta suggested_reclaim_time,
      SchedulerBackwardCompatibility backward_compatibility =
          SchedulerBackwardCompatibility::DISABLED);

  SchedulerWorkerPoolParams(const SchedulerWorkerPoolParams& other);
  SchedulerWorkerPoolParams& operator=(const SchedulerWorkerPoolParams& other);

  int max_threads() const { return max_threads_; }
  TimeDelta suggested_reclaim_time() const { return suggested_reclaim_time_; }
  SchedulerBackwardCompatibility backward_compatibility() const {
    return backward_compatibility_;
  }

 private:
  int max_threads_;
  TimeDelta suggested_reclaim_time_;
  SchedulerBackwardCompatibility backward_compatibility_;
};

}  // namespace base

#endif  // BASE_TASK_SCHEDULER_SCHEDULER_WORKER_POOL_PARAMS_H_
