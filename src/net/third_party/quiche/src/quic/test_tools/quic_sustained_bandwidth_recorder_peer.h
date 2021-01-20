// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SUSTAINED_BANDWIDTH_RECORDER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SUSTAINED_BANDWIDTH_RECORDER_PEER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {

class QuicSustainedBandwidthRecorder;

namespace test {

class QuicSustainedBandwidthRecorderPeer {
 public:
  QuicSustainedBandwidthRecorderPeer() = delete;

  static void SetBandwidthEstimate(
      QuicSustainedBandwidthRecorder* bandwidth_recorder,
      int32_t bandwidth_estimate_kbytes_per_second);

  static void SetMaxBandwidthEstimate(
      QuicSustainedBandwidthRecorder* bandwidth_recorder,
      int32_t max_bandwidth_estimate_kbytes_per_second,
      int32_t max_bandwidth_timestamp);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SUSTAINED_BANDWIDTH_RECORDER_PEER_H_
