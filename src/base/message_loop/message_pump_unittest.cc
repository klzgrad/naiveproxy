// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump.h"

#include <type_traits>

#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/test/bind_test_util.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
#include "base/message_loop/message_pump_libevent.h"
#endif

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::Return;

namespace base {

namespace {

bool PumpTypeUsesDoSomeWork(MessageLoop::Type type) {
  switch (type) {
    case MessagePump::Type::DEFAULT:
#if defined(OS_IOS)
      // iOS uses a MessagePumpCFRunLoop instead of MessagePumpDefault for
      // TYPE_DEFAULT. TODO(gab): migrate MessagePumpCFRunLoop too.
      return false;
#else
      return true;
#endif

    case MessagePump::Type::UI:
#if defined(OS_IOS)
      // iOS uses a MessagePumpDefault for UI in unit tests, ref.
      // test_support_ios.mm::CreateMessagePumpForUIForTests().
      return true;
#elif defined(OS_WIN) || defined(OS_ANDROID) || defined(USE_GLIB)
      return true;
#elif defined(OS_POSIX) && !defined(OS_NACL_SFI)
      // MessagePumpLibevent was migrated (ref. message_pump_for_ui.h and
      // |use_libevent| in base/BUILD.gn for enabled conditions).
      return std::is_same<MessagePumpForUI, MessagePumpLibevent>::value;
#else
      // TODO(gab): Complete migration of all UI pumps to DoSomeWork() as part
      // of crbug.com/885371.
      return false;
#endif

    case MessagePump::Type::IO:
#if defined(OS_WIN) || (defined(OS_MACOSX) && !defined(OS_IOS))
      return true;
#elif defined(OS_POSIX) && !defined(OS_NACL_SFI)
      // MessagePumpLibevent was migrated (ref. message_pump_for_io.h and
      // |use_libevent| in base/BUILD.gn for enabled conditions).
      return std::is_same<MessagePumpForIO, MessagePumpLibevent>::value;
#else
      // TODO(gab): Complete migration of all IO pumps to DoSomeWork() as part
      // of crbug.com/885371.
      return false;
#endif

    case MessagePump::Type::CUSTOM:
#if defined(OS_ANDROID)
    case MessagePump::Type::JAVA:
#endif  // defined(OS_ANDROID)
#if defined(OS_MACOSX)
    case MessagePump::Type::NS_RUNLOOP:
#endif  // defined(OS_MACOSX)
      // Not tested in this file.
      NOTREACHED();
      return false;
  }
  NOTREACHED();
  return false;
}

class MockMessagePumpDelegate : public MessagePump::Delegate {
 public:
  MockMessagePumpDelegate() = default;

  // MessagePump::Delegate:
  void BeforeDoInternalWork() {}
  MOCK_METHOD0(DoSomeWork, MessagePump::Delegate::NextWorkInfo());
  MOCK_METHOD0(DoWork, bool());
  MOCK_METHOD1(DoDelayedWork, bool(TimeTicks*));
  MOCK_METHOD0(DoIdleWork, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockMessagePumpDelegate);
};

class MessagePumpTest : public ::testing::TestWithParam<MessageLoop::Type> {
 public:
  MessagePumpTest() : message_pump_(MessagePump::Create(GetParam())) {}

 protected:
  const bool pump_uses_do_some_work_ = PumpTypeUsesDoSomeWork(GetParam());

  std::unique_ptr<MessagePump> message_pump_;
};

}  // namespace

TEST_P(MessagePumpTest, QuitStopsWork) {
  testing::StrictMock<MockMessagePumpDelegate> delegate;

  // Not expecting any calls to DoDelayedWork or DoIdleWork after quitting.
  if (pump_uses_do_some_work_) {
    EXPECT_CALL(delegate, DoSomeWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
  } else {
    EXPECT_CALL(delegate, DoWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return false;
    }));
  }
  EXPECT_CALL(delegate, DoDelayedWork(_)).Times(0);
  EXPECT_CALL(delegate, DoIdleWork()).Times(0);

