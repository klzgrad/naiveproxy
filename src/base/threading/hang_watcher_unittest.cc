// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/hang_watcher.h"
#include <memory>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/bind_test_util.h"
#include "base/test/task_environment.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

const base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(10);
const base::TimeDelta kHangTime = kTimeout + base::TimeDelta::FromSeconds(1);

// Waits on provided WaitableEvent before executing and signals when done.
class BlockingThread : public PlatformThread::Delegate {
 public:
  explicit BlockingThread(base::WaitableEvent* unblock_thread)
      : unblock_thread_(unblock_thread) {}

  void ThreadMain() override {
    // (Un)Register the thread here instead of in ctor/dtor so that the action
    // happens on the right thread.
    base::ScopedClosureRunner unregister_closure =
        base::HangWatcher::GetInstance()->RegisterThread();

    HangWatchScope scope(kTimeout);
    wait_until_entered_scope_.Signal();

    unblock_thread_->Wait();
    run_event_.Signal();
  }

  bool IsDone() { return run_event_.IsSignaled(); }

  // Block until this thread registered itself for hang watching and has entered
  // a HangWatchScope.
  void WaitUntilScopeEntered() { wait_until_entered_scope_.Wait(); }

 private:
  // Will be signaled once the thread is properly registered for watching and
  // scope has been entered.
  WaitableEvent wait_until_entered_scope_;

  // Will be signaled once ThreadMain has run.
  WaitableEvent run_event_;

  base::WaitableEvent* const unblock_thread_;
};

class HangWatcherTest : public testing::Test {
 public:
  HangWatcherTest()
      : hang_watcher_(std::make_unique<HangWatcher>(
            base::BindRepeating(&WaitableEvent::Signal,
                                base::Unretained(&hang_event_)))),
        thread_(&unblock_thread_) {
    hang_watcher_->SetAfterMonitorClosureForTesting(base::BindRepeating(
        &WaitableEvent::Signal, base::Unretained(&monitor_event_)));
  }

  void SetUp() override {
    // We're not testing the monitoring loop behavior in this test so we want to
    // trigger monitoring manually.
    hang_watcher_->SetMonitoringPeriodForTesting(base::TimeDelta::Max());
  }

  void StartBlockedThread() {
    // Thread has not run yet.
    ASSERT_FALSE(thread_.IsDone());

    // Start the thread. It will block since |unblock_thread_| was not
    // signaled yet.
    ASSERT_TRUE(PlatformThread::Create(0, &thread_, &handle));

    thread_.WaitUntilScopeEntered();

    // Thread registration triggered a call to HangWatcher::Monitor() which
    // signaled |monitor_event_|. Reset it so it's ready for waiting later on.
    monitor_event_.Reset();
  }

  void MonitorHangsAndJoinThread() {
    // HangWatcher::Monitor() should not be set which would mean a call to
    // HangWatcher::Monitor() happened and was unacounted for.
    ASSERT_FALSE(monitor_event_.IsSignaled());

    // Triger a monitoring on HangWatcher thread and verify results.
    hang_watcher_->SignalMonitorEventForTesting();
    monitor_event_.Wait();

    unblock_thread_.Signal();

    // Thread is joinable since we signaled |unblock_thread_|.
    PlatformThread::Join(handle);

    // If thread is done then it signaled.
    ASSERT_TRUE(thread_.IsDone());
  }

 protected:
  // Used to wait for monitoring. Will be signaled by the HangWatcher thread and
  // so needs to outlive it.
  WaitableEvent monitor_event_;

  // Signaled from the HangWatcher thread when a hang is detected. Needs to
  // outlive the HangWatcher thread.
  WaitableEvent hang_event_;

  std::unique_ptr<HangWatcher> hang_watcher_;

  // Used exclusively for MOCK_TIME. No tasks will be run on the environment.
  // Single threaded to avoid ThreadPool WorkerThreads registering.
  test::SingleThreadTaskEnvironment task_environment_{
      test::TaskEnvironment::TimeSource::MOCK_TIME};

  // Used to unblock the monitored thread. Signaled from the test main thread.
  WaitableEvent unblock_thread_;

  PlatformThreadHandle handle;
  BlockingThread thread_;

};
}  // namespace

