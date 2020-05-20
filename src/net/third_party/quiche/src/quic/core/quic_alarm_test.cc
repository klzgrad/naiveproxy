// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_alarm.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

using testing::Invoke;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QuicAlarm::Delegate {
 public:
  MOCK_METHOD0(OnAlarm, void());
};

class DestructiveDelegate : public QuicAlarm::Delegate {
 public:
  DestructiveDelegate() : alarm_(nullptr) {}

  void set_alarm(QuicAlarm* alarm) { alarm_ = alarm; }

  void OnAlarm() override {
    DCHECK(alarm_);
    delete alarm_;
  }

 private:
  QuicAlarm* alarm_;
};

class TestAlarm : public QuicAlarm {
 public:
  explicit TestAlarm(QuicAlarm::Delegate* delegate)
      : QuicAlarm(QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate)) {}

  bool scheduled() const { return scheduled_; }

  void FireAlarm() {
    scheduled_ = false;
    Fire();
  }

 protected:
  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    scheduled_ = true;
  }

  void CancelImpl() override {
    DCHECK(!deadline().IsInitialized());
    scheduled_ = false;
  }

 private:
  bool scheduled_;
};

class DestructiveAlarm : public QuicAlarm {
 public:
  explicit DestructiveAlarm(DestructiveDelegate* delegate)
      : QuicAlarm(QuicArenaScopedPtr<DestructiveDelegate>(delegate)) {}

  void FireAlarm() { Fire(); }

 protected:
  void SetImpl() override {}

  void CancelImpl() override {}
};

class QuicAlarmTest : public QuicTest {
 public:
  QuicAlarmTest()
      : delegate_(new MockDelegate()),
        alarm_(delegate_),
        deadline_(QuicTime::Zero() + QuicTime::Delta::FromSeconds(7)),
        deadline2_(QuicTime::Zero() + QuicTime::Delta::FromSeconds(14)),
        new_deadline_(QuicTime::Zero()) {}

  void ResetAlarm() { alarm_.Set(new_deadline_); }

  MockDelegate* delegate_;  // not owned
  TestAlarm alarm_;
  QuicTime deadline_;
  QuicTime deadline2_;
  QuicTime new_deadline_;
};

TEST_F(QuicAlarmTest, IsSet) {
  EXPECT_FALSE(alarm_.IsSet());
}

TEST_F(QuicAlarmTest, Set) {
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm_.Set(deadline);
  EXPECT_TRUE(alarm_.IsSet());
  EXPECT_TRUE(alarm_.scheduled());
  EXPECT_EQ(deadline, alarm_.deadline());
}

TEST_F(QuicAlarmTest, Cancel) {
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm_.Set(deadline);
  alarm_.Cancel();
  EXPECT_FALSE(alarm_.IsSet());
  EXPECT_FALSE(alarm_.scheduled());
  EXPECT_EQ(QuicTime::Zero(), alarm_.deadline());
}

TEST_F(QuicAlarmTest, Update) {
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm_.Set(deadline);
  QuicTime new_deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(8);
  alarm_.Update(new_deadline, QuicTime::Delta::Zero());
  EXPECT_TRUE(alarm_.IsSet());
  EXPECT_TRUE(alarm_.scheduled());
  EXPECT_EQ(new_deadline, alarm_.deadline());
}

TEST_F(QuicAlarmTest, UpdateWithZero) {
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm_.Set(deadline);
  alarm_.Update(QuicTime::Zero(), QuicTime::Delta::Zero());
  EXPECT_FALSE(alarm_.IsSet());
  EXPECT_FALSE(alarm_.scheduled());
  EXPECT_EQ(QuicTime::Zero(), alarm_.deadline());
}

TEST_F(QuicAlarmTest, Fire) {
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm_.Set(deadline);
  EXPECT_CALL(*delegate_, OnAlarm());
  alarm_.FireAlarm();
  EXPECT_FALSE(alarm_.IsSet());
  EXPECT_FALSE(alarm_.scheduled());
  EXPECT_EQ(QuicTime::Zero(), alarm_.deadline());
}

TEST_F(QuicAlarmTest, FireAndResetViaSet) {
  alarm_.Set(deadline_);
  new_deadline_ = deadline2_;
  EXPECT_CALL(*delegate_, OnAlarm())
      .WillOnce(Invoke(this, &QuicAlarmTest::ResetAlarm));
  alarm_.FireAlarm();
  EXPECT_TRUE(alarm_.IsSet());
  EXPECT_TRUE(alarm_.scheduled());
  EXPECT_EQ(deadline2_, alarm_.deadline());
}

TEST_F(QuicAlarmTest, FireDestroysAlarm) {
  DestructiveDelegate* delegate(new DestructiveDelegate);
  DestructiveAlarm* alarm = new DestructiveAlarm(delegate);
  delegate->set_alarm(alarm);
  QuicTime deadline = QuicTime::Zero() + QuicTime::Delta::FromSeconds(7);
  alarm->Set(deadline);
  // This should not crash, even though it will destroy alarm.
  alarm->FireAlarm();
}

}  // namespace
}  // namespace test
}  // namespace quic
