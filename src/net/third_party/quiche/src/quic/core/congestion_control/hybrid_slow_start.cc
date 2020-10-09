// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/hybrid_slow_start.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

// Note(pwestin): the magic clamping numbers come from the original code in
// tcp_cubic.c.
const int64_t kHybridStartLowWindow = 16;
// Number of delay samples for detecting the increase of delay.
const uint32_t kHybridStartMinSamples = 8;
// Exit slow start if the min rtt has increased by more than 1/8th.
const int kHybridStartDelayFactorExp = 3;  // 2^3 = 8
// The original paper specifies 2 and 8ms, but those have changed over time.
const int64_t kHybridStartDelayMinThresholdUs = 4000;
const int64_t kHybridStartDelayMaxThresholdUs = 16000;

HybridSlowStart::HybridSlowStart()
    : started_(false),
      hystart_found_(NOT_FOUND),
      rtt_sample_count_(0),
      current_min_rtt_(QuicTime::Delta::Zero()) {}

void HybridSlowStart::OnPacketAcked(QuicPacketNumber acked_packet_number) {
  // OnPacketAcked gets invoked after ShouldExitSlowStart, so it's best to end
  // the round when the final packet of the burst is received and start it on
  // the next incoming ack.
  if (IsEndOfRound(acked_packet_number)) {
    started_ = false;
  }
}

void HybridSlowStart::OnPacketSent(QuicPacketNumber packet_number) {
  last_sent_packet_number_ = packet_number;
}

void HybridSlowStart::Restart() {
  started_ = false;
  hystart_found_ = NOT_FOUND;
}

void HybridSlowStart::StartReceiveRound(QuicPacketNumber last_sent) {
  QUIC_DVLOG(1) << "Reset hybrid slow start @" << last_sent;
  end_packet_number_ = last_sent;
  current_min_rtt_ = QuicTime::Delta::Zero();
  rtt_sample_count_ = 0;
  started_ = true;
}

bool HybridSlowStart::IsEndOfRound(QuicPacketNumber ack) const {
  return !end_packet_number_.IsInitialized() || end_packet_number_ <= ack;
}

bool HybridSlowStart::ShouldExitSlowStart(QuicTime::Delta latest_rtt,
                                          QuicTime::Delta min_rtt,
                                          QuicPacketCount congestion_window) {
  if (!started_) {
    // Time to start the hybrid slow start.
    StartReceiveRound(last_sent_packet_number_);
  }
  if (hystart_found_ != NOT_FOUND) {
    return true;
  }
  // Second detection parameter - delay increase detection.
  // Compare the minimum delay (current_min_rtt_) of the current
  // burst of packets relative to the minimum delay during the session.
  // Note: we only look at the first few(8) packets in each burst, since we
  // only want to compare the lowest RTT of the burst relative to previous
  // bursts.
  rtt_sample_count_++;
  if (rtt_sample_count_ <= kHybridStartMinSamples) {
    if (current_min_rtt_.IsZero() || current_min_rtt_ > latest_rtt) {
      current_min_rtt_ = latest_rtt;
    }
  }
  // We only need to check this once per round.
  if (rtt_sample_count_ == kHybridStartMinSamples) {
    // Divide min_rtt by 8 to get a rtt increase threshold for exiting.
    int64_t min_rtt_increase_threshold_us =
        min_rtt.ToMicroseconds() >> kHybridStartDelayFactorExp;
    // Ensure the rtt threshold is never less than 2ms or more than 16ms.
    min_rtt_increase_threshold_us = std::min(min_rtt_increase_threshold_us,
                                             kHybridStartDelayMaxThresholdUs);
    QuicTime::Delta min_rtt_increase_threshold =
        QuicTime::Delta::FromMicroseconds(std::max(
            min_rtt_increase_threshold_us, kHybridStartDelayMinThresholdUs));

    if (current_min_rtt_ > min_rtt + min_rtt_increase_threshold) {
      hystart_found_ = DELAY;
    }
  }
  // Exit from slow start if the cwnd is greater than 16 and
  // increasing delay is found.
  return congestion_window >= kHybridStartLowWindow &&
         hystart_found_ != NOT_FOUND;
}

}  // namespace quic
