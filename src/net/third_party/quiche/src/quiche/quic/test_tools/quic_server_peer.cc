// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_server_peer.h"

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_reader.h"
#include "quiche/quic/tools/quic_server.h"

namespace quic {
namespace test {

// static
bool QuicServerPeer::SetSmallSocket(QuicServer* server) {
  int size = 1024 * 10;
  return setsockopt(server->fd_, SOL_SOCKET, SO_RCVBUF,
                    reinterpret_cast<char*>(&size), sizeof(size)) != -1;
}

// static
QuicDispatcher* QuicServerPeer::GetDispatcher(QuicServer* server) {
  return server->dispatcher_.get();
}

// static
void QuicServerPeer::SetReader(QuicServer* server, QuicPacketReader* reader) {
  server->packet_reader_.reset(reader);
}

}  // namespace test
}  // namespace quic
