// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/service_thread.h"

#include <string>

#include "base/bind.h"
#include "base/debug/stack_trace.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/task/task_scheduler/task_scheduler_impl.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

namespace {

// Verifies that |query| is found on the current stack. Ignores failures if this
// configuration doesn't have symbols.
void VerifyHasStringOnStack(const std::string& query) {
  const std::string stack = debug::StackTrace().ToString();
  SCOPED_TRACE(stack);
  const bool found_on_stack = stack.find(query) != std::string::npos;
  const bool stack_has_symbols =
      stack.find("SchedulerWorker") != std::string::npos;
  EXPECT_TRUE(found_on_stack || !stack_has_symbols) << query;
}

}  // namespace

#if defined(OS_POSIX)
// Many POSIX bots flakily crash on |debug::StackTrace().ToString()|,
// https://crbug.com/840429.
#define MAYBE_StackHasIdentifyingFrame DISABLED_StackHasIdentifyingFrame
#else
#define MAYBE_StackHasIdentifyingFrame StackHasIdentifyingFrame
#endif

TEST(TaskSchedulerServiceThreadTest, MAYBE_StackHasIdentifyingFrame) {
  ServiceThread service_thread(nullptr, DoNothing());
  service_thread.Start();

  service_thread.task_runner()->PostTask(
      FROM_HERE, BindOnce(&VerifyHasStringOnStack, "ServiceThread"));

  service_thread.FlushForTesting();
}

// Integration test verifying that a service thread running in a fully
// integrated TaskScheduler environment results in reporting
// HeartbeatLatencyMicroseconds metrics.
TEST(TaskSchedulerServiceThreadIntegrationTest, HeartbeatLatencyReport) {
  ServiceThread::SetHeartbeatIntervalForTesting(TimeDelta::FromMilliseconds(1));

  TaskScheduler::SetInstance(
      std::make_unique<internal::TaskSchedulerImpl>("Test"));
  TaskScheduler::GetInstance()->StartWithDefaultParams();

  static constexpr const char* kExpectedMetrics[] = {
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "UserBlockingTaskPriority",
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "UserBlockingTaskPriority_MayBlock",
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "UserVisibleTaskPriority",
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "UserVisibleTaskPriority_MayBlock",
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "BackgroundTaskPriority",
      "TaskScheduler.HeartbeatLatencyMicroseconds.Test."
      "BackgroundTaskPriority_MayBlock"};

  // Each report hits a single histogram above (randomly selected). But 1000
  // reports should touch all histograms at least once the vast majority of the
  // time.
  constexpr TimeDelta kReasonableTimeout = TimeDelta::FromSeconds(1);
  constexpr TimeDelta kBusyWaitTime = TimeDelta::FromMilliseconds(100);

  const TimeTicks start_time = TimeTicks::Now();

  HistogramTester tester;
  for (const char* expected_metric : kExpectedMetrics) {
    while (tester.GetAllSamples(expected_metric).empty()) {
      if (TimeTicks::Now() - start_time > kReasonableTimeout)
        LOG(WARNING) << "Waiting a while for " << expected_metric;
      PlatformThread::Sleep(kBusyWaitTime);
    }
  }

  TaskScheduler::GetInstance()->JoinForTesting();
  TaskScheduler::SetInstance(nullptr);

  ServiceThread::SetHeartbeatIntervalForTesting(TimeDelta());
}

}  // namespace internal
}  // namespace base