TEST_F(HangWatcherTest, NoRegisteredThreads) {
  ASSERT_FALSE(monitor_event_.IsSignaled());

  // Signal to advance the Run() loop.
  base::HangWatcher::GetInstance()->SignalMonitorEventForTesting();

  // Monitoring should just not happen when there are no registered threads.
  // Wait a while to make sure it does not.
  ASSERT_FALSE(monitor_event_.TimedWait(base::TimeDelta::FromSeconds(1)));

  ASSERT_FALSE(hang_event_.IsSignaled());
}

TEST_F(HangWatcherTest, NestedScopes) {
  // Create a state object for the test thread since this test is single
  // threaded.
  auto current_hang_watch_state =
      base::internal::HangWatchState::CreateHangWatchStateForCurrentThread();

  ASSERT_FALSE(current_hang_watch_state->IsOverDeadline());
  base::TimeTicks original_deadline = current_hang_watch_state->GetDeadline();

  constexpr base::TimeDelta kFirstTimeout(
      base::TimeDelta::FromMilliseconds(500));
  base::TimeTicks first_deadline = base::TimeTicks::Now() + kFirstTimeout;

  constexpr base::TimeDelta kSecondTimeout(
      base::TimeDelta::FromMilliseconds(250));
  base::TimeTicks second_deadline = base::TimeTicks::Now() + kSecondTimeout;

  // At this point we have not set any timeouts.
  {
    // Create a first timeout which is more restrictive than the default.
    HangWatchScope first_scope(kFirstTimeout);

    // We are on mock time. There is no time advancement and as such no hangs.
    ASSERT_FALSE(current_hang_watch_state->IsOverDeadline());
    ASSERT_EQ(current_hang_watch_state->GetDeadline(), first_deadline);
    {
      // Set a yet more restrictive deadline. Still no hang.
      HangWatchScope second_scope(kSecondTimeout);
      ASSERT_FALSE(current_hang_watch_state->IsOverDeadline());
      ASSERT_EQ(current_hang_watch_state->GetDeadline(), second_deadline);
    }
    // First deadline we set should be restored.
    ASSERT_FALSE(current_hang_watch_state->IsOverDeadline());
    ASSERT_EQ(current_hang_watch_state->GetDeadline(), first_deadline);
  }

  // Original deadline should now be restored.
  ASSERT_FALSE(current_hang_watch_state->IsOverDeadline());
  ASSERT_EQ(current_hang_watch_state->GetDeadline(), original_deadline);
}

TEST_F(HangWatcherTest, Hang) {
  StartBlockedThread();

  // Simulate hang.
  task_environment_.FastForwardBy(kHangTime);

  MonitorHangsAndJoinThread();
  ASSERT_TRUE(hang_event_.IsSignaled());
}

TEST_F(HangWatcherTest, NoHang) {
  StartBlockedThread();

  MonitorHangsAndJoinThread();
  ASSERT_FALSE(hang_event_.IsSignaled());
}

// |HangWatcher| relies on |WaitableEvent::TimedWait| to schedule monitoring
// which cannot be tested using MockTime. Some tests will have to actually wait
// in real time before observing results but the TimeDeltas used are chosen to
// minimize flakiness as much as possible.
class HangWatcherRealTimeTest : public testing::Test {
 public:
  HangWatcherRealTimeTest()
      : hang_watcher_(std::make_unique<HangWatcher>(
            base::BindRepeating(&WaitableEvent::Signal,
                                base::Unretained(&hang_event_)))) {}

 protected:
  std::unique_ptr<HangWatcher> hang_watcher_;

  // Signaled when a hang is detected.
  WaitableEvent hang_event_;

  std::atomic<int> monitor_count_{0};

  base::ScopedClosureRunner unregister_thread_closure_;
};

