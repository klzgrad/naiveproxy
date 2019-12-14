// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"

#include "net/third_party/quiche/src/quic/core/quic_stream_id_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/uber_quic_stream_id_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {
namespace test {

// static
void QuicStreamIdManagerPeer::set_incoming_actual_max_streams(
    QuicStreamIdManager* stream_id_manager,
    QuicStreamCount count) {
  stream_id_manager->incoming_actual_max_streams_ = count;
}

// static
void QuicStreamIdManagerPeer::set_outgoing_max_streams(
    QuicStreamIdManager* stream_id_manager,
    QuicStreamCount count) {
  stream_id_manager->outgoing_max_streams_ = count;
}

// static
QuicStreamId QuicStreamIdManagerPeer::GetFirstIncomingStreamId(
    QuicStreamIdManager* stream_id_manager) {
  return stream_id_manager->GetFirstIncomingStreamId();
}

// static
bool QuicStreamIdManagerPeer::get_unidirectional(
    QuicStreamIdManager* stream_id_manager) {
  return stream_id_manager->unidirectional_;
}

}  // namespace test
}  // namespace quic
