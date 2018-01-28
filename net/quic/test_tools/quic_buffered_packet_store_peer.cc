// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_buffered_packet_store_peer.h"

#include "net/quic/core/quic_buffered_packet_store.h"

namespace net {
namespace test {

// static
QuicAlarm* QuicBufferedPacketStorePeer::expiration_alarm(
    QuicBufferedPacketStore* store) {
  return store->expiration_alarm_.get();
}

// static
void QuicBufferedPacketStorePeer::set_clock(QuicBufferedPacketStore* store,
                                            const QuicClock* clock) {
  store->clock_ = clock;
}

}  // namespace test
}  // namespace net
