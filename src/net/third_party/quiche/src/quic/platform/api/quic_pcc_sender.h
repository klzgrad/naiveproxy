// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_PCC_SENDER_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_PCC_SENDER_H_

#include "net/quic/platform/impl/quic_pcc_sender_impl.h"

namespace quic {

class SendAlgorithmInterface;
// Interface for creating a PCC SendAlgorithmInterface
inline SendAlgorithmInterface* CreatePccSender(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    const QuicUnackedPacketMap* unacked_packets,
    QuicRandom* random,
    QuicConnectionStats* stats,
    QuicPacketCount initial_congestion_window,
    QuicPacketCount max_congestion_window) {
  return CreatePccSenderImpl(clock, rtt_stats, unacked_packets, random, stats,
                             initial_congestion_window, max_congestion_window);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_PCC_SENDER_H_
