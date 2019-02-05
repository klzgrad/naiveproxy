// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PEER_H_

#include "base/macros.h"

namespace quic {

class QuicClient;
class QuicPacketWriter;

namespace test {

class QuicClientPeer {
 public:
  QuicClientPeer() = delete;

  static bool CreateUDPSocketAndBind(QuicClient* client);
  static void CleanUpUDPSocket(QuicClient* client, int fd);
  static void SetClientPort(QuicClient* client, int port);
  static void SetWriter(QuicClient* client, QuicPacketWriter* writer);
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PEER_H_
