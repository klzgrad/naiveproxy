// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"

namespace quic {

class QuicConfig;

namespace test {

class QuicConfigPeer {
 public:
  QuicConfigPeer() = delete;

  static void SetReceivedInitialStreamFlowControlWindow(QuicConfig* config,
                                                        uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
      QuicConfig* config,
      uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
      QuicConfig* config,
      uint32_t window_bytes);

  static void SetReceivedInitialMaxStreamDataBytesUnidirectional(
      QuicConfig* config,
      uint32_t window_bytes);

  static void SetReceivedInitialSessionFlowControlWindow(QuicConfig* config,
                                                         uint32_t window_bytes);

  static void SetReceivedConnectionOptions(QuicConfig* config,
                                           const QuicTagVector& options);

  static void SetReceivedBytesForConnectionId(QuicConfig* config,
                                              uint32_t bytes);

  static void SetReceivedDisableConnectionMigration(QuicConfig* config);

  static void SetReceivedMaxIncomingBidirectionalStreams(QuicConfig* config,
                                                         uint32_t max_streams);
  static void SetReceivedMaxIncomingUnidirectionalStreams(QuicConfig* config,
                                                          uint32_t max_streams);

  static void SetConnectionOptionsToSend(QuicConfig* config,
                                         const QuicTagVector& options);

  static void SetReceivedStatelessResetToken(QuicConfig* config,
                                             QuicUint128 token);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
