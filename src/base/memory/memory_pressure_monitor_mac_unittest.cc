// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_pressure_monitor_mac.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/macros.h"
#include "base/test/metrics/histogram_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace mac {

class TestMemoryPressureMonitor : public MemoryPressureMonitor {
 public:
  using MemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel;

  // A HistogramTester for verifying correct UMA stat generation.
  base::HistogramTester tester;

  TestMemoryPressureMonitor() { }

  // Clears the next run loop update time so that the next pass of the run
  // loop checks the memory pressure level immediately. Normally there's a
  // 5 second delay between pressure readings.
  void ResetRunLoopUpdateTime() { next_run_loop_update_time_ = 0; }

  // Sets the last UMA stat report time. Time spent in memory pressure is
  // recorded in 5-second "ticks" from the last time statistics were recorded.
  void SetLastStatisticReportTime(CFTimeInterval time) {
    last_statistic_report_time_ = time;
  }

  // Sets the raw macOS memory pressure level read by the memory pressure
  // monitor.
  int macos_pressure_level_for_testing_;

  // Exposes the UpdatePressureLevel() method for testing.
  void UpdatePressureLevel() { MemoryPressureMonitor::UpdatePressureLevel(); }

  // Returns the number of seconds left over from the last UMA tick
  // calculation.
  int SubTickSeconds() { return subtick_seconds_; }

  // Returns the number of seconds per UMA tick.
  static int GetSecondsPerUMATick() {
    return MemoryPressureMonitor::GetSecondsPerUMATick();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestMemoryPressureMonitor);

  int GetMacMemoryPressureLevel() override {
    return macos_pressure_level_for_testing_;
  }
};

TEST(MacMemoryPressureMonitorTest, MemoryPressureFromMacMemoryPressure) {
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          DISPATCH_MEMORYPRESSURE_NORMAL));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          DISPATCH_MEMORYPRESSURE_WARN));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          DISPATCH_MEMORYPRESSURE_CRITICAL));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          0));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          3));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          5));
  EXPECT_EQ(
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
      TestMemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
          -1));
}

TEST(MacMemoryPressureMonitorTest, CurrentMemoryPressure) {
  TestMemoryPressureMonitor monitor;

  MemoryPressureListener::MemoryPressureLevel memory_pressure =
      monitor.GetCurrentPressureLevel();
  EXPECT_TRUE(memory_pressure ==
                  MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE ||
              memory_pressure ==
                  MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE ||
              memory_pressure ==
                  MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
}

TEST(MacMemoryPressureMonitorTest, MemoryPressureConversion) {
  TestMemoryPressureMonitor monitor;

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_NORMAL;
  monitor.UpdatePressureLevel();
  MemoryPressureListener::MemoryPressureLevel memory_pressure =
      monitor.GetCurrentPressureLevel();
  EXPECT_EQ(MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE,
            memory_pressure);

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_WARN;
  monitor.UpdatePressureLevel();
  memory_pressure = monitor.GetCurrentPressureLevel();
  EXPECT_EQ(MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE,
            memory_pressure);

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_CRITICAL;
  monitor.UpdatePressureLevel();
  memory_pressure = monitor.GetCurrentPressureLevel();
  EXPECT_EQ(MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL,
            memory_pressure);
}

TEST(MacMemoryPressureMonitorTest, MemoryPressureRunLoopChecking) {
  TestMemoryPressureMonitor monitor;

  // To test grabbing the memory presure at the end of the run loop, we have to
  // run the run loop, but to do that the run loop needs a run loop source. Add
  // a timer as the source. We know that the exit observer is attached to
  // the kMessageLoopExclusiveRunLoopMode mode, so use that mode.
  ScopedCFTypeRef<CFRunLoopTimerRef> timer_ref(CFRunLoopTimerCreate(
      NULL, CFAbsoluteTimeGetCurrent() + 10, 0, 0, 0, nullptr, nullptr));
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer_ref,
                    kMessageLoopExclusiveRunLoopMode);

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_WARN;
  monitor.ResetRunLoopUpdateTime();
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0, true);
  EXPECT_EQ(monitor.GetCurrentPressureLevel(),
            MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_CRITICAL;
  monitor.ResetRunLoopUpdateTime();
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0, true);
  EXPECT_EQ(monitor.GetCurrentPressureLevel(),
            MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);

  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_NORMAL;
  monitor.ResetRunLoopUpdateTime();
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0, true);
  EXPECT_EQ(monitor.GetCurrentPressureLevel(),
            MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE);

  CFRunLoopRemoveTimer(CFRunLoopGetCurrent(), timer_ref,
                       kMessageLoopExclusiveRunLoopMode);
}

