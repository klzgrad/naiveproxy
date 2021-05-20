// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/congestion_control/bbr2_startup.h"

#include "quic/core/congestion_control/bbr2_misc.h"
#include "quic/core/congestion_control/bbr2_sender.h"
#include "quic/core/quic_bandwidth.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

Bbr2StartupMode::Bbr2StartupMode(const Bbr2Sender* sender,
                                 Bbr2NetworkModel* model,
                                 QuicTime now)
    : Bbr2ModeBase(sender, model) {
  // Clear some startup stats if |sender_->connection_stats_| has been used by
  // another sender, which happens e.g. when QuicConnection switch send
  // algorithms.
  sender_->connection_stats_->slowstart_count = 1;
  sender_->connection_stats_->slowstart_duration = QuicTimeAccumulator();
  sender_->connection_stats_->slowstart_duration.Start(now);
  if (sender->Params().bw_startup) {
    // Enter() is never called for Startup, so the gains needs to be set here.
    model_->set_pacing_gain(Params().startup_pacing_gain);
    model_->set_cwnd_gain(Params().startup_cwnd_gain);
  }
}

void Bbr2StartupMode::Enter(QuicTime /*now*/,
                            const Bbr2CongestionEvent* /*congestion_event*/) {
  QUIC_BUG << "Bbr2StartupMode::Enter should not be called";
}

void Bbr2StartupMode::Leave(QuicTime now,
                            const Bbr2CongestionEvent* /*congestion_event*/) {
  sender_->connection_stats_->slowstart_duration.Stop(now);
  // Clear bandwidth_lo if it's set during STARTUP.
  model_->clear_bandwidth_lo();
}

Bbr2Mode Bbr2StartupMode::OnCongestionEvent(
    QuicByteCount /*prior_in_flight*/,
    QuicTime /*event_time*/,
    const AckedPacketVector& /*acked_packets*/,
    const LostPacketVector& /*lost_packets*/,
    const Bbr2CongestionEvent& congestion_event) {
  if (!model_->full_bandwidth_reached() && congestion_event.end_of_round_trip) {
    // TCP BBR always exits upon excessive losses. QUIC BBRv1 does not exits
    // upon excessive losses, if enough bandwidth growth is observed.
    Bbr2NetworkModel::BandwidthGrowth bw_growth =
        model_->CheckBandwidthGrowth(congestion_event);

    if (Params().always_exit_startup_on_excess_loss ||
        (bw_growth == Bbr2NetworkModel::NO_GROWTH ||
         bw_growth == Bbr2NetworkModel::EXIT)) {
      CheckExcessiveLosses(congestion_event);
    }
  }

  if (Params().decrease_startup_pacing_at_end_of_round) {
    QUICHE_DCHECK_GT(model_->pacing_gain(), 0);
    QUICHE_DCHECK(Params().bw_startup);
    if (congestion_event.end_of_round_trip &&
        !congestion_event.last_sample_is_app_limited) {
      // Multiply by startup_pacing_gain, so if the bandwidth doubles,
      // the pacing gain will be the full startup_pacing_gain.
      if (max_bw_at_round_beginning_ > QuicBandwidth::Zero()) {
        const float bandwidth_ratio =
            std::max(1., model_->MaxBandwidth().ToBitsPerSecond() /
                             static_cast<double>(
                                 max_bw_at_round_beginning_.ToBitsPerSecond()));
        // Even when bandwidth isn't increasing, use a gain large enough to
        // cause a startup_full_bw_threshold increase.
        const float new_gain =
            ((bandwidth_ratio - 1) * (Params().startup_pacing_gain -
                                      Params().startup_full_bw_threshold)) +
            Params().startup_full_bw_threshold;
        // Allow the pacing gain to decrease.
        model_->set_pacing_gain(
            std::min(Params().startup_pacing_gain, new_gain));
        // Clear bandwidth_lo if it's less than the pacing rate.
        // This avoids a constantly app-limited flow from having it's pacing
        // gain effectively decreased below 1.25.
        if (model_->bandwidth_lo() <
            model_->MaxBandwidth() * model_->pacing_gain()) {
          model_->clear_bandwidth_lo();
        }
      }
      max_bw_at_round_beginning_ = model_->MaxBandwidth();
    }
  } else if (!Params().bw_startup) {
    // When the flag is enabled, set these in the constructor.
    model_->set_pacing_gain(Params().startup_pacing_gain);
    model_->set_cwnd_gain(Params().startup_cwnd_gain);
  }

  // TODO(wub): Maybe implement STARTUP => PROBE_RTT.
  return model_->full_bandwidth_reached() ? Bbr2Mode::DRAIN : Bbr2Mode::STARTUP;
}

void Bbr2StartupMode::CheckExcessiveLosses(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK(congestion_event.end_of_round_trip);

  if (model_->full_bandwidth_reached()) {
    return;
  }

  // At the end of a round trip. Check if loss is too high in this round.
  if (model_->IsInflightTooHigh(congestion_event,
                                Params().startup_full_loss_count)) {
    QuicByteCount new_inflight_hi = model_->BDP();
    if (Params().startup_loss_exit_use_max_delivered_for_inflight_hi) {
      if (new_inflight_hi < model_->max_bytes_delivered_in_round()) {
        new_inflight_hi = model_->max_bytes_delivered_in_round();
      }
    }
    QUIC_DVLOG(3) << sender_ << " Exiting STARTUP due to loss. inflight_hi:"
                  << new_inflight_hi;
    // TODO(ianswett): Add a shared method to set inflight_hi in the model.
    model_->set_inflight_hi(new_inflight_hi);
    model_->set_full_bandwidth_reached();
    sender_->connection_stats_->bbr_exit_startup_due_to_loss = true;
  }
}

Bbr2StartupMode::DebugState Bbr2StartupMode::ExportDebugState() const {
  DebugState s;
  s.full_bandwidth_reached = model_->full_bandwidth_reached();
  s.full_bandwidth_baseline = model_->full_bandwidth_baseline();
  s.round_trips_without_bandwidth_growth =
      model_->rounds_without_bandwidth_growth();
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
