// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_UNACKED_PACKET_MAP_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_UNACKED_PACKET_MAP_PEER_H_

#include "net/third_party/quic/core/quic_unacked_packet_map.h"

namespace quic {
namespace test {

class QuicUnackedPacketMapPeer {
 public:
  static const QuicStreamFrame& GetAggregatedStreamFrame(
      const QuicUnackedPacketMap& unacked_packets);

  static void SetPerspective(QuicUnackedPacketMap* unacked_packets,
                             Perspective perspective);
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_UNACKED_PACKET_MAP_PEER_H_
