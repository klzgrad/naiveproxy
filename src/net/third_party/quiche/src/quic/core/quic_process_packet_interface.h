// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PROCESS_PACKET_INTERFACE_H_
#define QUICHE_QUIC_CORE_QUIC_PROCESS_PACKET_INTERFACE_H_

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

// A class to process each incoming packet.
class QUIC_NO_EXPORT ProcessPacketInterface {
 public:
  virtual ~ProcessPacketInterface() {}
  virtual void ProcessPacket(const QuicSocketAddress& self_address,
                             const QuicSocketAddress& peer_address,
                             const QuicReceivedPacket& packet) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PROCESS_PACKET_INTERFACE_H_
