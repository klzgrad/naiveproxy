// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_probe_rtt.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_sender.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

void Bbr2ProbeRttMode::Enter(QuicTime /*now*/,
                             const Bbr2CongestionEvent* /*congestion_event*/) {
  model_->set_pacing_gain(1.0);
  model_->set_cwnd_gain(1.0);
  exit_time_ = QuicTime::Zero();
}

Bbr2Mode Bbr2ProbeRttMode::OnCongestionEvent(
    QuicByteCount /*prior_in_flight*/,
    QuicTime /*event_time*/,
    const AckedPacketVector& /*acked_packets*/,
    const LostPacketVector& /*lost_packets*/,
    const Bbr2CongestionEvent& congestion_event) {
  if (exit_time_ == QuicTime::Zero()) {
    if (congestion_event.bytes_in_flight <= InflightTarget() ||
        congestion_event.bytes_in_flight <=
            sender_->GetMinimumCongestionWindow()) {
      exit_time_ = congestion_event.event_time + Params().probe_rtt_duration;
      QUIC_DVLOG(2) << sender_ << " PROBE_RTT exit time set to " << exit_time_
                    << ". bytes_inflight:" << congestion_event.bytes_in_flight
                    << ", inflight_target:" << InflightTarget()
                    << ", min_congestion_window:"
                    << sender_->GetMinimumCongestionWindow() << "  @ "
                    << congestion_event.event_time;
    }
    return Bbr2Mode::PROBE_RTT;
  }

  return congestion_event.event_time > exit_time_ ? Bbr2Mode::PROBE_BW
                                                  : Bbr2Mode::PROBE_RTT;
}

QuicByteCount Bbr2ProbeRttMode::InflightTarget() const {
  return model_->BDP(model_->MaxBandwidth(),
                     Params().probe_rtt_inflight_target_bdp_fraction);
}

Limits<QuicByteCount> Bbr2ProbeRttMode::GetCwndLimits() const {
  QuicByteCount inflight_upper_bound =
      std::min(model_->inflight_lo(), model_->inflight_hi_with_headroom());
  return NoGreaterThan(std::min(inflight_upper_bound, InflightTarget()));
}

Bbr2Mode Bbr2ProbeRttMode::OnExitQuiescence(
    QuicTime now,
    QuicTime /*quiescence_start_time*/) {
  if (now > exit_time_) {
    return Bbr2Mode::PROBE_BW;
  }
  return Bbr2Mode::PROBE_RTT;
}

Bbr2ProbeRttMode::DebugState Bbr2ProbeRttMode::ExportDebugState() const {
  DebugState s;
  s.inflight_target = InflightTarget();
  s.exit_time = exit_time_;
  return s;
}

std::ostream& operator<<(std::ostream& os,
                         const Bbr2ProbeRttMode::DebugState& state) {
  os << "[PROBE_RTT] inflight_target: " << state.inflight_target << "\n";
  os << "[PROBE_RTT] exit_time: " << state.exit_time << "\n";
  return os;
}

const Bbr2Params& Bbr2ProbeRttMode::Params() const {
  return sender_->Params();
}

}  // namespace quic
