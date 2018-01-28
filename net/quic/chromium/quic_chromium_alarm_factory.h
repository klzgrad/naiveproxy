// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The Chrome-specific helper for QuicConnection which uses
// a TaskRunner for alarms, and uses a DatagramClientSocket for writing data.

#ifndef NET_QUIC_CHROMIUM_QUIC_CHROMIUM_ALARM_FACTORY_H_
#define NET_QUIC_CHROMIUM_QUIC_CHROMIUM_ALARM_FACTORY_H_

#include <set>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_clock.h"

namespace base {
class TaskRunner;
}  // namespace base

namespace net {

class NET_EXPORT_PRIVATE QuicChromiumAlarmFactory : public QuicAlarmFactory {
 public:
  QuicChromiumAlarmFactory(base::TaskRunner* task_runner,
                           const QuicClock* clock);
  ~QuicChromiumAlarmFactory() override;

  // QuicAlarmFactory
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  base::TaskRunner* task_runner_;
  const QuicClock* clock_;
  base::WeakPtrFactory<QuicChromiumAlarmFactory> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuicChromiumAlarmFactory);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_CHROMIUM_ALARM_FACTORY_H_
