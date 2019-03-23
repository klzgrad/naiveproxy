// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_

#include "base/macros.h"

namespace quic {

class QuicDispatcher;
class QuicServer;
class QuicPacketReader;

namespace test {

class QuicServerPeer {
 public:
  QuicServerPeer() = delete;

  static bool SetSmallSocket(QuicServer* server);
  static QuicDispatcher* GetDispatcher(QuicServer* server);
  static void SetReader(QuicServer* server, QuicPacketReader* reader);
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_
