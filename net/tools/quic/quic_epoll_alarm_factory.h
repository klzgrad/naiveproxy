// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_EPOLL_ALARM_FACTORY_H_
#define NET_TOOLS_QUIC_QUIC_EPOLL_ALARM_FACTORY_H_

#include "net/quic/core/quic_alarm.h"
#include "net/quic/core/quic_alarm_factory.h"

namespace net {

class EpollServer;

// Creates alarms that use the supplied EpollServer for timing and firing.
class QuicEpollAlarmFactory : public QuicAlarmFactory {
 public:
  explicit QuicEpollAlarmFactory(EpollServer* epoll_server);
  ~QuicEpollAlarmFactory() override;

  // QuicAlarmFactory interface.
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  EpollServer* epoll_server_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(QuicEpollAlarmFactory);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_EPOLL_ALARM_FACTORY_H_