TEST(MacMemoryPressureMonitorTest, RecordMemoryPressureStats) {
  TestMemoryPressureMonitor monitor;
  const char* kHistogram = "Memory.PressureLevel";
  CFTimeInterval now = CFAbsoluteTimeGetCurrent();
  const int seconds_per_tick =
      TestMemoryPressureMonitor::GetSecondsPerUMATick();

  // Set the initial pressure level.
  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_NORMAL;
  // Incur one UMA tick of time (and include one extra second of elapsed time).
  monitor.SetLastStatisticReportTime(now - (seconds_per_tick + 1));
  monitor.UpdatePressureLevel();
  monitor.tester.ExpectTotalCount(kHistogram, 1);
  monitor.tester.ExpectBucketCount(kHistogram, 0, 1);
  // The report time above included an extra second so there should be 1
  // sub-tick second left over.
  EXPECT_EQ(1, monitor.SubTickSeconds());

  // Simulate sitting in normal pressure for 1 second less than 6 UMA tick
  // seconds and then elevating to warning. With the left over sub-tick second
  // from above, the total elapsed ticks should be an even 6 UMA ticks.
  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_WARN;
  monitor.SetLastStatisticReportTime(now - (seconds_per_tick * 6 - 1));
  monitor.UpdatePressureLevel();
  monitor.tester.ExpectTotalCount(kHistogram, 7);
  monitor.tester.ExpectBucketCount(kHistogram, 0, 7);
  monitor.tester.ExpectBucketCount(kHistogram, 1, 0);
  EXPECT_EQ(0, monitor.SubTickSeconds());

  // Simulate sitting in warning pressure for 20 UMA ticks and 2 seconds, and
  // then elevating to critical.
  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_CRITICAL;
  monitor.SetLastStatisticReportTime(now - (20 * seconds_per_tick + 2));
  monitor.UpdatePressureLevel();
  monitor.tester.ExpectTotalCount(kHistogram, 27);
  monitor.tester.ExpectBucketCount(kHistogram, 0, 7);
  monitor.tester.ExpectBucketCount(kHistogram, 1, 20);
  monitor.tester.ExpectBucketCount(kHistogram, 2, 0);
  EXPECT_EQ(2, monitor.SubTickSeconds());

  // A quick update while critical - the stats should not budge because less
  // than 1 tick of time has elapsed.
  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_CRITICAL;
  monitor.SetLastStatisticReportTime(now - 1);
  monitor.UpdatePressureLevel();
  monitor.tester.ExpectTotalCount(kHistogram, 27);
  monitor.tester.ExpectBucketCount(kHistogram, 0, 7);
  monitor.tester.ExpectBucketCount(kHistogram, 1, 20);
  monitor.tester.ExpectBucketCount(kHistogram, 2, 0);
  EXPECT_EQ(3, monitor.SubTickSeconds());

  // A quick change back to normal. Less than 1 tick of time has elapsed, but
  // in this case the pressure level changed, so the critical bucket should
  // get another sample (otherwise we could miss quick level changes).
  monitor.macos_pressure_level_for_testing_ = DISPATCH_MEMORYPRESSURE_NORMAL;
  monitor.SetLastStatisticReportTime(now - 1);
  monitor.UpdatePressureLevel();
  monitor.tester.ExpectTotalCount(kHistogram, 28);
  monitor.tester.ExpectBucketCount(kHistogram, 0, 7);
  monitor.tester.ExpectBucketCount(kHistogram, 1, 20);
  monitor.tester.ExpectBucketCount(kHistogram, 2, 1);
  // When less than 1 tick of time has elapsed but the pressure level changed,
  // the subtick remainder gets zeroed out.
  EXPECT_EQ(0, monitor.SubTickSeconds());
}
}  // namespace mac
}  // namespace base