// TODO(https://crbug.com/1064116): Fix this test not to rely on timely task
// execution, which results in flakiness on slower bots.
TEST_F(HangWatcherRealTimeTest, DISABLED_PeriodicCallsCount) {
  // These values are chosen to execute fast enough while running the unit tests
  // but be large enough to buffer against clock precision problems.
  const base::TimeDelta kMonitoringPeriod(
      base::TimeDelta::FromMilliseconds(100));
  const base::TimeDelta kExecutionTime = kMonitoringPeriod * 5;

  // HangWatcher::Monitor() will run once right away on thread registration.
  // We want to make sure it runs at least once more from being scheduled.
  constexpr int kMinimumMonitorCount = 2;

  // Some amount of extra monitoring can happen but it has to be of the right
  // order of magnitude. Otherwise it could indicate a problem like some code
  // signaling the Thread to wake up excessivelly.
  const int kMaximumMonitorCount = 2 * (kExecutionTime / kMonitoringPeriod);

  auto increment_monitor_count = [this]() { ++monitor_count_; };

  hang_watcher_->SetMonitoringPeriodForTesting(kMonitoringPeriod);
  hang_watcher_->SetAfterMonitorClosureForTesting(
      base::BindLambdaForTesting(increment_monitor_count));

  hang_event_.TimedWait(kExecutionTime);

  // No thread ever registered so no monitoring took place at all.
  ASSERT_EQ(monitor_count_.load(), 0);

  unregister_thread_closure_ = hang_watcher_->RegisterThread();

  hang_event_.TimedWait(kExecutionTime);

  ASSERT_GE(monitor_count_.load(), kMinimumMonitorCount);
  ASSERT_LE(monitor_count_.load(), kMaximumMonitorCount);

  // No monitored scope means no possible hangs.
  ASSERT_FALSE(hang_event_.IsSignaled());
}

class HangWatchScopeBlockingTest : public testing::Test {
 public:
  void SetUp() override {
    // Start the HangWatcher.
    hang_watcher_ =
        std::make_unique<HangWatcher>(base::BindLambdaForTesting([&] {
          capture_started_.Signal();
          // Simulate capturing that takes a long time.
          PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
          completed_capture_ = true;
        }));

    hang_watcher_->SetAfterMonitorClosureForTesting(
        base::BindLambdaForTesting([&]() {
          // Simulate monitoring that takes a long time.
          PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
          completed_monitoring_.Signal();
        }));

    // Make sure no periodic monitoring takes place.
    hang_watcher_->SetMonitoringPeriodForTesting(base::TimeDelta::Max());

    // Register the test main thread for hang watching.
    unregister_thread_closure_ = hang_watcher_->RegisterThread();
  }

  void VerifyScopesDontBlock() {
    // Start a hang watch scope that cannot possibly cause a hang to be
    // detected.
    {
      HangWatchScope long_scope(base::TimeDelta::Max());

      // Manually trigger a monitoring.
      hang_watcher_->SignalMonitorEventForTesting();

      // Execution has to continue freely here as no capture is in progress.
    }

    // Monitoring should not be over yet because the test code should execute
    // faster when not blocked.
    EXPECT_FALSE(completed_monitoring_.IsSignaled());

    // Wait for the full monitoring process to be complete. This is to prove
    // that monitoring truly executed and that we raced the signaling.
    completed_monitoring_.Wait();

    // No hang means no capture.
    EXPECT_FALSE(completed_capture_);
  }

 protected:
  base::WaitableEvent capture_started_;
  base::WaitableEvent completed_monitoring_;

  // No need for this to be atomic because in tests with no capture the variable
  // is not even written to by the HangWatcher thread and in tests with a
  // capture the accesses are serialized by the blocking in ~HangWatchScope().
  bool completed_capture_ = false;

  std::unique_ptr<HangWatcher> hang_watcher_;
  base::ScopedClosureRunner unregister_thread_closure_;
};

// Tests that execution is unimpeded by ~HangWatchScope() when no capture ever
// takes place.
TEST_F(HangWatchScopeBlockingTest, ScopeDoesNotBlocksWithoutCapture) {
  VerifyScopesDontBlock();
}

// Test that execution blocks in ~HangWatchScope() for a thread under watch
// during the capturing of a hang.
TEST_F(HangWatchScopeBlockingTest, ScopeBlocksDuringCapture) {
  // Start a hang watch scope that expires in the past already. Ensures that the
  // first monitor will detect a hang.
  {
    HangWatchScope already_over(base::TimeDelta::FromDays(-1));

    // Manually trigger a monitoring.
    hang_watcher_->SignalMonitorEventForTesting();

    // Ensure that the hang capturing started.
    capture_started_.Wait();

    // Execution will get stuck in this scope because execution does not escape
    // ~HangWatchScope() if a hang capture is under way.
  }

  // A hang was in progress so execution should have been blocked in
  // BlockWhileCaptureInProgress() until capture finishes.
  EXPECT_TRUE(completed_capture_);

  // Reset expectations
  completed_monitoring_.Reset();
  capture_started_.Reset();
  completed_capture_ = false;

  // Verify that scopes don't block just because a capture happened in the past.
  VerifyScopesDontBlock();
}

}  // namespace base
