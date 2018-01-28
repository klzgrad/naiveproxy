// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_alarm.h"
#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/test_tools/simulator/alarm_factory.h"

using std::string;

namespace net {
namespace simulator {

// Alarm is an implementation of QuicAlarm which can schedule alarms in the
// simulation timeline.
class Alarm : public QuicAlarm {
 public:
  Alarm(Simulator* simulator,
        string name,
        QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
      : QuicAlarm(std::move(delegate)), adapter_(simulator, name, this) {}
  ~Alarm() override {}

  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    adapter_.Set(deadline());
  }

  void CancelImpl() override { adapter_.Cancel(); }

 private:
  // An adapter class triggering a QuicAlarm using a simulation time system.
  // An adapter is required here because neither Actor nor QuicAlarm are pure
  // interfaces.
  class Adapter : public Actor {
   public:
    Adapter(Simulator* simulator, string name, Alarm* parent)
        : Actor(simulator, name), parent_(parent) {}
    ~Adapter() override {}

    void Set(QuicTime time) { Schedule(time); }
    void Cancel() { Unschedule(); }

    void Act() override {
      DCHECK(clock_->Now() == parent_->deadline());
      parent_->Fire();
    }

   private:
    Alarm* parent_;
  };
  Adapter adapter_;
};

AlarmFactory::AlarmFactory(Simulator* simulator, string name)
    : simulator_(simulator), name_(std::move(name)), counter_(0) {}

AlarmFactory::~AlarmFactory() {}

string AlarmFactory::GetNewAlarmName() {
  ++counter_;
  return QuicStringPrintf("%s (alarm %i)", name_.c_str(), counter_);
}

QuicAlarm* AlarmFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new Alarm(simulator_, GetNewAlarmName(),
                   QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> AlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<Alarm>(simulator_, GetNewAlarmName(),
                             std::move(delegate));
  }
  return QuicArenaScopedPtr<QuicAlarm>(
      new Alarm(simulator_, GetNewAlarmName(), std::move(delegate)));
}

}  // namespace simulator
}  // namespace net
