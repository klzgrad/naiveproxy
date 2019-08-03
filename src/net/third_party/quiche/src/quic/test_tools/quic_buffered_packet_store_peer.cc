// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_buffered_packet_store_peer.h"

#include "net/third_party/quiche/src/quic/core/quic_buffered_packet_store.h"

namespace quic {
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
}  // namespace quic
