// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_config_peer.h"

#include <utility>

#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"

namespace quic {
namespace test {

// static
void QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(
    QuicConfig* config, uint32_t window_bytes) {
  config->initial_stream_flow_control_window_bytes_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
    QuicConfig* config, uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_incoming_bidirectional_
      .SetReceivedValue(window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
    QuicConfig* config, uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_outgoing_bidirectional_
      .SetReceivedValue(window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
    QuicConfig* config, uint32_t window_bytes) {
  config->initial_max_stream_data_bytes_unidirectional_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
    QuicConfig* config, uint32_t window_bytes) {
  config->initial_session_flow_control_window_bytes_.SetReceivedValue(
      window_bytes);
}

// static
void QuicConfigPeer::SetReceivedConnectionOptions(
    QuicConfig* config, const QuicTagVector& options) {
  config->connection_options_.SetReceivedValues(options);
}

// static
void QuicConfigPeer::SetReceivedBytesForConnectionId(QuicConfig* config,
                                                     uint32_t bytes) {
  QUICHE_DCHECK(bytes == 0 || bytes == 8);
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
void QuicConfigPeer::SetReceivedStatelessResetToken(
    QuicConfig* config, const StatelessResetToken& token) {
  config->stateless_reset_token_.SetReceivedValue(token);
}

// static
void QuicConfigPeer::SetReceivedMaxPacketSize(QuicConfig* config,
                                              uint32_t max_udp_payload_size) {
  config->max_udp_payload_size_.SetReceivedValue(max_udp_payload_size);
}

// static
void QuicConfigPeer::SetReceivedMinAckDelayMs(QuicConfig* config,
                                              uint32_t min_ack_delay_ms) {
  config->min_ack_delay_ms_.SetReceivedValue(min_ack_delay_ms);
}

// static
void QuicConfigPeer::SetNegotiated(QuicConfig* config, bool negotiated) {
  config->negotiated_ = negotiated;
}

// static
void QuicConfigPeer::SetReceivedOriginalConnectionId(
    QuicConfig* config,
    const QuicConnectionId& original_destination_connection_id) {
  config->received_original_destination_connection_id_ =
      original_destination_connection_id;
}

// static
void QuicConfigPeer::SetReceivedInitialSourceConnectionId(
    QuicConfig* config, const QuicConnectionId& initial_source_connection_id) {
  config->received_initial_source_connection_id_ = initial_source_connection_id;
}

// static
void QuicConfigPeer::SetReceivedRetrySourceConnectionId(
    QuicConfig* config, const QuicConnectionId& retry_source_connection_id) {
  config->received_retry_source_connection_id_ = retry_source_connection_id;
}

// static
void QuicConfigPeer::SetReceivedMaxDatagramFrameSize(
    QuicConfig* config, uint64_t max_datagram_frame_size) {
  config->max_datagram_frame_size_.SetReceivedValue(max_datagram_frame_size);
}

//  static
void QuicConfigPeer::SetReceivedAlternateServerAddress(
    QuicConfig* config, const QuicSocketAddress& server_address) {
  switch (server_address.host().address_family()) {
    case quiche::IpAddressFamily::IP_V4:
      config->alternate_server_address_ipv4_.SetReceivedValue(server_address);
      break;
    case quiche::IpAddressFamily::IP_V6:
      config->alternate_server_address_ipv6_.SetReceivedValue(server_address);
      break;
    case quiche::IpAddressFamily::IP_UNSPEC:
      break;
  }
}

// static
void QuicConfigPeer::SetPreferredAddressConnectionIdAndToken(
    QuicConfig* config, QuicConnectionId connection_id,
    const StatelessResetToken& token) {
  config->preferred_address_connection_id_and_token_ =
      std::make_pair(connection_id, token);
}

}  // namespace test
}  // namespace quic
