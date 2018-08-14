// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_DELAYED_TASK_MANAGER_H_
#define BASE_TASK_TASK_SCHEDULER_DELAYED_TASK_MANAGER_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/atomic_flag.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"

namespace base {

class TaskRunner;

namespace internal {

struct Task;

// The DelayedTaskManager forwards tasks to post task callbacks when they become
// ripe for execution. Tasks are not forwarded before Start() is called. This
// class is thread-safe.
class BASE_EXPORT DelayedTaskManager {
 public:
  // Posts |task| for execution immediately.
  using PostTaskNowCallback = OnceCallback<void(Task task)>;

  // |tick_clock| can be specified for testing.
  DelayedTaskManager(std::unique_ptr<const TickClock> tick_clock =
                         std::make_unique<DefaultTickClock>());
  ~DelayedTaskManager();

  // Starts the delayed task manager, allowing past and future tasks to be
  // forwarded to their callbacks as they become ripe for execution.
  // |service_thread_task_runner| posts tasks to the TaskScheduler service
  // thread.
  void Start(scoped_refptr<TaskRunner> service_thread_task_runner);

  // Schedules a call to |post_task_now_callback| with |task| as argument when
  // |task| is ripe for execution and Start() has been called.
  void AddDelayedTask(Task task, PostTaskNowCallback post_task_now_callback);

 private:
  // Schedules a call to |post_task_now_callback| with |task| as argument when
  // |delay| expires. Start() must have been called before this.
  void AddDelayedTaskNow(Task task,
                         TimeDelta delay,
                         PostTaskNowCallback post_task_now_callback);

  const std::unique_ptr<const TickClock> tick_clock_;

  AtomicFlag started_;

  // Synchronizes access to all members below before |started_| is set. Once
  // |started_| is set:
  // - |service_thread_task_runner| doest not change, so it can be read without
  //   holding the lock.
  // - |tasks_added_before_start_| isn't accessed anymore.
  SchedulerLock lock_;

  scoped_refptr<TaskRunner> service_thread_task_runner_;
  std::vector<std::pair<Task, PostTaskNowCallback>> tasks_added_before_start_;

  DISALLOW_COPY_AND_ASSIGN(DelayedTaskManager);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_DELAYED_TASK_MANAGER_H_
