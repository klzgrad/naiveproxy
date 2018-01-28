// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_
#define NET_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_

#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/test_tools/simulator/actor.h"

namespace net {
namespace simulator {

// AlarmFactory allows to schedule QuicAlarms using the simulation event queue.
class AlarmFactory : public QuicAlarmFactory {
 public:
  AlarmFactory(Simulator* simulator, std::string name);
  ~AlarmFactory() override;

  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  // Automatically generate a name for a new alarm.
  std::string GetNewAlarmName();

  Simulator* simulator_;
  std::string name_;
  int counter_;

  DISALLOW_COPY_AND_ASSIGN(AlarmFactory);
};

}  // namespace simulator
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_
