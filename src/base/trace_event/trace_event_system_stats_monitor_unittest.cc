// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/trace_event_system_stats_monitor.h"

#include <sstream>
#include <string>

#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/trace_event/trace_event_impl.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace trace_event {

#if !defined(OS_IOS)
// Tests for the system stats monitor.
// Exists as a class so it can be a friend of TraceEventSystemStatsMonitor.
class TraceSystemStatsMonitorTest : public testing::Test {
 public:
  TraceSystemStatsMonitorTest() = default;
  ~TraceSystemStatsMonitorTest() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(TraceSystemStatsMonitorTest);
};

//////////////////////////////////////////////////////////////////////////////

TEST_F(TraceSystemStatsMonitorTest, TraceEventSystemStatsMonitor) {
  MessageLoop message_loop;

  // Start with no observers of the TraceLog.
  EXPECT_EQ(0u, TraceLog::GetInstance()->GetObserverCountForTest());

  // Creating a system stats monitor adds it to the TraceLog observer list.
  std::unique_ptr<TraceEventSystemStatsMonitor> system_stats_monitor(
      new TraceEventSystemStatsMonitor(message_loop.task_runner()));
  EXPECT_EQ(1u, TraceLog::GetInstance()->GetObserverCountForTest());
  EXPECT_TRUE(
      TraceLog::GetInstance()->HasEnabledStateObserver(
          system_stats_monitor.get()));

  // By default the observer isn't dumping memory profiles.
  EXPECT_FALSE(system_stats_monitor->IsTimerRunningForTest());

  // Simulate enabling tracing.
  system_stats_monitor->StartProfiling();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(system_stats_monitor->IsTimerRunningForTest());

  // Simulate disabling tracing.
  system_stats_monitor->StopProfiling();
  RunLoop().RunUntilIdle();
  EXPECT_FALSE(system_stats_monitor->IsTimerRunningForTest());

  // Deleting the observer removes it from the TraceLog observer list.
  system_stats_monitor.reset();
  EXPECT_EQ(0u, TraceLog::GetInstance()->GetObserverCountForTest());
}
#endif  // !defined(OS_IOS)

}  // namespace trace_event
}  // namespace base
