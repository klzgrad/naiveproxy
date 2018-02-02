// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/rtt_stats.h"

#include <cstdlib>  // std::abs

#include "net/quic/platform/api/quic_logging.h"

namespace net {

namespace {

// Default initial rtt used before any samples are received.
const int kInitialRttMs = 100;
const float kAlpha = 0.125f;
const float kOneMinusAlpha = (1 - kAlpha);
const float kBeta = 0.25f;
const float kOneMinusBeta = (1 - kBeta);

}  // namespace

RttStats::RttStats()
    : latest_rtt_(QuicTime::Delta::Zero()),
      min_rtt_(QuicTime::Delta::Zero()),
      smoothed_rtt_(QuicTime::Delta::Zero()),
      previous_srtt_(QuicTime::Delta::Zero()),
      mean_deviation_(QuicTime::Delta::Zero()),
      initial_rtt_us_(kInitialRttMs * kNumMicrosPerMilli) {}

void RttStats::ExpireSmoothedMetrics() {
  mean_deviation_ = std::max(
      mean_deviation_, QuicTime::Delta::FromMicroseconds(std::abs(
                           (smoothed_rtt_ - latest_rtt_).ToMicroseconds())));
  smoothed_rtt_ = std::max(smoothed_rtt_, latest_rtt_);
}

// Updates the RTT based on a new sample.
void RttStats::UpdateRtt(QuicTime::Delta send_delta,
                         QuicTime::Delta ack_delay,
                         QuicTime now) {
  if (send_delta.IsInfinite() || send_delta <= QuicTime::Delta::Zero()) {
    QUIC_LOG_FIRST_N(WARNING, 3)
        << "Ignoring measured send_delta, because it's is "
        << "either infinite, zero, or negative.  send_delta = "
        << send_delta.ToMicroseconds();
    return;
  }

  // Update min_rtt_ first. min_rtt_ does not use an rtt_sample corrected for
  // ack_delay but the raw observed send_delta, since poor clock granularity at
  // the client may cause a high ack_delay to result in underestimation of the
  // min_rtt_.
  if (min_rtt_.IsZero() || min_rtt_ > send_delta) {
    min_rtt_ = send_delta;
  }

  // Correct for ack_delay if information received from the peer results in a
  // positive RTT sample. Otherwise, we use the send_delta as a reasonable
  // measure for smoothed_rtt.
  QuicTime::Delta rtt_sample(send_delta);
  previous_srtt_ = smoothed_rtt_;

  if (rtt_sample > ack_delay) {
    rtt_sample = rtt_sample - ack_delay;
  }
  latest_rtt_ = rtt_sample;
  // First time call.
  if (smoothed_rtt_.IsZero()) {
    smoothed_rtt_ = rtt_sample;
    mean_deviation_ =
        QuicTime::Delta::FromMicroseconds(rtt_sample.ToMicroseconds() / 2);
  } else {
    mean_deviation_ = QuicTime::Delta::FromMicroseconds(static_cast<int64_t>(
        kOneMinusBeta * mean_deviation_.ToMicroseconds() +
        kBeta * std::abs((smoothed_rtt_ - rtt_sample).ToMicroseconds())));
    smoothed_rtt_ = kOneMinusAlpha * smoothed_rtt_ + kAlpha * rtt_sample;
    QUIC_DVLOG(1) << " smoothed_rtt(us):" << smoothed_rtt_.ToMicroseconds()
                  << " mean_deviation(us):" << mean_deviation_.ToMicroseconds();
  }
}

void RttStats::OnConnectionMigration() {
  latest_rtt_ = QuicTime::Delta::Zero();
  min_rtt_ = QuicTime::Delta::Zero();
  smoothed_rtt_ = QuicTime::Delta::Zero();
  mean_deviation_ = QuicTime::Delta::Zero();
  initial_rtt_us_ = kInitialRttMs * kNumMicrosPerMilli;
}

}  // namespace net
