// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/quic_packets.h"

namespace quic {

class QuicPacketCreator;
class QuicPacketGenerator;

namespace test {

class QuicPacketGeneratorPeer {
 public:
  QuicPacketGeneratorPeer() = delete;

  static QuicPacketCreator* GetPacketCreator(QuicPacketGenerator* generator);
};

}  // namespace test

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_
