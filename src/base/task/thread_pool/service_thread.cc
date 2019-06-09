// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/service_thread.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/debug/alias.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/task/post_task.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool/task_tracker.h"
#include "base/task/thread_pool/thread_pool.h"

namespace base {
namespace internal {

namespace {

TimeDelta g_heartbeat_for_testing = TimeDelta();

}  // namespace

ServiceThread::ServiceThread(const TaskTracker* task_tracker,
                             RepeatingClosure report_heartbeat_metrics_callback)
    : Thread("ThreadPoolServiceThread"),
      task_tracker_(task_tracker),
      report_heartbeat_metrics_callback_(
          std::move(report_heartbeat_metrics_callback)) {}

ServiceThread::~ServiceThread() = default;

// static
void ServiceThread::SetHeartbeatIntervalForTesting(TimeDelta heartbeat) {
  g_heartbeat_for_testing = heartbeat;
}

void ServiceThread::Init() {
  // In unit tests we sometimes do not have a fully functional thread pool
  // environment, do not perform the heartbeat report in that case since it
  // relies on such an environment.
  if (ThreadPoolInstance::Get()) {
    // Compute the histogram every hour (with a slight offset to drift if that
    // hour tick happens to line up with specific events). Once per hour per
    // user was deemed sufficient to gather a reliable metric.
    constexpr TimeDelta kHeartbeat = TimeDelta::FromMinutes(59);

    heartbeat_metrics_timer_.Start(
        FROM_HERE,
        g_heartbeat_for_testing.is_zero() ? kHeartbeat
                                          : g_heartbeat_for_testing,
        BindRepeating(&ServiceThread::ReportHeartbeatMetrics,
                      Unretained(this)));
  }
}

NOINLINE void ServiceThread::Run(RunLoop* run_loop) {
  const int line_number = __LINE__;
  Thread::Run(run_loop);
  base::debug::Alias(&line_number);
}

void ServiceThread::ReportHeartbeatMetrics() const {
  report_heartbeat_metrics_callback_.Run();
  PerformHeartbeatLatencyReport();
}

void ServiceThread::PerformHeartbeatLatencyReport() const {
  if (!task_tracker_)
    return;

  static constexpr TaskTraits kReportedTraits[] = {
      {TaskPriority::BEST_EFFORT},   {TaskPriority::BEST_EFFORT, MayBlock()},
      {TaskPriority::USER_VISIBLE},  {TaskPriority::USER_VISIBLE, MayBlock()},
      {TaskPriority::USER_BLOCKING}, {TaskPriority::USER_BLOCKING, MayBlock()}};

  // Only record latency for one set of TaskTraits per report to avoid bias in
  // the order in which tasks are posted (should we record all at once) as well
  // as to avoid spinning up many worker threads to process this report if the
  // thread pool is currently idle (each thread group keeps at least one idle
  // thread so a single task isn't an issue).

  // Invoke RandInt() out-of-line to ensure it's obtained before
  // TimeTicks::Now().
  const TaskTraits& profiled_traits =
      kReportedTraits[RandInt(0, base::size(kReportedTraits) - 1)];

  // Post through the static API to time the full stack. Use a new Now() for
  // every set of traits in case PostTaskWithTraits() itself is slow.
  // Bonus: this approach also includes the overhead of BindOnce() in the
  // reported latency.
  // TODO(jessemckenna): pass |profiled_traits| directly to
  // RecordHeartbeatLatencyAndTasksRunWhileQueuingHistograms() once compiler
  // error on NaCl is fixed
  TaskPriority task_priority = profiled_traits.priority();
  bool may_block = profiled_traits.may_block();
  base::PostTaskWithTraits(
      FROM_HERE, profiled_traits,
      BindOnce(
          &TaskTracker::RecordHeartbeatLatencyAndTasksRunWhileQueuingHistograms,
          Unretained(task_tracker_), task_priority, may_block, TimeTicks::Now(),
          task_tracker_->GetNumTasksRun()));
}

}  // namespace internal
}  // namespace base
