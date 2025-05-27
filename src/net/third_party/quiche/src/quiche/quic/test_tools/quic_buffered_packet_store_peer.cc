// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_buffered_packet_store_peer.h"

#include "quiche/quic/core/quic_buffered_packet_store.h"

namespace quic {
namespace test {

QuicAlarm* QuicBufferedPacketStorePeer::expiration_alarm(
    QuicBufferedPacketStore* store) {
  return store->expiration_alarm_.get();
}

void QuicBufferedPacketStorePeer::set_clock(QuicBufferedPacketStore* store,
                                            const QuicClock* clock) {
  store->clock_ = clock;
}

const QuicBufferedPacketStore::BufferedPacketList*
QuicBufferedPacketStorePeer::FindBufferedPackets(
    const QuicBufferedPacketStore* store, QuicConnectionId connection_id) {
  auto it = store->buffered_session_map_.find(connection_id);
  if (it == store->buffered_session_map_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace test
}  // namespace quic
