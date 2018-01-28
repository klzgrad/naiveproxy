// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_

#include "net/tools/quic/quic_time_wait_list_manager.h"

namespace net {
namespace test {

class QuicTimeWaitListManagerPeer {
 public:
  static bool ShouldSendResponse(QuicTimeWaitListManager* manager,
                                 int received_packet_count);

  static QuicTime::Delta time_wait_period(QuicTimeWaitListManager* manager);

  static QuicTransportVersion GetQuicVersionFromConnectionId(
      QuicTimeWaitListManager* manager,
      QuicConnectionId connection_id);

  static QuicAlarm* expiration_alarm(QuicTimeWaitListManager* manager);

  static void set_clock(QuicTimeWaitListManager* manager,
                        const QuicClock* clock);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_TIME_WAIT_LIST_MANAGER_PEER_H_
