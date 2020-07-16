// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_client_peer.h"

#include "net/third_party/quiche/src/quic/tools/quic_client.h"

namespace quic {
namespace test {

// static
bool QuicClientPeer::CreateUDPSocketAndBind(QuicClient* client) {
  return client->network_helper()->CreateUDPSocketAndBind(
      client->server_address(), client->bind_to_address(),
      client->local_port());
}

// static
void QuicClientPeer::CleanUpUDPSocket(QuicClient* client, int fd) {
  client->epoll_network_helper()->CleanUpUDPSocket(fd);
}

// static
void QuicClientPeer::SetClientPort(QuicClient* client, int port) {
  client->epoll_network_helper()->SetClientPort(port);
}

// static
void QuicClientPeer::SetWriter(QuicClient* client, QuicPacketWriter* writer) {
  client->set_writer(writer);
}

}  // namespace test
}  // namespace quic
