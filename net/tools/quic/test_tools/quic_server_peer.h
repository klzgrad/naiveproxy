// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_

#include "base/macros.h"

namespace net {

class QuicDispatcher;
class QuicServer;
class QuicPacketReader;

namespace test {

class QuicServerPeer {
 public:
  static bool SetSmallSocket(QuicServer* server);
  static QuicDispatcher* GetDispatcher(QuicServer* server);
  static void SetReader(QuicServer* server, QuicPacketReader* reader);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicServerPeer);
};

}  // namespace test
}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_QUIC_SERVER_PEER_H_
