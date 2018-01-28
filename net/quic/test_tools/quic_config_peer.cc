// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_config_peer.h"

#include "net/quic/core/quic_config.h"

namespace net {
namespace test {

// static
void QuicConfigPeer::SetReceivedSocketReceiveBuffer(
    QuicConfig* config,
    uint32_t receive_buffer_bytes) {
  config->socket_receive_buffer_.SetReceivedValue(receive_buffer_bytes);
}

// static
void QuicConfigPeer::SetReceivedInitialStreamFlowControlWindow(
    QuicConfig* config,
    uint32_t window_bytes) {
  config->initial_stream_flow_control_window_bytes_.SetReceivedValue(
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
void QuicConfigPeer::SetReceivedMaxIncomingDynamicStreams(
    QuicConfig* config,
    uint32_t max_streams) {
  config->max_incoming_dynamic_streams_.SetReceivedValue(max_streams);
}

// static
void QuicConfigPeer::SetConnectionOptionsToSend(QuicConfig* config,
                                                const QuicTagVector& options) {
  config->SetConnectionOptionsToSend(options);
}

}  // namespace test
}  // namespace net
