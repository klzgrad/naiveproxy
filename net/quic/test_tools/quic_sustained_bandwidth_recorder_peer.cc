// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_sustained_bandwidth_recorder_peer.h"

#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_sustained_bandwidth_recorder.h"

namespace net {
namespace test {

// static
void QuicSustainedBandwidthRecorderPeer::SetBandwidthEstimate(
    QuicSustainedBandwidthRecorder* bandwidth_recorder,
    int32_t bandwidth_estimate_kbytes_per_second) {
  bandwidth_recorder->has_estimate_ = true;
  bandwidth_recorder->bandwidth_estimate_ =
      QuicBandwidth::FromKBytesPerSecond(bandwidth_estimate_kbytes_per_second);
}

// static
void QuicSustainedBandwidthRecorderPeer::SetMaxBandwidthEstimate(
    QuicSustainedBandwidthRecorder* bandwidth_recorder,
    int32_t max_bandwidth_estimate_kbytes_per_second,
    int32_t max_bandwidth_timestamp) {
  bandwidth_recorder->max_bandwidth_estimate_ =
      QuicBandwidth::FromKBytesPerSecond(
          max_bandwidth_estimate_kbytes_per_second);
  bandwidth_recorder->max_bandwidth_timestamp_ = max_bandwidth_timestamp;
}

}  // namespace test
}  // namespace net
