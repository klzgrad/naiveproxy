// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/quic_server_peer.h"

#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/quic_packet_reader.h"
#include "net/tools/quic/quic_server.h"

namespace net {
namespace test {

// static
bool QuicServerPeer::SetSmallSocket(QuicServer* server) {
  int size = 1024 * 10;
  return setsockopt(server->fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) !=
         -1;
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
}  // namespace net
