// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_epoll_alarm_factory.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_epoll_test_tools.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/quic/platform/impl/quic_epoll_clock.h"

namespace quic {
namespace test {
namespace {

class TestDelegate : public QuicAlarm::Delegate {
 public:
  TestDelegate() : fired_(false) {}

  void OnAlarm() override { fired_ = true; }

  bool fired() const { return fired_; }

 private:
  bool fired_;
};

// The boolean parameter denotes whether or not to use an arena.
class QuicEpollAlarmFactoryTest : public QuicTestWithParam<bool> {
 protected:
  QuicEpollAlarmFactoryTest()
      : clock_(&epoll_server_), alarm_factory_(&epoll_server_) {}

  QuicConnectionArena* GetArenaParam() {
    return GetParam() ? &arena_ : nullptr;
  }

  const QuicEpollClock clock_;
  QuicEpollAlarmFactory alarm_factory_;
  QuicFakeEpollServer epoll_server_;
  QuicConnectionArena arena_;
};

INSTANTIATE_TEST_SUITE_P(UseArena,
                         QuicEpollAlarmFactoryTest,
                         ::testing::ValuesIn({true, false}),
                         ::testing::PrintToStringParamName());

TEST_P(QuicEpollAlarmFactoryTest, CreateAlarm) {
  QuicArenaScopedPtr<TestDelegate> delegate =
      QuicArenaScopedPtr<TestDelegate>(new TestDelegate());
  QuicArenaScopedPtr<QuicAlarm> alarm(
      alarm_factory_.CreateAlarm(std::move(delegate), GetArenaParam()));

  QuicTime start = clock_.Now();
  QuicTime::Delta delta = QuicTime::Delta::FromMicroseconds(1);
  alarm->Set(start + delta);

  epoll_server_.AdvanceByAndWaitForEventsAndExecuteCallbacks(
      delta.ToMicroseconds());
  EXPECT_EQ(start + delta, clock_.Now());
}

TEST_P(QuicEpollAlarmFactoryTest, CreateAlarmAndCancel) {
  QuicArenaScopedPtr<TestDelegate> delegate =
      QuicArenaScopedPtr<TestDelegate>(new TestDelegate());
  TestDelegate* unowned_delegate = delegate.get();
  QuicArenaScopedPtr<QuicAlarm> alarm(
      alarm_factory_.CreateAlarm(std::move(delegate), GetArenaParam()));

  QuicTime start = clock_.Now();
  QuicTime::Delta delta = QuicTime::Delta::FromMicroseconds(1);
  alarm->Set(start + delta);
  alarm->Cancel();

  epoll_server_.AdvanceByExactlyAndCallCallbacks(delta.ToMicroseconds());
  EXPECT_EQ(start + delta, clock_.Now());
  EXPECT_FALSE(unowned_delegate->fired());
}

TEST_P(QuicEpollAlarmFactoryTest, CreateAlarmAndReset) {
  QuicArenaScopedPtr<TestDelegate> delegate =
      QuicArenaScopedPtr<TestDelegate>(new TestDelegate());
  TestDelegate* unowned_delegate = delegate.get();
  QuicArenaScopedPtr<QuicAlarm> alarm(
      alarm_factory_.CreateAlarm(std::move(delegate), GetArenaParam()));

  QuicTime start = clock_.Now();
  QuicTime::Delta delta = QuicTime::Delta::FromMicroseconds(1);
  alarm->Set(clock_.Now() + delta);
  alarm->Cancel();
  QuicTime::Delta new_delta = QuicTime::Delta::FromMicroseconds(3);
  alarm->Set(clock_.Now() + new_delta);

  epoll_server_.AdvanceByExactlyAndCallCallbacks(delta.ToMicroseconds());
  EXPECT_EQ(start + delta, clock_.Now());
  EXPECT_FALSE(unowned_delegate->fired());

  epoll_server_.AdvanceByExactlyAndCallCallbacks(
      (new_delta - delta).ToMicroseconds());
  EXPECT_EQ(start + new_delta, clock_.Now());
  EXPECT_TRUE(unowned_delegate->fired());
}

TEST_P(QuicEpollAlarmFactoryTest, CreateAlarmAndUpdate) {
  QuicArenaScopedPtr<TestDelegate> delegate =
      QuicArenaScopedPtr<TestDelegate>(new TestDelegate());
  TestDelegate* unowned_delegate = delegate.get();
  QuicArenaScopedPtr<QuicAlarm> alarm(
      alarm_factory_.CreateAlarm(std::move(delegate), GetArenaParam()));

  QuicTime start = clock_.Now();
  QuicTime::Delta delta = QuicTime::Delta::FromMicroseconds(1);
  alarm->Set(clock_.Now() + delta);
  QuicTime::Delta new_delta = QuicTime::Delta::FromMicroseconds(3);
  alarm->Update(clock_.Now() + new_delta, QuicTime::Delta::FromMicroseconds(1));

  epoll_server_.AdvanceByExactlyAndCallCallbacks(delta.ToMicroseconds());
  EXPECT_EQ(start + delta, clock_.Now());
  EXPECT_FALSE(unowned_delegate->fired());

  // Move the alarm forward 1us and ensure it doesn't move forward.
  alarm->Update(clock_.Now() + new_delta, QuicTime::Delta::FromMicroseconds(2));

  epoll_server_.AdvanceByExactlyAndCallCallbacks(
      (new_delta - delta).ToMicroseconds());
  EXPECT_EQ(start + new_delta, clock_.Now());
  EXPECT_TRUE(unowned_delegate->fired());

  // Set the alarm via an update call.
  new_delta = QuicTime::Delta::FromMicroseconds(5);
  alarm->Update(clock_.Now() + new_delta, QuicTime::Delta::FromMicroseconds(1));
  EXPECT_TRUE(alarm->IsSet());

  // Update it with an uninitialized time and ensure it's cancelled.
  alarm->Update(QuicTime::Zero(), QuicTime::Delta::FromMicroseconds(1));
  EXPECT_FALSE(alarm->IsSet());
}

}  // namespace
}  // namespace test
}  // namespace quic
