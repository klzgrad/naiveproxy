// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_

#include <memory>

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_buffered_packet_store.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"

namespace quic {

class QuicBufferedPacketStore;

namespace test {

class QuicBufferedPacketStorePeer {
 public:
  QuicBufferedPacketStorePeer() = delete;

  static QuicAlarm* expiration_alarm(QuicBufferedPacketStore* store);

  static void set_clock(QuicBufferedPacketStore* store, const QuicClock* clock);

  static const QuicBufferedPacketStore::BufferedPacketList* FindBufferedPackets(
      const QuicBufferedPacketStore* store, QuicConnectionId connection_id);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
