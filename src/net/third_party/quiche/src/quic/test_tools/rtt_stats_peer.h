// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"

namespace quic {
namespace test {

class RttStatsPeer {
 public:
  RttStatsPeer() = delete;

  static void SetSmoothedRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms);

  static void SetMinRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_
