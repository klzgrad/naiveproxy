// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quic/test_tools/quic_stream_id_manager_peer.h"

#include "net/third_party/quic/core/quic_stream_id_manager.h"

namespace quic {
namespace test {

// static
void QuicStreamIdManagerPeer::IncrementMaximumAllowedOutgoingStreamId(
    QuicStreamIdManager* stream_id_manager,
    int increment) {
  stream_id_manager->max_allowed_outgoing_stream_id_ +=
      (increment * kV99StreamIdIncrement);
}

// static
void QuicStreamIdManagerPeer::IncrementMaximumAllowedIncomingStreamId(
    QuicStreamIdManager* stream_id_manager,
    int increment) {
  stream_id_manager->actual_max_allowed_incoming_stream_id_ +=
      (increment * kV99StreamIdIncrement);
  stream_id_manager->advertised_max_allowed_incoming_stream_id_ +=
      (increment * kV99StreamIdIncrement);
}

// static
void QuicStreamIdManagerPeer::SetMaxOpenIncomingStreams(
    QuicStreamIdManager* stream_id_manager,
    size_t max_streams) {
  stream_id_manager->SetMaxOpenIncomingStreams(max_streams);
}
}  // namespace test
}  // namespace quic
