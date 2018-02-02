// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_TASK_RUNNER_INTERFACE_H_
#define NET_QUIC_QUARTC_QUARTC_TASK_RUNNER_INTERFACE_H_

#include <stdint.h>

#include <memory>

namespace net {

// Used by platform specific QuicAlarms. For example, WebRTC will use it to set
// and cancel an alarm. When setting an alarm, the task runner will schedule a
// task on rtc::Thread. When canceling an alarm, the canceler for that task will
// be called.
class QuartcTaskRunnerInterface {
 public:
  virtual ~QuartcTaskRunnerInterface() {}

  class Task {
   public:
    virtual ~Task() {}

    // Called when it's time to start the task.
    virtual void Run() = 0;
  };

  // A handler used to cancel a scheduled task. In some cases, a task cannot
  // be directly canceled with its pointer. For example, in WebRTC, the task
  // will be scheduled on rtc::Thread. When canceling a task, its pointer cannot
  // locate the scheduled task in the thread message queue. So when scheduling a
  // task, an additional handler (ScheduledTask) will be returned.
  class ScheduledTask {
   public:
    virtual ~ScheduledTask() {}

    // Cancels a scheduled task, meaning the task will not be run.
    virtual void Cancel() = 0;
  };

  // Schedules a task, which will be run after the given delay. A ScheduledTask
  // may be used to cancel the task.
  virtual std::unique_ptr<ScheduledTask> Schedule(Task* task,
                                                  uint64_t delay_ms) = 0;
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_TASK_RUNNER_INTERFACE_H_
