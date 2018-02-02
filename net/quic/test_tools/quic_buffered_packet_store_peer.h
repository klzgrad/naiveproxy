// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_

#include <memory>

#include "net/quic/core/quic_alarm.h"
#include "net/quic/platform/api/quic_clock.h"

namespace net {

class QuicBufferedPacketStore;

namespace test {

class QuicBufferedPacketStorePeer {
 public:
  static QuicAlarm* expiration_alarm(QuicBufferedPacketStore* store);

  static void set_clock(QuicBufferedPacketStore* store, const QuicClock* clock);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicBufferedPacketStorePeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_BUFFERED_PACKET_STORE_PEER_H_
