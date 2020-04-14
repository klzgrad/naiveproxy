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
        thread_(&unblock_thread_) {}

  void StartBlockedThread() {
    // Thread has not run yet.
    ASSERT_FALSE(thread_.IsDone());

    // Start the thread. It will block since |unblock_thread_| was not
    // signaled yet.
    ASSERT_TRUE(PlatformThread::Create(0, &thread_, &handle));

    thread_.WaitUntilScopeEntered();
  }

  void MonitorHangsAndJoinThread() {
    // Try to detect a hang if any.
    HangWatcher::GetInstance()->Monitor();

    unblock_thread_.Signal();

    // Thread is joinable since we signaled |unblock_thread_|.
    PlatformThread::Join(handle);

    // If thread is done then it signaled.
    ASSERT_TRUE(thread_.IsDone());
  }

 protected:
  std::unique_ptr<HangWatcher> hang_watcher_;

  // Used exclusively for MOCK_TIME. No tasks will be run on the environment.
  test::TaskEnvironment task_environment_{
      test::TaskEnvironment::TimeSource::MOCK_TIME};

  PlatformThreadHandle handle;

  WaitableEvent unblock_thread_;

  BlockingThread thread_;

  // Signaled when a hang is detected.
  WaitableEvent hang_event_;
};
}  // namespace

TEST_F(HangWatcherTest, NoScopes) {
  HangWatcher::GetInstance()->Monitor();
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

}  // namespace base
