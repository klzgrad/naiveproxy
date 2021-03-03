// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/qbone/bonnet/qbone_tunnel_silo.h"

#include "absl/synchronization/notification.h"
#include "quic/platform/api/quic_test.h"
#include "quic/qbone/bonnet/mock_qbone_tunnel.h"

namespace quic {
namespace {

using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;

TEST(QboneTunnelSiloTest, SiloRunsEventLoop) {
  MockQboneTunnel mock_tunnel;

  absl::Notification event_loop_run;
  EXPECT_CALL(mock_tunnel, WaitForEvents)
      .WillRepeatedly(Invoke([&event_loop_run]() {
        if (!event_loop_run.HasBeenNotified()) {
          event_loop_run.Notify();
        }
        return false;
      }));

  QboneTunnelSilo silo(&mock_tunnel, false);
  silo.Start();

  event_loop_run.WaitForNotification();

  absl::Notification client_disconnected;
  EXPECT_CALL(mock_tunnel, Disconnect)
      .WillOnce(Invoke([&client_disconnected]() {
        client_disconnected.Notify();
        return QboneTunnelInterface::ENDED;
      }));

  silo.Quit();
  client_disconnected.WaitForNotification();

  silo.Join();
}

TEST(QboneTunnelSiloTest, SiloCanShutDownAfterInit) {
  MockQboneTunnel mock_tunnel;

  int iteration_count = 0;
  EXPECT_CALL(mock_tunnel, WaitForEvents)
      .WillRepeatedly(Invoke([&iteration_count]() {
        iteration_count++;
        return false;
      }));

  EXPECT_CALL(mock_tunnel, state)
      .WillOnce(Return(QboneTunnelInterface::START_REQUESTED))
      .WillOnce(Return(QboneTunnelInterface::STARTED));

  absl::Notification client_disconnected;
  EXPECT_CALL(mock_tunnel, Disconnect)
      .WillOnce(Invoke([&client_disconnected]() {
        client_disconnected.Notify();
        return QboneTunnelInterface::ENDED;
      }));

  QboneTunnelSilo silo(&mock_tunnel, true);
  silo.Start();

  client_disconnected.WaitForNotification();
  silo.Join();
  EXPECT_THAT(iteration_count, Eq(1));
}

}  // namespace
}  // namespace quic
