// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_unacked_packet_map_peer.h"

namespace quic {
namespace test {

const QuicStreamFrame& QuicUnackedPacketMapPeer::GetAggregatedStreamFrame(
    const QuicUnackedPacketMap& unacked_packets) {
  return unacked_packets.aggregated_stream_frame_;
}

void QuicUnackedPacketMapPeer::SetPerspective(
    QuicUnackedPacketMap* unacked_packets, Perspective perspective) {
  *const_cast<Perspective*>(&unacked_packets->perspective_) = perspective;
}

size_t QuicUnackedPacketMapPeer::GetCapacity(
    const QuicUnackedPacketMap& unacked_packets) {
  return unacked_packets.unacked_packets_.capacity();
}

size_t QuicUnackedPacketMapPeer::GetSize(
    const QuicUnackedPacketMap& unacked_packets) {
  return unacked_packets.unacked_packets_.size();
}

}  // namespace test
}  // namespace quic
