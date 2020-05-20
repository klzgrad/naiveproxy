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
void QuicConfigPeer::SetReceivedMaxBidirectionalStreams(QuicConfig* config,
                                                        uint32_t max_streams) {
  config->max_bidirectional_streams_.SetReceivedValue(max_streams);
}
// static
void QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(QuicConfig* config,
                                                         uint32_t max_streams) {
  config->max_unidirectional_streams_.SetReceivedValue(max_streams);
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

// static
void QuicConfigPeer::SetReceivedMaxPacketSize(QuicConfig* config,
                                              uint32_t max_packet_size) {
  config->max_packet_size_.SetReceivedValue(max_packet_size);
}

// static
void QuicConfigPeer::ReceiveIdleNetworkTimeout(QuicConfig* config,
                                               HelloType hello_type,
                                               uint32_t idle_timeout_seconds) {
  std::string error_details;
  config->idle_network_timeout_seconds_.ReceiveValue(
      idle_timeout_seconds, hello_type, &error_details);
}

}  // namespace test
}  // namespace quic
