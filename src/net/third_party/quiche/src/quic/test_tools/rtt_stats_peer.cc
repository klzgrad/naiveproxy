// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/rtt_stats_peer.h"

namespace quic {
namespace test {

// static
void RttStatsPeer::SetSmoothedRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms) {
  rtt_stats->smoothed_rtt_ = rtt_ms;
}

// static
void RttStatsPeer::SetMinRtt(RttStats* rtt_stats, QuicTime::Delta rtt_ms) {
  rtt_stats->min_rtt_ = rtt_ms;
}

}  // namespace test
}  // namespace quic
