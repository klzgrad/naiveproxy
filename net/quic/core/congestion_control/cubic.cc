// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/cubic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

namespace {

// Constants based on TCP defaults.
// The following constants are in 2^10 fractions of a second instead of ms to
// allow a 10 shift right to divide.
const int kCubeScale = 40;  // 1024*1024^3 (first 1024 is from 0.100^3)
                            // where 0.100 is 100 ms which is the scaling
                            // round trip time.
const int kCubeCongestionWindowScale = 410;
const uint64_t kCubeFactor =
    (UINT64_C(1) << kCubeScale) / kCubeCongestionWindowScale;

const uint32_t kDefaultNumConnections = 2;
const float kBeta = 0.7f;  // Default Cubic backoff factor.
// Additional backoff factor when loss occurs in the concave part of the Cubic
// curve. This additional backoff factor is expected to give up bandwidth to
// new concurrent flows and speed up convergence.
const float kBetaLastMax = 0.85f;

}  // namespace

Cubic::Cubic(const QuicClock* clock)
    : clock_(clock),
      num_connections_(kDefaultNumConnections),
      epoch_(QuicTime::Zero()),
      app_limited_start_time_(QuicTime::Zero()),
      last_update_time_(QuicTime::Zero()),
      fix_convex_mode_(FLAGS_quic_reloadable_flag_quic_enable_cubic_fixes),
      fix_beta_last_max_(fix_convex_mode_),
      allow_per_ack_updates_(fix_convex_mode_) {
  ResetCubicState();
  if (fix_convex_mode_) {
    QUIC_FLAG_COUNT_N(quic_reloadable_flag_quic_enable_cubic_fixes, 1, 2);
  }
}

void Cubic::SetNumConnections(int num_connections) {
  num_connections_ = num_connections;
}

float Cubic::Alpha() const {
  // TCPFriendly alpha is described in Section 3.3 of the CUBIC paper. Note that
  // beta here is a cwnd multiplier, and is equal to 1-beta from the paper.
  // We derive the equivalent alpha for an N-connection emulation as:
  const float beta = Beta();
  return 3 * num_connections_ * num_connections_ * (1 - beta) / (1 + beta);
}

float Cubic::Beta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kBeta) / num_connections_;
}

float Cubic::BetaLastMax() const {
  // BetaLastMax is the additional backoff factor after loss for our
  // N-connection emulation, which emulates the additional backoff of
  // an ensemble of N TCP-Reno connections on a single loss event. The
  // effective multiplier is computed as:
  return fix_beta_last_max_
             ? (num_connections_ - 1 + kBetaLastMax) / num_connections_
             : kBetaLastMax;
}

void Cubic::ResetCubicState() {
  epoch_ = QuicTime::Zero();  // Reset time.
  app_limited_start_time_ = QuicTime::Zero();
  last_update_time_ = QuicTime::Zero();  // Reset time.
  last_congestion_window_ = 0;
  last_max_congestion_window_ = 0;
  acked_packets_count_ = 0;
  epoch_packets_count_ = 0;
  estimated_tcp_congestion_window_ = 0;
  origin_point_congestion_window_ = 0;
  time_to_origin_point_ = 0;
  last_target_congestion_window_ = 0;
}

void Cubic::OnApplicationLimited() {
  // When sender is not using the available congestion window, Cubic's epoch
  // should not continue growing. Reset the epoch when in such a period.
  epoch_ = QuicTime::Zero();
}

void Cubic::SetFixConvexMode(bool fix_convex_mode) {
  fix_convex_mode_ = fix_convex_mode;
}

void Cubic::SetFixBetaLastMax(bool fix_beta_last_max) {
  fix_beta_last_max_ = fix_beta_last_max;
}

void Cubic::SetAllowPerAckUpdates(bool allow_per_ack_updates) {
  allow_per_ack_updates_ = allow_per_ack_updates;
}

QuicPacketCount Cubic::CongestionWindowAfterPacketLoss(
    QuicPacketCount current_congestion_window) {
  if (current_congestion_window < last_max_congestion_window_) {
    // We never reached the old max, so assume we are competing with another
    // flow. Use our extra back off factor to allow the other flow to go up.
    last_max_congestion_window_ =
        static_cast<int>(BetaLastMax() * current_congestion_window);
  } else {
    last_max_congestion_window_ = current_congestion_window;
  }
  epoch_ = QuicTime::Zero();  // Reset time.
  return static_cast<int>(current_congestion_window * Beta());
}