  message_pump_->ScheduleWork();
  message_pump_->Run(&delegate);
}

TEST_P(MessagePumpTest, QuitStopsWorkWithNestedRunLoop) {
  testing::InSequence sequence;
  testing::StrictMock<MockMessagePumpDelegate> delegate;
  testing::StrictMock<MockMessagePumpDelegate> nested_delegate;

  // We first schedule a call to DoWork, which runs a nested run loop. After the
  // nested loop exits, we schedule another DoWork which quits the outer
  // (original) run loop. The test verifies that there are no extra calls to
  // DoWork after the outer loop quits.
  if (pump_uses_do_some_work_) {
    EXPECT_CALL(delegate, DoSomeWork).WillOnce(Invoke([&] {
      message_pump_->ScheduleWork();
      message_pump_->Run(&nested_delegate);
      message_pump_->ScheduleWork();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
    EXPECT_CALL(nested_delegate, DoSomeWork).WillOnce(Invoke([&] {
      // Quit the nested run loop.
      message_pump_->Quit();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
  } else {
    EXPECT_CALL(delegate, DoWork).WillOnce(Invoke([&] {
      message_pump_->ScheduleWork();
      message_pump_->Run(&nested_delegate);
      message_pump_->ScheduleWork();
      return false;
    }));
    EXPECT_CALL(nested_delegate, DoWork).WillOnce(Invoke([&] {
      // Quit the nested run loop.
      message_pump_->Quit();
      return false;
    }));
    EXPECT_CALL(delegate, DoDelayedWork(_)).WillOnce(Return(false));
  }

  // The outer pump may or may not trigger idle work at this point.
  EXPECT_CALL(delegate, DoIdleWork()).Times(AnyNumber());

  if (pump_uses_do_some_work_) {
    EXPECT_CALL(delegate, DoSomeWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
  } else {
    EXPECT_CALL(delegate, DoWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return false;
    }));
  }

  message_pump_->ScheduleWork();
  message_pump_->Run(&delegate);
}

namespace {

class TimerSlackTestDelegate : public MessagePump::Delegate {
 public:
  TimerSlackTestDelegate(MessagePump* message_pump)
      : message_pump_(message_pump) {
    // We first schedule a delayed task far in the future with maximum timer
    // slack.
    message_pump_->SetTimerSlack(TIMER_SLACK_MAXIMUM);
    message_pump_->ScheduleDelayedWork(TimeTicks::Now() +
                                       TimeDelta::FromHours(1));

    // Since we have no other work pending, the pump will initially be idle.
    action_.store(NONE);
  }

  void BeforeDoInternalWork() override {}

  MessagePump::Delegate::NextWorkInfo DoSomeWork() override {
    switch (action_.load()) {
      case NONE:
        break;
      case SCHEDULE_DELAYED_WORK: {
        // After being woken up by the other thread, we let the pump know that
        // the next delayed task is in fact much sooner than the 1 hour delay it
        // was aware of. If the pump refreshes its timer correctly, it will wake
        // up shortly, finishing the test.
        action_.store(QUIT);
        TimeTicks now = TimeTicks::Now();
        return {now + TimeDelta::FromMilliseconds(50), now};
      }
      case QUIT:
        message_pump_->Quit();
        break;
    }
    return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
  }

  bool DoWork() override {
    switch (action_.load()) {
      case NONE:
        break;
      case SCHEDULE_DELAYED_WORK:
        // After being woken up by the other thread, we schedule work after a
        // short delay. If the pump refreshes its timer correctly, it will wake
        // up shortly, finishing the test.
        action_.store(QUIT);
        message_pump_->ScheduleDelayedWork(TimeTicks::Now() +
                                           TimeDelta::FromMilliseconds(50));
        break;
      case QUIT:
        message_pump_->Quit();
        break;
    }
    return false;
  }
  bool DoDelayedWork(base::TimeTicks*) override { return false; }
  bool DoIdleWork() override { return false; }

  void WakeUpFromOtherThread() {
    action_.store(SCHEDULE_DELAYED_WORK);
    message_pump_->ScheduleWork();
  }

 private:
  enum Action {
    NONE,
    SCHEDULE_DELAYED_WORK,
    QUIT,
  };

  MessagePump* const message_pump_;
  std::atomic<Action> action_;
};

}  // namespace

TEST_P(MessagePumpTest, TimerSlackWithLongDelays) {
  // This is a regression test for an issue where the iOS message pump fails to
  // run delayed work when timer slack is enabled. The steps needed to trigger
  // this are:
  //
  //  1. The message pump timer slack is set to maximum.
  //  2. A delayed task is posted for far in the future (e.g., 1h).
  //  3. The system goes idle at least for a few seconds.
  //  4. Another delayed task is posted with a much smaller delay.
  //
  // The following message pump test delegate automatically runs through this
  // sequence.
  TimerSlackTestDelegate delegate(message_pump_.get());

  // We use another thread to wake up the pump after 2 seconds to allow the
  // system to enter an idle state. This delay was determined experimentally on
  // the iPhone 6S simulator.
  Thread thread("Waking thread");
  thread.StartAndWaitForTesting();
  thread.task_runner()->PostDelayedTask(
      FROM_HERE,
      BindLambdaForTesting([&delegate] { delegate.WakeUpFromOtherThread(); }),
      TimeDelta::FromSeconds(2));

  message_pump_->Run(&delegate);
}

TEST_P(MessagePumpTest, RunWithoutScheduleWorkInvokesDoWork) {
  testing::StrictMock<MockMessagePumpDelegate> delegate;
#if defined(OS_IOS)
  EXPECT_CALL(delegate, DoIdleWork).Times(AnyNumber());
#endif
  if (pump_uses_do_some_work_) {
    EXPECT_CALL(delegate, DoSomeWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
  } else {
    EXPECT_CALL(delegate, DoWork).WillOnce(Invoke([this] {
      message_pump_->Quit();
      return false;
    }));
  }
  message_pump_->Run(&delegate);
}

TEST_P(MessagePumpTest, NestedRunWithoutScheduleWorkInvokesDoWork) {
  testing::StrictMock<MockMessagePumpDelegate> delegate;
#if defined(OS_IOS)
  EXPECT_CALL(delegate, DoIdleWork).Times(AnyNumber());
#endif
  if (pump_uses_do_some_work_) {
    EXPECT_CALL(delegate, DoSomeWork).WillOnce(Invoke([this] {
      testing::StrictMock<MockMessagePumpDelegate> nested_delegate;
#if defined(OS_IOS)
      EXPECT_CALL(nested_delegate, DoIdleWork).Times(AnyNumber());
#endif
      EXPECT_CALL(nested_delegate, DoSomeWork).WillOnce(Invoke([this] {
        message_pump_->Quit();
        return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
      }));
      message_pump_->Run(&nested_delegate);
      message_pump_->Quit();
      return MessagePump::Delegate::NextWorkInfo{TimeTicks::Max()};
    }));
  } else {
    EXPECT_CALL(delegate, DoWork).WillOnce(Invoke([this] {
      testing::StrictMock<MockMessagePumpDelegate> nested_delegate;
#if defined(OS_IOS)
      EXPECT_CALL(nested_delegate, DoIdleWork).Times(AnyNumber());
#endif
      EXPECT_CALL(nested_delegate, DoWork).WillOnce(Invoke([this] {
        message_pump_->Quit();
        return false;
      }));
      message_pump_->Run(&nested_delegate);
      message_pump_->Quit();
      return false;
    }));
  }
  message_pump_->Run(&delegate);
}

INSTANTIATE_TEST_SUITE_P(,
                         MessagePumpTest,
                         ::testing::Values(MessageLoop::TYPE_DEFAULT,
                                           MessageLoop::TYPE_UI,
                                           MessageLoop::TYPE_IO));

}  // namespace base
