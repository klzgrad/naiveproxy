// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_

#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/actor.h"

namespace quic {
namespace simulator {

// AlarmFactory allows to schedule QuicAlarms using the simulation event queue.
class AlarmFactory : public QuicAlarmFactory {
 public:
  AlarmFactory(Simulator* simulator, std::string name);
  AlarmFactory(const AlarmFactory&) = delete;
  AlarmFactory& operator=(const AlarmFactory&) = delete;
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
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMULATOR_ALARM_FACTORY_H_
