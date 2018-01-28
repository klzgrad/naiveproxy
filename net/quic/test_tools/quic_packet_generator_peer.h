// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"

namespace net {

class QuicPacketCreator;
class QuicPacketGenerator;

namespace test {

class QuicPacketGeneratorPeer {
 public:
  static QuicPacketCreator* GetPacketCreator(QuicPacketGenerator* generator);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicPacketGeneratorPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_PACKET_GENERATOR_PEER_H_