QuicPacketCount Cubic::CongestionWindowAfterAck(
    QuicPacketCount current_congestion_window,
    QuicTime::Delta delay_min,
    QuicTime event_time) {
  acked_packets_count_ += 1;  // Packets acked.
  epoch_packets_count_ += 1;
  // Cubic is "independent" of RTT, the update is limited by the time elapsed.
  if (!allow_per_ack_updates_ &&
      last_congestion_window_ == current_congestion_window &&
      (event_time - last_update_time_ <= MaxCubicTimeInterval())) {
    return std::max(last_target_congestion_window_,
                    estimated_tcp_congestion_window_);
  }
  last_congestion_window_ = current_congestion_window;
  last_update_time_ = event_time;

  if (!epoch_.IsInitialized()) {
    // First ACK after a loss event.
    epoch_ = event_time;       // Start of epoch.
    acked_packets_count_ = 1;  // Reset count.
    epoch_packets_count_ = 1;
    // Reset estimated_tcp_congestion_window_ to be in sync with cubic.
    estimated_tcp_congestion_window_ = current_congestion_window;
    if (last_max_congestion_window_ <= current_congestion_window) {
      time_to_origin_point_ = 0;
      origin_point_congestion_window_ = current_congestion_window;
    } else {
      time_to_origin_point_ = static_cast<uint32_t>(
          cbrt(kCubeFactor *
               (last_max_congestion_window_ - current_congestion_window)));
      origin_point_congestion_window_ = last_max_congestion_window_;
    }
  }

  // Change the time unit from microseconds to 2^10 fractions per second. Take
  // the round trip time in account. This is done to allow us to use shift as a
  // divide operator.
  const int64_t elapsed_time =
      ((event_time + delay_min - epoch_).ToMicroseconds() << 10) /
      kNumMicrosPerSecond;
  DCHECK_GE(elapsed_time, 0);

  int64_t offset = time_to_origin_point_ - elapsed_time;
  if (fix_convex_mode_) {
    // Right-shifts of negative, signed numbers have
    // implementation-dependent behavior.  Force the offset to be
    // positive, similar to the kernel implementation.
    offset = std::abs(time_to_origin_point_ - elapsed_time);
  }

  QuicPacketCount delta_congestion_window =
      (kCubeCongestionWindowScale * offset * offset * offset) >> kCubeScale;

  const bool add_delta = elapsed_time > time_to_origin_point_;
  DCHECK(add_delta ||
         (origin_point_congestion_window_ > delta_congestion_window));
  QuicPacketCount target_congestion_window =
      (fix_convex_mode_ && add_delta)
          ? origin_point_congestion_window_ + delta_congestion_window
          : origin_point_congestion_window_ - delta_congestion_window;

  // Limit the CWND increase to half the acked packets rounded up to the
  // nearest packet.
  target_congestion_window =
      std::min(target_congestion_window,
               current_congestion_window + (epoch_packets_count_ + 1) / 2);

  DCHECK_LT(0u, estimated_tcp_congestion_window_);
  // With dynamic beta/alpha based on number of active streams, it is possible
  // for the required_ack_count to become much lower than acked_packets_count_
  // suddenly, leading to more than one iteration through the following loop.
  while (true) {
    // Update estimated TCP congestion_window.
    QuicPacketCount required_ack_count = static_cast<QuicPacketCount>(
        estimated_tcp_congestion_window_ / Alpha());
    if (acked_packets_count_ < required_ack_count) {
      break;
    }
    acked_packets_count_ -= required_ack_count;
    estimated_tcp_congestion_window_++;
  }
  epoch_packets_count_ = 0;

  // We have a new cubic congestion window.
  last_target_congestion_window_ = target_congestion_window;

  // Compute target congestion_window based on cubic target and estimated TCP
  // congestion_window, use highest (fastest).
  if (target_congestion_window < estimated_tcp_congestion_window_) {
    target_congestion_window = estimated_tcp_congestion_window_;
  }

  QUIC_DVLOG(1) << "Final target congestion_window: "
                << target_congestion_window;
  return target_congestion_window;
}

}  // namespace net
