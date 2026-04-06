// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_COALESCED_PACKET_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_COALESCED_PACKET_PEER_H_

#include "quiche/quic/core/quic_coalesced_packet.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {
namespace test {

class QuicCoalescedPacketPeer {
 public:
  static void SetMaxPacketLength(QuicCoalescedPacket& coalesced_packet,
                                 QuicPacketLength length);

  static std::string* GetMutableEncryptedBuffer(
      QuicCoalescedPacket& coalesced_packet, EncryptionLevel encryption_level);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_COALESCED_PACKET_PEER_H_
