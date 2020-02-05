// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"

#include "net/third_party/quiche/src/quic/core/quic_config.h"

namespace quic {
namespace test {

// static
void QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_stream_flow_control_window_bytes_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_incoming_bidirectional_
      .SetReceivedValue(window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_outgoing_bidirectional_
      .SetReceivedValue(window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_unidirectional_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_session_flow_control_window_bytes_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedConnectionOptions(
    QuicConfig* config,
    const QuicTagVector& options) {
  config->connection_options_.SetReceivedValues(options);
}

// static
void QuicConfigPeer::SetReceivedBytesForConnectionId(QuicConfig* config,
                                                     uint32_t bytes) {
  DCHECK(bytes == 0 || bytes == 8);
  config->bytes_for_connection_id_.SetReceivedValue(bytes);
}

// static
void QuicConfigPeer::SetReceivedDisableConnectionMigration(QuicConfig* config) {
  config->connection_migration_disabled_.SetReceivedValue(1);
}

// static
void QuicConfigPeer::SetReceivedMaxIncomingBidirectionalStreams(
    QuicConfig* config,
    uint32_t max_streams) {
  config->max_incoming_bidirectional_streams_.SetReceivedValue(max_streams);
}
// static
void QuicConfigPeer::SetReceivedMaxIncomingUnidirectionalStreams(
    QuicConfig* config,
    uint32_t max_streams) {
  config->max_incoming_unidirectional_streams_.SetReceivedValue(max_streams);
}

// static
void QuicConfigPeer::SetConnectionOptionsToSend(QuicConfig* config,
                                                const QuicTagVector& options) {
  config->SetConnectionOptionsToSend(options);
}

// static
void QuicConfigPeer::SetReceivedStatelessResetToken(QuicConfig* config,
                                                    QuicUint128 token) {
  config->stateless_reset_token_.SetReceivedValue(token);
}

}  // namespace test
}  // namespace quic
