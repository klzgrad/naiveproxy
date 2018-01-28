// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_PROCESS_PACKET_INTERFACE_H_
#define NET_TOOLS_QUIC_QUIC_PROCESS_PACKET_INTERFACE_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_socket_address.h"

namespace net {

// A class to process each incoming packet.
class ProcessPacketInterface {
 public:
  virtual ~ProcessPacketInterface() {}
  virtual void ProcessPacket(const QuicSocketAddress& server_address,
                             const QuicSocketAddress& client_address,
                             const QuicReceivedPacket& packet) = 0;
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_PROCESS_PACKET_INTERFACE_H_
