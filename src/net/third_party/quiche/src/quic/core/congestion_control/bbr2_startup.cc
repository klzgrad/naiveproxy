// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_startup.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_sender.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

Bbr2StartupMode::Bbr2StartupMode(const Bbr2Sender* sender,
                                 Bbr2NetworkModel* model,
                                 QuicTime now)
    : Bbr2ModeBase(sender, model),
      full_bandwidth_reached_(false),
      full_bandwidth_baseline_(QuicBandwidth::Zero()),
      rounds_without_bandwidth_growth_(0) {
  // Clear some startup stats if |sender_->connection_stats_| has been used by
  // another sender, which happens e.g. when QuicConnection switch send
  // algorithms.
  sender_->connection_stats_->slowstart_count = 1;
  sender_->connection_stats_->slowstart_duration = QuicTimeAccumulator();
  sender_->connection_stats_->slowstart_duration.Start(now);
}

void Bbr2StartupMode::Enter(QuicTime /*now*/,
                            const Bbr2CongestionEvent* /*congestion_event*/) {
  QUIC_BUG << "Bbr2StartupMode::Enter should not be called";
}

void Bbr2StartupMode::Leave(QuicTime now,
                            const Bbr2CongestionEvent* /*congestion_event*/) {
  sender_->connection_stats_->slowstart_duration.Stop(now);
}

Bbr2Mode Bbr2StartupMode::OnCongestionEvent(
    QuicByteCount /*prior_in_flight*/,
    QuicTime /*event_time*/,
    const AckedPacketVector& /*acked_packets*/,
    const LostPacketVector& /*lost_packets*/,
    const Bbr2CongestionEvent& congestion_event) {
  if (!full_bandwidth_reached_ && congestion_event.end_of_round_trip) {
    // TCP BBR always exits upon excessive losses. QUIC BBRv1 does not exits
    // upon excessive losses, if enough bandwidth growth is observed.
    bool has_enough_bw_growth = CheckBandwidthGrowth(congestion_event);

    if (Params().always_exit_startup_on_excess_loss || !has_enough_bw_growth) {
      CheckExcessiveLosses(congestion_event);
    }
  }

  model_->set_pacing_gain(Params().startup_pacing_gain);
  model_->set_cwnd_gain(Params().startup_cwnd_gain);

  // TODO(wub): Maybe implement STARTUP => PROBE_RTT.
  return full_bandwidth_reached_ ? Bbr2Mode::DRAIN : Bbr2Mode::STARTUP;
}

bool Bbr2StartupMode::CheckBandwidthGrowth(
    const Bbr2CongestionEvent& congestion_event) {
  DCHECK(!full_bandwidth_reached_);
  DCHECK(congestion_event.end_of_round_trip);
  if (congestion_event.last_sample_is_app_limited) {
    // Return true such that when Params().always_exit_startup_on_excess_loss is
    // false, we'll not check excess loss, which is the behavior of QUIC BBRv1.
    return true;
  }

  QuicBandwidth threshold =
      full_bandwidth_baseline_ * Params().startup_full_bw_threshold;

  if (model_->MaxBandwidth() >= threshold) {
    QUIC_DVLOG(3) << sender_
                  << " CheckBandwidthGrowth at end of round. max_bandwidth:"
                  << model_->MaxBandwidth() << ", threshold:" << threshold
                  << " (Still growing)  @ " << congestion_event.event_time;
    full_bandwidth_baseline_ = model_->MaxBandwidth();
    rounds_without_bandwidth_growth_ = 0;
    return true;
  }

  ++rounds_without_bandwidth_growth_;
  full_bandwidth_reached_ =
      rounds_without_bandwidth_growth_ >= Params().startup_full_bw_rounds;
  QUIC_DVLOG(3) << sender_
                << " CheckBandwidthGrowth at end of round. max_bandwidth:"
                << model_->MaxBandwidth() << ", threshold:" << threshold
                << " rounds_without_growth:" << rounds_without_bandwidth_growth_
                << " full_bw_reached:" << full_bandwidth_reached_ << "  @ "
                << congestion_event.event_time;

  return false;
}

void Bbr2StartupMode::CheckExcessiveLosses(
    const Bbr2CongestionEvent& congestion_event) {
  DCHECK(congestion_event.end_of_round_trip);

  if (full_bandwidth_reached_) {
    return;
  }

  // At the end of a round trip. Check if loss is too high in this round.
  if (model_->IsInflightTooHigh(congestion_event,
                                Params().startup_full_loss_count)) {
    QuicByteCount new_inflight_hi = model_->BDP(model_->MaxBandwidth());
    if (Params().startup_loss_exit_use_max_delivered_for_inflight_hi) {
      if (new_inflight_hi < model_->max_bytes_delivered_in_round()) {
        QUIC_RELOADABLE_FLAG_COUNT_N(
            quic_bbr2_startup_loss_exit_use_max_delivered, 1, 2);
        new_inflight_hi = model_->max_bytes_delivered_in_round();
      } else {
        QUIC_RELOADABLE_FLAG_COUNT_N(
            quic_bbr2_startup_loss_exit_use_max_delivered, 2, 2);
      }
    }
    QUIC_DVLOG(3) << sender_ << " Exiting STARTUP due to loss. inflight_hi:"
                  << new_inflight_hi;
    // TODO(ianswett): Add a shared method to set inflight_hi in the model.
    model_->set_inflight_hi(new_inflight_hi);

    full_bandwidth_reached_ = true;
    sender_->connection_stats_->bbr_exit_startup_due_to_loss = true;
  }
}

Bbr2StartupMode::DebugState Bbr2StartupMode::ExportDebugState() const {
  DebugState s;
  s.full_bandwidth_reached = full_bandwidth_reached_;
  s.full_bandwidth_baseline = full_bandwidth_baseline_;
  s.round_trips_without_bandwidth_growth = rounds_without_bandwidth_growth_;
  return s;
}

std::ostream& operator<<(std::ostream& os,
                         const Bbr2StartupMode::DebugState& state) {
  os << "[STARTUP] full_bandwidth_reached: " << state.full_bandwidth_reached
     << "\n";
  os << "[STARTUP] full_bandwidth_baseline: " << state.full_bandwidth_baseline
     << "\n";
  os << "[STARTUP] round_trips_without_bandwidth_growth: "
     << state.round_trips_without_bandwidth_growth << "\n";
  return os;
}

const Bbr2Params& Bbr2StartupMode::Params() const {
  return sender_->Params();
}

}  // namespace quic
