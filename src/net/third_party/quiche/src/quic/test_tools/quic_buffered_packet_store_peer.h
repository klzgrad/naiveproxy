// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_

#include <memory>

#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_clock.h"

namespace quic {

class QuicBufferedPacketStore;

namespace test {

class QuicBufferedPacketStorePeer {
 public:
  QuicBufferedPacketStorePeer() = delete;

  static QuicAlarm* expiration_alarm(QuicBufferedPacketStore* store);

  static void set_clock(QuicBufferedPacketStore* store, const QuicClock* clock);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
