// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_coalesced_packet_peer.h"

#include <string>

namespace quic {
namespace test {

//  static
void QuicCoalescedPacketPeer::SetMaxPacketLength(
    QuicCoalescedPacket& coalesced_packet, QuicPacketLength length) {
  coalesced_packet.max_packet_length_ = length;
}

//  static
std::string* QuicCoalescedPacketPeer::GetMutableEncryptedBuffer(
    QuicCoalescedPacket& coalesced_packet, EncryptionLevel encryption_level) {
  return &coalesced_packet.encrypted_buffers_[encryption_level];
}

}  // namespace test
}  // namespace quic
