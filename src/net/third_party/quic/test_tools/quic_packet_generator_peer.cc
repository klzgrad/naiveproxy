// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/quic_packet_generator_peer.h"

#include "net/third_party/quic/core/quic_packet_creator.h"
#include "net/third_party/quic/core/quic_packet_generator.h"

namespace quic {
namespace test {

// static
QuicPacketCreator* QuicPacketGeneratorPeer::GetPacketCreator(
    QuicPacketGenerator* generator) {
  return &generator->packet_creator_;
}

}  // namespace test
}  // namespace quic
