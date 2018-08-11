// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/service_thread.h"

#include "base/debug/alias.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/task_scheduler/task_traits.h"
#include "base/time/time.h"

namespace base {
namespace internal {

ServiceThread::ServiceThread(const TaskTracker* task_tracker)
    : Thread("TaskSchedulerServiceThread"), task_tracker_(task_tracker) {}

void ServiceThread::Init() {
  // In unit tests we sometimes do not have a fully functional TaskScheduler
  // environment, do not perform the heartbeat report in that case since it
  // relies on such an environment.
  if (task_tracker_ && TaskScheduler::GetInstance()) {
    heartbeat_latency_timer_.Start(
        FROM_HERE, TimeDelta::FromSeconds(5),
        BindRepeating(&ServiceThread::PerformHeartbeatLatencyReport,
                      Unretained(this)));
  }
}

NOINLINE void ServiceThread::Run(RunLoop* run_loop) {
  const int line_number = __LINE__;
  Thread::Run(run_loop);
  base::debug::Alias(&line_number);
}

void ServiceThread::PerformHeartbeatLatencyReport() const {
  static constexpr TaskTraits kReportedTraits[] = {
      {TaskPriority::BACKGROUND},    {TaskPriority::BACKGROUND, MayBlock()},
      {TaskPriority::USER_VISIBLE},  {TaskPriority::USER_VISIBLE, MayBlock()},
      {TaskPriority::USER_BLOCKING}, {TaskPriority::USER_BLOCKING, MayBlock()}};

  for (auto& traits : kReportedTraits) {
    // Post through the static API to time the full stack. Use a new Now() for
    // every set of traits in case PostTaskWithTraits() itself is slow.
    base::PostTaskWithTraits(
        FROM_HERE, traits,
        BindOnce(&TaskTracker::RecordLatencyHistogram,
                 Unretained(task_tracker_),
                 TaskTracker::LatencyHistogramType::HEARTBEAT_LATENCY, traits,
                 TimeTicks::Now()));
  }
}

}  // namespace internal
}  // namespace base
