// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_ID_MANAGER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_ID_MANAGER_PEER_H_

#include <stddef.h>

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

class QuicStreamIdManager;
class UberQuicStreamIdManager;

namespace test {

class QuicStreamIdManagerPeer {
 public:
  QuicStreamIdManagerPeer() = delete;

  static void set_incoming_actual_max_streams(
      QuicStreamIdManager* stream_id_manager,
      QuicStreamCount count);
  static void set_outgoing_max_streams(QuicStreamIdManager* stream_id_manager,
                                       QuicStreamCount count);

  static QuicStreamId GetFirstIncomingStreamId(
      QuicStreamIdManager* stream_id_manager);

  static bool get_unidirectional(QuicStreamIdManager* stream_id_manager);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
