// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_time_wait_list_manager_peer.h"

namespace quic {
namespace test {

bool QuicTimeWaitListManagerPeer::ShouldSendResponse(
    QuicTimeWaitListManager* manager, int received_packet_count) {
  return manager->ShouldSendResponse(received_packet_count);
}

QuicTime::Delta QuicTimeWaitListManagerPeer::time_wait_period(
    QuicTimeWaitListManager* manager) {
  return manager->time_wait_period_;
}

QuicAlarm* QuicTimeWaitListManagerPeer::expiration_alarm(
    QuicTimeWaitListManager* manager) {
  return manager->connection_id_clean_up_alarm_.get();
}

void QuicTimeWaitListManagerPeer::set_clock(QuicTimeWaitListManager* manager,
                                            const QuicClock* clock) {
  manager->clock_ = clock;
}

// static
bool QuicTimeWaitListManagerPeer::SendOrQueuePacket(
    QuicTimeWaitListManager* manager,
    std::unique_ptr<QuicTimeWaitListManager::QueuedPacket> packet,
    const QuicPerPacketContext* packet_context) {
  return manager->SendOrQueuePacket(std::move(packet), packet_context);
}

// static
size_t QuicTimeWaitListManagerPeer::PendingPacketsQueueSize(
    QuicTimeWaitListManager* manager) {
  return manager->pending_packets_queue_.size();
}

}  // namespace test
}  // namespace quic
