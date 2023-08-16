// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_unacked_packet_map_peer.h"

namespace quic {
namespace test {

// static
const QuicStreamFrame& QuicUnackedPacketMapPeer::GetAggregatedStreamFrame(
    const QuicUnackedPacketMap& unacked_packets) {
  return unacked_packets.aggregated_stream_frame_;
}

// static
void QuicUnackedPacketMapPeer::SetPerspective(
    QuicUnackedPacketMap* unacked_packets, Perspective perspective) {
  *const_cast<Perspective*>(&unacked_packets->perspective_) = perspective;
}

// static
size_t QuicUnackedPacketMapPeer::GetCapacity(
    const QuicUnackedPacketMap& unacked_packets) {
  return unacked_packets.unacked_packets_.capacity();
}

}  // namespace test
}  // namespace quic
