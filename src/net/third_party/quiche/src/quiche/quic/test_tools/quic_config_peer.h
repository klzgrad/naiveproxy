// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_

#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

class QuicConfig;

namespace test {

class QuicConfigPeer {
 public:
  QuicConfigPeer() = delete;

  static void SetReceivedInitialStreamFlowControlWindow(QuicConfig* config,
                                                        uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
      QuicConfig* config, uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
      QuicConfig* config, uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesUnidirectional(
      QuicConfig* config, uint32_t window_bytes);

  static void SetReceivedInitialSessionFlowControlWindow(QuicConfig* config,
                                                         uint32_t window_bytes);

  static void SetReceivedConnectionOptions(QuicConfig* config,
                                           const QuicTagVector& options);

  static void SetReceivedBytesForConnectionId(QuicConfig* config,
                                              uint32_t bytes);

  static void SetReceivedDisableConnectionMigration(QuicConfig* config);

  static void SetReceivedMaxBidirectionalStreams(QuicConfig* config,
                                                 uint32_t max_streams);
  static void SetReceivedMaxUnidirectionalStreams(QuicConfig* config,
                                                  uint32_t max_streams);

  static void SetConnectionOptionsToSend(QuicConfig* config,
                                         const QuicTagVector& options);

  static void SetReceivedStatelessResetToken(QuicConfig* config,
                                             const StatelessResetToken& token);

  static void SetReceivedMaxPacketSize(QuicConfig* config,
                                       uint32_t max_udp_payload_size);

  static void SetReceivedMinAckDelayMs(QuicConfig* config,
                                       uint32_t min_ack_delay_ms);

  static void SetNegotiated(QuicConfig* config, bool negotiated);

  static void SetReceivedOriginalConnectionId(
      QuicConfig* config,
      const QuicConnectionId& original_destination_connection_id);

  static void SetReceivedInitialSourceConnectionId(
      QuicConfig* config, const QuicConnectionId& initial_source_connection_id);

  static void SetReceivedRetrySourceConnectionId(
      QuicConfig* config, const QuicConnectionId& retry_source_connection_id);

  static void SetReceivedMaxDatagramFrameSize(QuicConfig* config,
                                              uint64_t max_datagram_frame_size);

  static void SetReceivedAlternateServerAddress(
      QuicConfig* config, const QuicSocketAddress& server_address);

  static void SetPreferredAddressConnectionIdAndToken(
      QuicConfig* config, QuicConnectionId connection_id,
      const StatelessResetToken& token);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
