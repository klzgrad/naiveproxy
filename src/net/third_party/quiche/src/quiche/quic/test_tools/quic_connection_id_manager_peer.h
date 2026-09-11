// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_ID_MANAGER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_ID_MANAGER_PEER_H_

#include "quiche/quic/core/quic_connection_id_manager.h"

namespace quic {
namespace test {

class QuicConnectionIdManagerPeer {
 public:
  static QuicAlarm* GetRetirePeerIssuedConnectionIdAlarm(
      QuicPeerIssuedConnectionIdManager* manager) {
    return manager->retire_connection_id_alarm_.get();
  }

  static QuicAlarm* GetRetireSelfIssuedConnectionIdAlarm(
      QuicSelfIssuedConnectionIdManager* manager) {
    return manager->retire_connection_id_alarm_.get();
  }
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_ID_MANAGER_PEER_H_
