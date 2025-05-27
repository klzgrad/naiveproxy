// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/congestion_control/rtt_stats.h"

#include <algorithm>
#include <cstdlib>  // std::abs

#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

namespace {

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
      calculate_standard_deviation_(false),
      initial_rtt_(QuicTime::Delta::FromMilliseconds(kInitialRttMs)),
      last_update_time_(QuicTime::Zero()) {}

void RttStats::ExpireSmoothedMetrics() {
  mean_deviation_ = std::max(
      mean_deviation_, QuicTime::Delta::FromMicroseconds(std::abs(
                           (smoothed_rtt_ - latest_rtt_).ToMicroseconds())));
  smoothed_rtt_ = std::max(smoothed_rtt_, latest_rtt_);
}

// Updates the RTT based on a new sample.
bool RttStats::UpdateRtt(QuicTime::Delta send_delta, QuicTime::Delta ack_delay,
                         QuicTime now) {
  if (send_delta.IsInfinite() || send_delta <= QuicTime::Delta::Zero()) {
    QUIC_LOG_FIRST_N(WARNING, 3)
        << "Ignoring measured send_delta, because it's is "
        << "either infinite, zero, or negative.  send_delta = "
        << send_delta.ToMicroseconds();
    return false;
  }

  last_update_time_ = now;

  // Update min_rtt_ first. min_rtt_ does not use an rtt_sample corrected for
  // ack_delay but the raw observed send_delta, since poor clock granularity at
  // the client may cause a high ack_delay to result in underestimation of the
  // min_rtt_.
  if (min_rtt_.IsZero() || min_rtt_ > send_delta) {
    min_rtt_ = send_delta;
  }

  QuicTime::Delta rtt_sample(send_delta);
  previous_srtt_ = smoothed_rtt_;
  // Correct for ack_delay if information received from the peer results in a
  // an RTT sample at least as large as min_rtt. Otherwise, only use the
  // send_delta.
  // TODO(fayang): consider to ignore rtt_sample if rtt_sample < ack_delay and
  // ack_delay is relatively large.
  if (rtt_sample > ack_delay) {
    if (rtt_sample - min_rtt_ >= ack_delay) {
      rtt_sample = rtt_sample - ack_delay;
    } else {
      QUIC_CODE_COUNT(quic_ack_delay_makes_rtt_sample_smaller_than_min_rtt);
    }
  } else {
    QUIC_CODE_COUNT(quic_ack_delay_greater_than_rtt_sample);
  }
  latest_rtt_ = rtt_sample;
  if (calculate_standard_deviation_) {
    standard_deviation_calculator_.OnNewRttSample(rtt_sample, smoothed_rtt_);
  }
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
  return true;
}

void RttStats::OnConnectionMigration() {
  latest_rtt_ = QuicTime::Delta::Zero();
  min_rtt_ = QuicTime::Delta::Zero();
  smoothed_rtt_ = QuicTime::Delta::Zero();
  mean_deviation_ = QuicTime::Delta::Zero();
  initial_rtt_ = QuicTime::Delta::FromMilliseconds(kInitialRttMs);
}

QuicTime::Delta RttStats::GetStandardOrMeanDeviation() const {
  QUICHE_DCHECK(calculate_standard_deviation_);
  if (!standard_deviation_calculator_.has_valid_standard_deviation) {
    return mean_deviation_;
  }
  return standard_deviation_calculator_.CalculateStandardDeviation();
}

void RttStats::StandardDeviationCalculator::OnNewRttSample(
    QuicTime::Delta rtt_sample, QuicTime::Delta smoothed_rtt) {
  double new_value = rtt_sample.ToMicroseconds();
  if (smoothed_rtt.IsZero()) {
    return;
  }
  has_valid_standard_deviation = true;
  const double delta = new_value - smoothed_rtt.ToMicroseconds();
  m2 = kOneMinusBeta * m2 + kBeta * pow(delta, 2);
}

QuicTime::Delta
RttStats::StandardDeviationCalculator::CalculateStandardDeviation() const {
  QUICHE_DCHECK(has_valid_standard_deviation);
  return QuicTime::Delta::FromMicroseconds(sqrt(m2));
}

void RttStats::CloneFrom(const RttStats& stats) {
  latest_rtt_ = stats.latest_rtt_;
  min_rtt_ = stats.min_rtt_;
  smoothed_rtt_ = stats.smoothed_rtt_;
  previous_srtt_ = stats.previous_srtt_;
  mean_deviation_ = stats.mean_deviation_;
  standard_deviation_calculator_ = stats.standard_deviation_calculator_;
  calculate_standard_deviation_ = stats.calculate_standard_deviation_;
  initial_rtt_ = stats.initial_rtt_;
  last_update_time_ = stats.last_update_time_;
}

}  // namespace quic
