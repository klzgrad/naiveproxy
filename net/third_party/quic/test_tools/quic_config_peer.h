// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/quic_config.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_uint128.h"

namespace quic {

class QuicConfig;

namespace test {

class QuicConfigPeer {
 public:
  QuicConfigPeer() = delete;

  static void SetReceivedInitialStreamFlowControlWindow(QuicConfig* config,
                                                        uint32_t window_bytes);

  static void SetReceivedInitialSessionFlowControlWindow(QuicConfig* config,
                                                         uint32_t window_bytes);

  static void SetReceivedConnectionOptions(QuicConfig* config,
                                           const QuicTagVector& options);

  static void SetReceivedBytesForConnectionId(QuicConfig* config,
                                              uint32_t bytes);

  static void SetReceivedDisableConnectionMigration(QuicConfig* config);

  static void SetReceivedMaxIncomingDynamicStreams(QuicConfig* config,
                                                   uint32_t max_streams);

  static void SetConnectionOptionsToSend(QuicConfig* config,
                                         const QuicTagVector& options);

  static void SetReceivedStatelessResetToken(QuicConfig* config,
                                             QuicUint128 token);
};

}  // namespace test

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CONFIG_PEER_H_
