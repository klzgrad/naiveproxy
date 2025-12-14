// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_

#include "quiche/quic/core/quic_time_wait_list_manager.h"

namespace quic {
namespace test {

class QuicTimeWaitListManagerPeer {
 public:
  static bool ShouldSendResponse(QuicTimeWaitListManager* manager,
                                 int received_packet_count);

  static QuicTime::Delta time_wait_period(QuicTimeWaitListManager* manager);

  static QuicAlarm* expiration_alarm(QuicTimeWaitListManager* manager);

  static void set_clock(QuicTimeWaitListManager* manager,
                        const QuicClock* clock);

  static bool SendOrQueuePacket(
      QuicTimeWaitListManager* manager,
      std::unique_ptr<QuicTimeWaitListManager::QueuedPacket> packet,
      const QuicPerPacketContext* packet_context);

  static size_t PendingPacketsQueueSize(QuicTimeWaitListManager* manager);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_
