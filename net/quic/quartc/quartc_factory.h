// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_FACTORY_H_
#define NET_QUIC_QUARTC_QUARTC_FACTORY_H_

#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_simple_buffer_allocator.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/quartc/quartc_factory_interface.h"
#include "net/quic/quartc/quartc_packet_writer.h"
#include "net/quic/quartc/quartc_task_runner_interface.h"

namespace net {

// Implements the QuartcFactoryInterface to create the instances of
// QuartcSessionInterface. Implements the QuicAlarmFactory to create alarms
// using the QuartcTaskRunner. Implements the QuicConnectionHelperInterface used
// by the QuicConnections. Only one QuartcFactory is expected to be created.
class QUIC_EXPORT_PRIVATE QuartcFactory : public QuartcFactoryInterface,
                                          public QuicAlarmFactory,
                                          public QuicConnectionHelperInterface {
 public:
  explicit QuartcFactory(const QuartcFactoryConfig& factory_config);
  ~QuartcFactory() override;

  // QuartcFactoryInterface overrides.
  std::unique_ptr<QuartcSessionInterface> CreateQuartcSession(
      const QuartcSessionConfig& quartc_session_config) override;

  // QuicAlarmFactory overrides.
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;

  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

  // QuicConnectionHelperInterface overrides.
  const QuicClock* GetClock() const override;

  QuicRandom* GetRandomGenerator() override;

  QuicBufferAllocator* GetStreamSendBufferAllocator() override;

 private:
  std::unique_ptr<QuicConnection> CreateQuicConnection(
      const QuartcSessionConfig& quartc_session_config,
      Perspective perspective);

  // Used to implement QuicAlarmFactory..
  QuartcTaskRunnerInterface* task_runner_;
  // Used to implement the QuicConnectionHelperInterface.
  // The QuicClock wrapper held in this variable is owned by QuartcFactory,
  // but the QuartcClockInterface inside of it belongs to the user!
  std::unique_ptr<QuicClock> clock_;
  SimpleBufferAllocator buffer_allocator_;
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_FACTORY_H_
