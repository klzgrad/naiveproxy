// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_STREAM_ID_MANAGER_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_STREAM_ID_MANAGER_PEER_H_

#include "base/macros.h"

namespace quic {

class QuicStreamIdManager;

namespace test {

class QuicStreamIdManagerPeer {
 public:
  QuicStreamIdManagerPeer() = delete;
  static void IncrementMaximumAllowedOutgoingStreamId(
      QuicStreamIdManager* stream_id_manager,
      int increment);
  static void IncrementMaximumAllowedIncomingStreamId(
      QuicStreamIdManager* stream_id_manager,
      int increment);
  static void SetMaxOpenIncomingStreams(QuicStreamIdManager* stream_id_manager,
                                        size_t max_streams);
};

}  // namespace test

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
