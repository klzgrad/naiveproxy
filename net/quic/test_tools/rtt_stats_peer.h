// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_
#define NET_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_

#include "base/macros.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/quic_time.h"

namespace net {
namespace test {

class RttStatsPeer {
 public:
  static void SetSmoothedRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms);

  static void SetMinRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms);

 private:
  DISALLOW_COPY_AND_ASSIGN(RttStatsPeer);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_RTT_STATS_PEER_H_
