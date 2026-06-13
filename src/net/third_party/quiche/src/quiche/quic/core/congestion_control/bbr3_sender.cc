// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/congestion_control/bbr3_sender.h"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>

#include "quiche/quic/core/congestion_control/bandwidth_sampler.h"
#include "quiche/quic/core/congestion_control/bbr2_misc.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_tag.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/print_elements.h"

namespace quic {

namespace {

const int kMaxModeChangesPerCongestionEvent = 4;
}  // namespace

Bbr3Sender::Bbr3Sender(QuicTime now, const RttStats* rtt_stats,
                       const QuicUnackedPacketMap* unacked_packets,
                       QuicPacketCount initial_cwnd_in_packets,
                       QuicPacketCount max_cwnd_in_packets, QuicRandom* random,
                       QuicConnectionStats* stats, BbrSender* old_sender)
    : mode_(Bbr2Mode::STARTUP),
      unacked_packets_(unacked_packets),
      random_(random),
      connection_stats_(stats),
      params_(kDefaultMinimumCongestionWindow,
              max_cwnd_in_packets * kDefaultTCPMSS),
      model_(&params_, rtt_stats->SmoothedOrInitialRtt(),
             rtt_stats->last_update_time(),
             old_sender ? &old_sender->sampler_ : nullptr),
      initial_cwnd_(params_.cwnd_limits.ApplyLimits(
          (old_sender) ? old_sender->GetCongestionWindow()
                       : (initial_cwnd_in_packets * kDefaultTCPMSS))),
      cwnd_(initial_cwnd_),
      pacing_rate_(params_.startup_pacing_gain *
                   QuicBandwidth::FromBytesAndTimeDelta(
                       cwnd_, rtt_stats->SmoothedOrInitialRtt())),
      last_sample_is_app_limited_(false) {
  // Increment, instead of reset startup stats, so we don't lose data recorded
  // before QuicConnection switched send algorithm to BBRv2.
  ++connection_stats_->slowstart_count;
  if (!connection_stats_->slowstart_duration.IsRunning()) {
    connection_stats_->slowstart_duration.Start(now);
  }
  // Enter() is never called for Startup, so the gains needs to be set here.
  model_.set_pacing_gain(params_.startup_pacing_gain);
  model_.set_cwnd_gain(params_.startup_cwnd_gain);
  QUIC_DVLOG(2) << this << " Initializing Bbr3Sender. mode:" << mode_
                << ", PacingRate:" << pacing_rate_ << ", Cwnd:" << cwnd_
                << ", CwndLimits:" << params_.cwnd_limits << "  @ " << now;
  QUICHE_DCHECK_EQ(mode_, Bbr2Mode::STARTUP);
}

void Bbr3Sender::SetFromConfig(const QuicConfig& config,
                               Perspective perspective) {
  if (config.HasClientRequestedIndependentOption(kB2NA, perspective)) {
    params_.add_ack_height_to_queueing_threshold = false;
  }
  if (config.HasClientRequestedIndependentOption(kB2RP, perspective)) {
    params_.avoid_unnecessary_probe_rtt = false;
  }
  if (config.HasClientRequestedIndependentOption(k1RTT, perspective)) {
    params_.startup_full_bw_rounds = 1;
  }
  if (config.HasClientRequestedIndependentOption(k2RTT, perspective)) {
    params_.startup_full_bw_rounds = 2;
  }
  if (config.HasClientRequestedIndependentOption(kB2HR, perspective)) {
    params_.inflight_hi_headroom = 0.15;
  }
  if (config.HasClientRequestedIndependentOption(kICW1, perspective)) {
    max_cwnd_when_network_parameters_adjusted_ = 100 * kDefaultTCPMSS;
  }

  ApplyConnectionOptions(config.ClientRequestedIndependentOptions(perspective));
}

void Bbr3Sender::ApplyConnectionOptions(
    const QuicTagVector& connection_options) {
  if (GetQuicReloadableFlag(quic_bbr2_extra_acked_window) &&
      ContainsQuicTag(connection_options, kBBR4)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_extra_acked_window, 1, 2);
    model_.SetMaxAckHeightTrackerWindowLength(20);
  }
  if (GetQuicReloadableFlag(quic_bbr2_extra_acked_window) &&
      ContainsQuicTag(connection_options, kBBR5)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_extra_acked_window, 2, 2);
    model_.SetMaxAckHeightTrackerWindowLength(40);
  }
  if (ContainsQuicTag(connection_options, kBBQ1)) {
    params_.startup_pacing_gain = 2.773;
    params_.drain_pacing_gain = 1.0 / params_.drain_cwnd_gain;
  }
  if (ContainsQuicTag(connection_options, kBBQ2)) {
    params_.startup_cwnd_gain = 2.885;
    params_.drain_cwnd_gain = 2.885;
    model_.set_cwnd_gain(params_.startup_cwnd_gain);
  }
  if (ContainsQuicTag(connection_options, kB2LO)) {
    params_.ignore_inflight_lo = true;
  }
  if (ContainsQuicTag(connection_options, kB2NE)) {
    params_.always_exit_startup_on_excess_loss = true;
  }
  if (ContainsQuicTag(connection_options, kB2SL)) {
    params_.startup_loss_exit_use_max_delivered_for_inflight_hi = false;
  }
  if (ContainsQuicTag(connection_options, kB2H2)) {
    params_.limit_inflight_hi_by_max_delivered = true;
  }
  if (ContainsQuicTag(connection_options, kB2DL)) {
    params_.use_bytes_delivered_for_inflight_hi = true;
  }
  if (ContainsQuicTag(connection_options, kB2RC)) {
    params_.enable_reno_coexistence = false;
  }
  if (ContainsQuicTag(connection_options, kBSAO)) {
    model_.EnableOverestimateAvoidance();
  }
  if (ContainsQuicTag(connection_options, kBBQ6)) {
    params_.decrease_startup_pacing_at_end_of_round = true;
  }
  if (ContainsQuicTag(connection_options, kBBQ7)) {
    params_.bw_lo_mode_ = Bbr2Params::QuicBandwidthLoMode::MIN_RTT_REDUCTION;
  }
  if (ContainsQuicTag(connection_options, kBBQ8)) {
    params_.bw_lo_mode_ = Bbr2Params::QuicBandwidthLoMode::INFLIGHT_REDUCTION;
  }
  if (ContainsQuicTag(connection_options, kBBQ9)) {
    params_.bw_lo_mode_ = Bbr2Params::QuicBandwidthLoMode::CWND_REDUCTION;
  }
  if (ContainsQuicTag(connection_options, kB202)) {
    params_.max_probe_up_queue_rounds = 1;
  }
  if (ContainsQuicTag(connection_options, kB203)) {
    params_.probe_up_ignore_inflight_hi = false;
  }
  if (ContainsQuicTag(connection_options, kB204)) {
    model_.SetReduceExtraAckedOnBandwidthIncrease(true);
  }
  if (ContainsQuicTag(connection_options, kB205)) {
    params_.startup_include_extra_acked = true;
  }
  if (ContainsQuicTag(connection_options, kB207)) {
    params_.max_startup_queue_rounds = 1;
  }
  if (ContainsQuicTag(connection_options, kBBRA)) {
    model_.SetStartNewAggregationEpochAfterFullRound(true);
  }
  if (ContainsQuicTag(connection_options, kBBRB)) {
    model_.SetLimitMaxAckHeightTrackerBySendRate(true);
  }
  if (ContainsQuicTag(connection_options, kADP0)) {
    model_.SetEnableAppDrivenPacing(true);
  }
  if (ContainsQuicTag(connection_options, kB206)) {
    params_.startup_full_loss_count = params_.probe_bw_full_loss_count;
  }
  if (ContainsQuicTag(connection_options, kBBHI)) {
    params_.probe_up_simplify_inflight_hi = true;
    // Simplify inflight_hi is intended as an alternative to ignoring it,
    // so ensure we're not ignoring it.
    params_.probe_up_ignore_inflight_hi = false;
  }
  if (ContainsQuicTag(connection_options, kBB2U)) {
    params_.max_probe_up_queue_rounds = 2;
  }
  if (ContainsQuicTag(connection_options, kBB2S)) {
    params_.max_startup_queue_rounds = 2;
  }
}

Limits<QuicByteCount> Bbr3Sender::GetCwndLimitsByMode() const {
  switch (mode_) {
    case Bbr2Mode::STARTUP:
      // Inflight_lo is never set in STARTUP.
      QUICHE_DCHECK_EQ(Bbr2NetworkModel::inflight_lo_default(),
                       model_.inflight_lo());
      return NoGreaterThan(model_.inflight_lo());
    case Bbr2Mode::PROBE_BW: {
      if (probe_bw_.phase == ProbePhase::PROBE_CRUISE) {
        return NoGreaterThan(
            std::min(model_.inflight_lo(), model_.inflight_hi_with_headroom()));
      }
      if (params_.probe_up_ignore_inflight_hi &&
          probe_bw_.phase == ProbePhase::PROBE_UP) {
        // Similar to STARTUP.
        return NoGreaterThan(model_.inflight_lo());
      }
      return NoGreaterThan(
          std::min(model_.inflight_lo(), model_.inflight_hi()));
    }
    case Bbr2Mode::DRAIN:
      return NoGreaterThan(model_.inflight_lo());
    case Bbr2Mode::PROBE_RTT: {
      QuicByteCount inflight_upper_bound =
          std::min(model_.inflight_lo(), model_.inflight_hi_with_headroom());
      return NoGreaterThan(std::min(inflight_upper_bound, InflightTarget()));
    }
    default:
      QUICHE_NOTREACHED();
      return Unlimited<QuicByteCount>();
  }
}

void Bbr3Sender::AdjustNetworkParameters(const NetworkParams& params) {
  model_.UpdateNetworkParameters(params.rtt);

  if (mode_ == Bbr2Mode::STARTUP) {
    const QuicByteCount prior_cwnd = cwnd_;

    QuicBandwidth effective_bandwidth =
        std::max(params.bandwidth, model_.BandwidthEstimate());
    connection_stats_->cwnd_bootstrapping_rtt_us =
        model_.MinRtt().ToMicroseconds();

    if (params.max_initial_congestion_window > 0) {
      max_cwnd_when_network_parameters_adjusted_ =
          params.max_initial_congestion_window * kDefaultTCPMSS;
    }
    cwnd_ = params_.cwnd_limits.ApplyLimits(
        std::min(max_cwnd_when_network_parameters_adjusted_,
                 model_.BDP(effective_bandwidth)));

    if (!params.allow_cwnd_to_decrease) {
      cwnd_ = std::max(cwnd_, prior_cwnd);
    }

    pacing_rate_ = std::max(pacing_rate_, QuicBandwidth::FromBytesAndTimeDelta(
                                              cwnd_, model_.MinRtt()));
  }
}

void Bbr3Sender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  if (mode_ == Bbr2Mode::STARTUP) {
    // The cwnd limits is unchanged and still applies to the new cwnd.
    cwnd_ = params_.cwnd_limits.ApplyLimits(congestion_window * kDefaultTCPMSS);
  }
}

void Bbr3Sender::SetApplicationDrivenPacingRate(
    QuicBandwidth application_bandwidth_target) {
  QUIC_CODE_COUNT(quic_bbr2_set_app_driven_pacing_rate);
  model_.SetApplicationBandwidthTarget(application_bandwidth_target);
}

void Bbr3Sender::OnCongestionEvent(bool /*rtt_updated*/,
                                   QuicByteCount prior_in_flight,
                                   QuicTime event_time,
                                   const AckedPacketVector& acked_packets,
                                   const LostPacketVector& lost_packets,
                                   QuicPacketCount /*num_ect*/,
                                   QuicPacketCount /*num_ce*/) {
  QUIC_DVLOG(3) << this
                << " OnCongestionEvent. prior_in_flight:" << prior_in_flight
                << " prior_cwnd:" << cwnd_ << "  @ " << event_time;
  Bbr2CongestionEvent congestion_event;
  congestion_event.prior_cwnd = cwnd_;
  congestion_event.prior_bytes_in_flight = prior_in_flight;
  bool is_probing_for_bandwidth = false;
  if (mode_ == Bbr2Mode::STARTUP) {
    is_probing_for_bandwidth = true;
  } else if (mode_ == Bbr2Mode::PROBE_BW) {
    is_probing_for_bandwidth = probe_bw_.phase == ProbePhase::PROBE_REFILL ||
                               probe_bw_.phase == ProbePhase::PROBE_UP;
  }
  congestion_event.is_probing_for_bandwidth = is_probing_for_bandwidth;

  model_.OnCongestionEventStart(event_time, acked_packets, lost_packets,
                                &congestion_event);

  if (InSlowStart()) {
    if (!lost_packets.empty()) {
      connection_stats_->slowstart_packets_lost += lost_packets.size();
      connection_stats_->slowstart_bytes_lost += congestion_event.bytes_lost;
    }
    if (congestion_event.end_of_round_trip) {
      ++connection_stats_->slowstart_num_rtts;
    }
  }

  // Number of mode changes allowed for this congestion event.
  int mode_changes_allowed = kMaxModeChangesPerCongestionEvent;
  while (true) {
    Bbr2Mode prev_mode = mode_;
    switch (mode_) {
      case Bbr2Mode::STARTUP:
        mode_ = OnCongestionEventStartup(congestion_event);
        break;
      case Bbr2Mode::DRAIN:
        mode_ = OnCongestionEventDrain(congestion_event);
        break;
      case Bbr2Mode::PROBE_BW:
        mode_ = OnCongestionEventProbeBw(prior_in_flight, event_time,
                                         congestion_event);
        break;
      case Bbr2Mode::PROBE_RTT:
        mode_ = OnCongestionEventProbeRtt(congestion_event);
        break;
    }

    if (mode_ == prev_mode) {
      break;
    }

    QUIC_DVLOG(2) << this << " Mode change:  " << prev_mode << " ==> " << mode_
                  << "  @ " << event_time;

    if (prev_mode == Bbr2Mode::STARTUP) {
      LeaveStartup(event_time);
    }

    if (mode_ == Bbr2Mode::PROBE_BW) {
      if (probe_bw_.phase == ProbePhase::PROBE_NOT_STARTED) {
        EnterProbeDown(/*probed_too_high=*/false, /*stopped_risky_probe=*/false,
                       event_time);
      } else {
        QUICHE_DCHECK(probe_bw_.phase == ProbePhase::PROBE_CRUISE ||
                      probe_bw_.phase == ProbePhase::PROBE_REFILL);
        probe_bw_.cycle_start_time = event_time;
        if (probe_bw_.phase == ProbePhase::PROBE_CRUISE) {
          EnterProbeCruise(event_time);
        } else if (probe_bw_.phase == ProbePhase::PROBE_REFILL) {
          EnterProbeRefill(probe_bw_.probe_up_rounds, event_time);
        }
      }
    } else if (mode_ == Bbr2Mode::PROBE_RTT) {
      EnterProbeRtt();
    }

    --mode_changes_allowed;
    if (mode_changes_allowed < 0) {
      QUIC_BUG(quic_bug_10443_1)
          << "Exceeded max number of mode changes per congestion event.";
      break;
    }
  }

  UpdatePacingRate(congestion_event.bytes_acked);
  QUIC_BUG_IF(quic_bug_10443_2, pacing_rate_.IsZero())
      << "Pacing rate must not be zero!";

  UpdateCongestionWindow(congestion_event.bytes_acked);
  QUIC_BUG_IF(quic_bug_10443_3, cwnd_ == 0u)
      << "Congestion window must not be zero!";

  model_.OnCongestionEventFinish(unacked_packets_->GetLeastUnacked(),
                                 congestion_event);
  last_sample_is_app_limited_ =
      congestion_event.last_packet_send_state.is_app_limited;
  if (!last_sample_is_app_limited_) {
    has_non_app_limited_sample_ = true;
  }
  if (congestion_event.bytes_in_flight == 0 &&
      params_.avoid_unnecessary_probe_rtt) {
    last_quiescence_start_ = event_time;
  }

  QUIC_DVLOG(3)
      << this
      << " END CongestionEvent(acked:" << quiche::PrintElements(acked_packets)
      << ", lost:" << lost_packets.size() << ") "
      << ", Mode:" << mode_ << ", RttCount:" << model_.RoundTripCount()
      << ", BytesInFlight:" << congestion_event.bytes_in_flight
      << ", PacingRate:" << PacingRate(0) << ", CWND:" << GetCongestionWindow()
      << ", PacingGain:" << model_.pacing_gain()
      << ", CwndGain:" << model_.cwnd_gain()
      << ", BandwidthEstimate(kbps):" << BandwidthEstimate().ToKBitsPerSecond()
      << ", MinRTT(us):" << model_.MinRtt().ToMicroseconds()
      << ", BDP:" << model_.BDP(BandwidthEstimate())
      << ", BandwidthLatest(kbps):"
      << model_.bandwidth_latest().ToKBitsPerSecond()
      << ", BandwidthLow(kbps):" << model_.bandwidth_lo().ToKBitsPerSecond()
      << ", BandwidthHigh(kbps):" << model_.MaxBandwidth().ToKBitsPerSecond()
      << ", InflightLatest:" << model_.inflight_latest()
      << ", InflightLow:" << model_.inflight_lo()
      << ", InflightHigh:" << model_.inflight_hi()
      << ", TotalAcked:" << model_.total_bytes_acked()
      << ", TotalLost:" << model_.total_bytes_lost()
      << ", TotalSent:" << model_.total_bytes_sent() << "  @ " << event_time;
}

void Bbr3Sender::UpdatePacingRate(QuicByteCount bytes_acked) {
  if (BandwidthEstimate().IsZero()) {
    return;
  }

  if (model_.total_bytes_acked() == bytes_acked) {
    // After the first ACK, cwnd_ is still the initial congestion window.
    pacing_rate_ = QuicBandwidth::FromBytesAndTimeDelta(cwnd_, model_.MinRtt());
    return;
  }

  QuicBandwidth target_rate = model_.pacing_gain() * model_.BandwidthEstimate();
  if (model_.full_bandwidth_reached()) {
    pacing_rate_ = target_rate;
    return;
  }
  if (params_.decrease_startup_pacing_at_end_of_round &&
      model_.pacing_gain() < Params().startup_pacing_gain) {
    pacing_rate_ = target_rate;
    return;
  }
  if (params_.bw_lo_mode_ != Bbr2Params::DEFAULT &&
      model_.loss_events_in_round() > 0) {
    pacing_rate_ = target_rate;
    return;
  }

  // By default, the pacing rate never decreases in STARTUP.
  if (target_rate > pacing_rate_) {
    pacing_rate_ = target_rate;
  }
}

void Bbr3Sender::UpdateCongestionWindow(QuicByteCount bytes_acked) {
  QuicByteCount target_cwnd = GetTargetCongestionWindow(model_.cwnd_gain());

  const QuicByteCount prior_cwnd = cwnd_;
  if (model_.full_bandwidth_reached() || Params().startup_include_extra_acked) {
    target_cwnd += model_.MaxAckHeight();
    cwnd_ = std::min(prior_cwnd + bytes_acked, target_cwnd);
  } else if (prior_cwnd < target_cwnd || prior_cwnd < 2 * initial_cwnd_) {
    cwnd_ = prior_cwnd + bytes_acked;
  }
  const QuicByteCount desired_cwnd = cwnd_;

  cwnd_ = GetCwndLimitsByMode().ApplyLimits(cwnd_);
  const QuicByteCount model_limited_cwnd = cwnd_;

  cwnd_ = params_.cwnd_limits.ApplyLimits(cwnd_);

  QUIC_DVLOG(3) << this << " Updating CWND. target_cwnd:" << target_cwnd
                << ", max_ack_height:" << model_.MaxAckHeight()
                << ", full_bw:" << model_.full_bandwidth_reached()
                << ", bytes_acked:" << bytes_acked
                << ", inflight_lo:" << model_.inflight_lo()
                << ", inflight_hi:" << model_.inflight_hi() << ". (prior_cwnd) "
                << prior_cwnd << " => (desired_cwnd) " << desired_cwnd
                << " => (model_limited_cwnd) " << model_limited_cwnd
                << " => (final_cwnd) " << cwnd_;
}

QuicByteCount Bbr3Sender::GetTargetCongestionWindow(float gain) const {
  return std::max(model_.BDP(model_.BandwidthEstimate(), gain),
                  params_.cwnd_limits.Min());
}

void Bbr3Sender::OnPacketSent(QuicTime sent_time, QuicByteCount bytes_in_flight,
                              QuicPacketNumber packet_number,
                              QuicByteCount bytes,
                              HasRetransmittableData is_retransmittable) {
  QUIC_DVLOG(3) << this << " OnPacketSent: pkn:" << packet_number
                << ", bytes:" << bytes << ", cwnd:" << cwnd_
                << ", inflight:" << bytes_in_flight + bytes
                << ", total_sent:" << model_.total_bytes_sent() + bytes
                << ", total_acked:" << model_.total_bytes_acked()
                << ", total_lost:" << model_.total_bytes_lost() << "  @ "
                << sent_time;
  if (InSlowStart()) {
    ++connection_stats_->slowstart_packets_sent;
    connection_stats_->slowstart_bytes_sent += bytes;
  }
  if (bytes_in_flight == 0 && params_.avoid_unnecessary_probe_rtt) {
    OnExitQuiescence(sent_time);
  }
  model_.OnPacketSent(sent_time, bytes_in_flight, packet_number, bytes,
                      is_retransmittable);
}

void Bbr3Sender::OnPacketNeutered(QuicPacketNumber packet_number) {
  model_.OnPacketNeutered(packet_number);
}

bool Bbr3Sender::CanSend(QuicByteCount bytes_in_flight) {
  return bytes_in_flight < GetCongestionWindow();
}

QuicByteCount Bbr3Sender::GetCongestionWindow() const {
  // TODO(wub): Implement Recovery?
  return cwnd_;
}

QuicBandwidth Bbr3Sender::PacingRate(QuicByteCount /*bytes_in_flight*/) const {
  return pacing_rate_;
}

void Bbr3Sender::OnApplicationLimited(QuicByteCount bytes_in_flight) {
  if (bytes_in_flight >= GetCongestionWindow()) {
    return;
  }

  model_.OnApplicationLimited();
  QUIC_DVLOG(2) << this << " Becoming application limited. Last sent packet: "
                << model_.last_sent_packet()
                << ", CWND: " << GetCongestionWindow();
}

QuicByteCount Bbr3Sender::GetTargetBytesInflight() const {
  QuicByteCount bdp = model_.BDP(model_.BandwidthEstimate());
  return std::min(bdp, GetCongestionWindow());
}

void Bbr3Sender::PopulateConnectionStats(QuicConnectionStats* stats) const {
  stats->num_ack_aggregation_epochs = model_.num_ack_aggregation_epochs();
}

void Bbr3Sender::LeaveStartup(QuicTime now) {
  connection_stats_->slowstart_duration.Stop(now);
  // Clear bandwidth_lo if it's set during STARTUP.
  model_.clear_bandwidth_lo();
}

void Bbr3Sender::OnExitQuiescence(QuicTime now) {
  if (last_quiescence_start_ == QuicTime::Zero()) {
    return;
  }

  Bbr2Mode prev_mode = mode_;
  switch (mode_) {
    case Bbr2Mode::STARTUP:
    case Bbr2Mode::DRAIN:
      break;
    case Bbr2Mode::PROBE_BW:
      QUIC_DVLOG(3) << this << " Postponing min_rtt_timestamp("
                    << model_.MinRttTimestamp() << ") by "
                    << now - last_quiescence_start_;
      model_.PostponeMinRttTimestamp(now - last_quiescence_start_);
      break;
    case Bbr2Mode::PROBE_RTT:
      if (now > probe_rtt_.exit_time) {
        mode_ = Bbr2Mode::PROBE_BW;
      }
      break;
  }

  if (mode_ != prev_mode) {
    QUICHE_DCHECK_EQ(mode_, Bbr2Mode::PROBE_BW);
    if (probe_bw_.phase == ProbePhase::PROBE_NOT_STARTED) {
      EnterProbeDown(/*probed_too_high=*/false, /*stopped_risky_probe=*/false,
                     now);
    } else {
      QUICHE_DCHECK(probe_bw_.phase == ProbePhase::PROBE_CRUISE ||
                    probe_bw_.phase == ProbePhase::PROBE_REFILL);
      probe_bw_.cycle_start_time = now;
      if (probe_bw_.phase == ProbePhase::PROBE_CRUISE) {
        EnterProbeCruise(now);
      } else if (probe_bw_.phase == ProbePhase::PROBE_REFILL) {
        EnterProbeRefill(probe_bw_.probe_up_rounds, now);
      }
    }
  }
  last_quiescence_start_ = QuicTime::Zero();
}

std::string Bbr3Sender::GetDebugState() const {
  std::ostringstream stream;
  stream << ExportDebugState();
  return stream.str();
}

Bbr2DebugState Bbr3Sender::ExportDebugState() const {
  Bbr2DebugState s;
  s.mode = mode_;
  s.round_trip_count = model_.RoundTripCount();
  s.bandwidth_hi = model_.MaxBandwidth();
  s.bandwidth_lo = model_.bandwidth_lo();
  s.bandwidth_est = BandwidthEstimate();
  s.inflight_hi = model_.inflight_hi();
  s.inflight_lo = model_.inflight_lo();
  s.max_ack_height = model_.MaxAckHeight();
  s.min_rtt = model_.MinRtt();
  s.min_rtt_timestamp = model_.MinRttTimestamp();
  s.congestion_window = cwnd_;
  s.pacing_rate = pacing_rate_;
  s.last_sample_is_app_limited = last_sample_is_app_limited_;
  s.end_of_app_limited_phase = model_.end_of_app_limited_phase();

  s.startup.full_bandwidth_reached = model_.full_bandwidth_reached();
  s.startup.full_bandwidth_baseline = model_.full_bandwidth_baseline();
  s.startup.round_trips_without_bandwidth_growth =
      model_.rounds_without_bandwidth_growth();

  s.drain.drain_target = DrainTarget();

  s.probe_bw.phase = probe_bw_.phase;
  s.probe_bw.cycle_start_time = probe_bw_.cycle_start_time;
  s.probe_bw.phase_start_time = probe_bw_.phase_start_time;

  s.probe_rtt.inflight_target = InflightTarget();
  s.probe_rtt.exit_time = probe_rtt_.exit_time;

  return s;
}

Bbr2Mode Bbr3Sender::OnCongestionEventStartup(
    const Bbr2CongestionEvent& congestion_event) {
  if (model_.full_bandwidth_reached()) {
    QUIC_BUG(quic_bug_10463_2)
        << "In STARTUP, but full_bandwidth_reached is true.";
    return Bbr2Mode::DRAIN;
  }
  if (!congestion_event.end_of_round_trip) {
    return Bbr2Mode::STARTUP;
  }
  bool has_bandwidth_growth = model_.HasBandwidthGrowth(congestion_event);
  if (params_.max_startup_queue_rounds > 0 && !has_bandwidth_growth) {
    // 1.75 is less than the 2x CWND gain, but substantially more than 1.25x,
    // the minimum bandwidth increase expected during STARTUP.
    model_.CheckPersistentQueue(congestion_event, 1.75);
  }
  // TCP BBR always exits upon excessive losses. QUIC BBRv1 does not exit
  // upon excessive losses, if enough bandwidth growth is observed or if the
  // sample was app limited.
  if (params_.always_exit_startup_on_excess_loss ||
      (!congestion_event.last_packet_send_state.is_app_limited &&
       !has_bandwidth_growth)) {
    CheckExcessiveLosses(congestion_event);
  }

  if (params_.decrease_startup_pacing_at_end_of_round) {
    QUICHE_DCHECK_GT(model_.pacing_gain(), 0);
    if (!congestion_event.last_packet_send_state.is_app_limited) {
      // Multiply by startup_pacing_gain, so if the bandwidth doubles,
      // the pacing gain will be the full startup_pacing_gain.
      if (startup_max_bw_at_round_beginning_ > QuicBandwidth::Zero()) {
        const float bandwidth_ratio = std::max(
            1., model_.MaxBandwidth().ToBitsPerSecond() /
                    static_cast<double>(
                        startup_max_bw_at_round_beginning_.ToBitsPerSecond()));
        // Even when bandwidth isn't increasing, use a gain large enough to
        // cause a full_bw_threshold increase.
        const float new_gain =
            ((bandwidth_ratio - 1) *
             (params_.startup_pacing_gain - params_.full_bw_threshold)) +
            params_.full_bw_threshold;
        // Allow the pacing gain to decrease.
        model_.set_pacing_gain(std::min(params_.startup_pacing_gain, new_gain));
        // Clear bandwidth_lo if it's less than the pacing rate.
        // This avoids a constantly app-limited flow from having it's pacing
        // gain effectively decreased below 1.25.
        if (model_.bandwidth_lo() <
            model_.MaxBandwidth() * model_.pacing_gain()) {
          model_.clear_bandwidth_lo();
        }
      }
      startup_max_bw_at_round_beginning_ = model_.MaxBandwidth();
    }
  }

  // TODO(wub): Maybe implement STARTUP => PROBE_RTT.
  return model_.full_bandwidth_reached() ? Bbr2Mode::DRAIN : Bbr2Mode::STARTUP;
}

void Bbr3Sender::CheckExcessiveLosses(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK(congestion_event.end_of_round_trip);

  if (model_.full_bandwidth_reached()) {
    return;
  }

  // At the end of a round trip. Check if loss is too high in this round.
  if (model_.IsInflightTooHigh(congestion_event,
                               params_.startup_full_loss_count)) {
    QuicByteCount new_inflight_hi = model_.BDP();
    if (params_.startup_loss_exit_use_max_delivered_for_inflight_hi) {
      if (new_inflight_hi < model_.max_bytes_delivered_in_round()) {
        new_inflight_hi = model_.max_bytes_delivered_in_round();
      }
    }
    QUIC_DVLOG(3) << this << " Exiting STARTUP due to loss at round "
                  << model_.RoundTripCount()
                  << ". inflight_hi:" << new_inflight_hi;
    // TODO(ianswett): Add a shared method to set inflight_hi in the model.
    model_.set_inflight_hi(new_inflight_hi);
    model_.set_full_bandwidth_reached();
    connection_stats_->bbr_exit_startup_due_to_loss = true;
  }
}

Bbr2Mode Bbr3Sender::OnCongestionEventDrain(
    const Bbr2CongestionEvent& congestion_event) {
  model_.set_pacing_gain(params_.drain_pacing_gain);

  // Only STARTUP can transition to DRAIN, both of them use the same cwnd gain.
  QUICHE_DCHECK_EQ(model_.cwnd_gain(), params_.drain_cwnd_gain);
  model_.set_cwnd_gain(params_.drain_cwnd_gain);

  QuicByteCount drain_target = DrainTarget();
  if (congestion_event.bytes_in_flight <= drain_target) {
    QUIC_DVLOG(3) << this << " Exiting DRAIN. bytes_in_flight:"
                  << congestion_event.bytes_in_flight
                  << ", bdp:" << model_.BDP()
                  << ", drain_target:" << drain_target << "  @ "
                  << congestion_event.event_time;
    return Bbr2Mode::PROBE_BW;
  }

  QUIC_DVLOG(3) << this << " Staying in DRAIN. bytes_in_flight:"
                << congestion_event.bytes_in_flight << ", bdp:" << model_.BDP()
                << ", drain_target:" << drain_target << "  @ "
                << congestion_event.event_time;
  return Bbr2Mode::DRAIN;
}

QuicByteCount Bbr3Sender::DrainTarget() const {
  QuicByteCount bdp = model_.BDP();
  return std::max<QuicByteCount>(bdp, GetMinimumCongestionWindow());
}

Bbr2Mode Bbr3Sender::OnCongestionEventProbeBw(
    QuicByteCount prior_in_flight, QuicTime event_time,
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_NE(probe_bw_.phase, ProbePhase::PROBE_NOT_STARTED);

  if (congestion_event.end_of_round_trip) {
    if (probe_bw_.cycle_start_time != event_time) {
      ++probe_bw_.rounds_since_probe;
    }
    if (probe_bw_.phase_start_time != event_time) {
      ++probe_bw_.rounds_in_phase;
    }
  }

  bool switch_to_probe_rtt = false;

  if (probe_bw_.phase == ProbePhase::PROBE_UP) {
    UpdateProbeUp(congestion_event);
  } else if (probe_bw_.phase == ProbePhase::PROBE_DOWN) {
    UpdateProbeDown(prior_in_flight, congestion_event);
    // Maybe transition to PROBE_RTT at the end of this cycle.
    if (probe_bw_.phase != ProbePhase::PROBE_DOWN &&
        model_.MaybeExpireMinRtt(congestion_event)) {
      switch_to_probe_rtt = true;
    }
  } else if (probe_bw_.phase == ProbePhase::PROBE_CRUISE) {
    UpdateProbeCruise(congestion_event);
  } else if (probe_bw_.phase == ProbePhase::PROBE_REFILL) {
    UpdateProbeRefill(congestion_event);
  }

  // Do not need to set the gains if switching to PROBE_RTT, they will be set
  // when Bbr2ProbeRttMode::Enter is called.
  if (!switch_to_probe_rtt) {
    model_.set_pacing_gain(PacingGainForPhase(probe_bw_.phase));
    model_.set_cwnd_gain(params_.probe_bw_cwnd_gain);
  }

  return switch_to_probe_rtt ? Bbr2Mode::PROBE_RTT : Bbr2Mode::PROBE_BW;
}

void Bbr3Sender::UpdateProbeDown(QuicByteCount prior_in_flight,
                                 const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_DOWN);

  if (probe_bw_.rounds_in_phase == 1 && congestion_event.end_of_round_trip) {
    probe_bw_.is_sample_from_probing = false;

    if (!congestion_event.last_packet_send_state.is_app_limited) {
      QUIC_DVLOG(2)
          << this << " Advancing max bw filter after one round in PROBE_DOWN.";
      model_.AdvanceMaxBandwidthFilter();
      probe_bw_.has_advanced_max_bw = true;
    }

    if (probe_bw_.last_cycle_stopped_risky_probe &&
        !probe_bw_.last_cycle_probed_too_high) {
      EnterProbeRefill(/*probe_up_rounds=*/0, congestion_event.event_time);
      return;
    }
  }

  MaybeAdaptUpperBounds(congestion_event);

  if (IsTimeToProbeBandwidth(congestion_event)) {
    EnterProbeRefill(/*probe_up_rounds=*/0, congestion_event.event_time);
    return;
  }

  if (HasPhaseLasted(model_.MinRtt(), congestion_event)) {
    QUIC_DVLOG(3) << this << " Proportional time based PROBE_DOWN exit";
    EnterProbeCruise(congestion_event.event_time);
    return;
  }

  const QuicByteCount inflight_with_headroom =
      model_.inflight_hi_with_headroom();
  QUIC_DVLOG(3)
      << this << " Checking if have enough inflight headroom. prior_in_flight:"
      << prior_in_flight << " congestion_event.bytes_in_flight:"
      << congestion_event.bytes_in_flight
      << ", inflight_with_headroom:" << inflight_with_headroom;
  QuicByteCount bytes_in_flight = congestion_event.bytes_in_flight;

  if (bytes_in_flight > inflight_with_headroom) {
    // Stay in PROBE_DOWN.
    return;
  }

  // Transition to PROBE_CRUISE iff we've drained to target.
  QuicByteCount bdp = model_.BDP();
  QUIC_DVLOG(3) << this << " Checking if drained to target. bytes_in_flight:"
                << bytes_in_flight << ", bdp:" << bdp;
  if (bytes_in_flight < bdp) {
    EnterProbeCruise(congestion_event.event_time);
  }
}

Bbr3Sender::AdaptUpperBoundsResult Bbr3Sender::MaybeAdaptUpperBounds(
    const Bbr2CongestionEvent& congestion_event) {
  const SendTimeState& send_state = congestion_event.last_packet_send_state;
  if (!send_state.is_valid) {
    QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                  << ": NOT_ADAPTED_INVALID_SAMPLE";
    return NOT_ADAPTED_INVALID_SAMPLE;
  }

  QuicByteCount inflight_at_send = BytesInFlight(send_state);
  if (params_.use_bytes_delivered_for_inflight_hi) {
    if (congestion_event.last_packet_send_state.total_bytes_acked <=
        model_.total_bytes_acked()) {
      inflight_at_send =
          model_.total_bytes_acked() -
          congestion_event.last_packet_send_state.total_bytes_acked;
    } else {
      QUIC_BUG(quic_bug_10463_3)
          << "Total_bytes_acked(" << model_.total_bytes_acked()
          << ") < send_state.total_bytes_acked("
          << congestion_event.last_packet_send_state.total_bytes_acked << ")";
    }
  }
  if (model_.IsInflightTooHigh(congestion_event,
                               params_.probe_bw_full_loss_count)) {
    if (probe_bw_.is_sample_from_probing) {
      probe_bw_.is_sample_from_probing = false;
      if (!send_state.is_app_limited || params_.max_probe_up_queue_rounds > 0) {
        const QuicByteCount inflight_target =
            GetTargetBytesInflight() * (1.0 - params_.beta);
        if (params_.limit_inflight_hi_by_max_delivered) {
          QuicByteCount new_inflight_hi =
              std::max(inflight_at_send, inflight_target);
          if (new_inflight_hi < model_.max_bytes_delivered_in_round()) {
            new_inflight_hi = model_.max_bytes_delivered_in_round();
          }
          QUIC_DVLOG(3) << this
                        << " Setting inflight_hi due to loss. new_inflight_hi:"
                        << new_inflight_hi
                        << ", inflight_at_send:" << inflight_at_send
                        << ", inflight_target:" << inflight_target
                        << ", max_bytes_delivered_in_round:"
                        << model_.max_bytes_delivered_in_round() << "  @ "
                        << congestion_event.event_time;
          model_.set_inflight_hi(new_inflight_hi);
        } else {
          model_.set_inflight_hi(std::max(inflight_at_send, inflight_target));
        }
      }

      QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                    << ": ADAPTED_PROBED_TOO_HIGH";
      return ADAPTED_PROBED_TOO_HIGH;
    }
    return ADAPTED_OK;
  }

  if (model_.inflight_hi() == model_.inflight_hi_default()) {
    QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                  << ": NOT_ADAPTED_INFLIGHT_HIGH_NOT_SET";
    return NOT_ADAPTED_INFLIGHT_HIGH_NOT_SET;
  }

  // Raise the upper bound for inflight.
  if (inflight_at_send > model_.inflight_hi()) {
    QUIC_DVLOG(3)
        << this << " " << ProbePhaseToString(probe_bw_.phase)
        << ": Adapting inflight_hi from inflight_at_send. inflight_at_send:"
        << inflight_at_send << ", old inflight_hi:" << model_.inflight_hi();
    model_.set_inflight_hi(inflight_at_send);
  }

  return ADAPTED_OK;
}

bool Bbr3Sender::IsTimeToProbeBandwidth(
    const Bbr2CongestionEvent& congestion_event) const {
  if (HasCycleLasted(probe_bw_.probe_wait_time, congestion_event)) {
    return true;
  }

  if (IsTimeToProbeForRenoCoexistence(1.0, congestion_event)) {
    ++connection_stats_->bbr_num_short_cycles_for_reno_coexistence;
    return true;
  }
  return false;
}

bool Bbr3Sender::HasCycleLasted(
    QuicTime::Delta duration,
    const Bbr2CongestionEvent& congestion_event) const {
  bool result =
      (congestion_event.event_time - probe_bw_.cycle_start_time) > duration;
  QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                << ": HasCycleLasted=" << result << ". elapsed:"
                << (congestion_event.event_time - probe_bw_.cycle_start_time)
                << ", duration:" << duration;
  return result;
}

bool Bbr3Sender::HasPhaseLasted(
    QuicTime::Delta duration,
    const Bbr2CongestionEvent& congestion_event) const {
  bool result =
      (congestion_event.event_time - probe_bw_.phase_start_time) > duration;
  QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                << ": HasPhaseLasted=" << result << ". elapsed:"
                << (congestion_event.event_time - probe_bw_.phase_start_time)
                << ", duration:" << duration;
  return result;
}

bool Bbr3Sender::IsTimeToProbeForRenoCoexistence(
    double probe_wait_fraction,
    const Bbr2CongestionEvent& /*congestion_event*/) const {
  if (!params_.enable_reno_coexistence) {
    return false;
  }

  uint64_t rounds = params_.probe_bw_probe_max_rounds;
  if (params_.probe_bw_probe_reno_gain > 0.0) {
    QuicByteCount target_bytes_inflight = GetTargetBytesInflight();
    uint64_t reno_rounds = params_.probe_bw_probe_reno_gain *
                           target_bytes_inflight / kDefaultTCPMSS;
    rounds = std::min(rounds, reno_rounds);
  }
  bool result = probe_bw_.rounds_since_probe >= (rounds * probe_wait_fraction);
  QUIC_DVLOG(3) << this << " " << ProbePhaseToString(probe_bw_.phase)
                << ": IsTimeToProbeForRenoCoexistence=" << result
                << ". rounds_since_probe:" << probe_bw_.rounds_since_probe
                << ", rounds:" << rounds
                << ", probe_wait_fraction:" << probe_wait_fraction;
  return result;
}

void Bbr3Sender::RaiseInflightHighSlope() {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_UP);
  uint64_t growth_this_round = 1 << probe_bw_.probe_up_rounds;
  // The number 30 below means |growth_this_round| is capped at 1G and the lower
  // bound of |probe_up_bytes| is (practically) 1 mss, at this speed inflight_hi
  // grows by approximately 1 packet per packet acked.
  probe_bw_.probe_up_rounds =
      std::min<uint64_t>(probe_bw_.probe_up_rounds + 1, 30);
  uint64_t probe_up_bytes = GetCongestionWindow() / growth_this_round;
  probe_bw_.probe_up_bytes =
      std::max<QuicByteCount>(probe_up_bytes, kDefaultTCPMSS);
  QUIC_DVLOG(3) << this << " Rasing inflight_hi slope. probe_up_rounds:"
                << probe_bw_.probe_up_rounds
                << ", probe_up_bytes:" << probe_bw_.probe_up_bytes;
}

void Bbr3Sender::ProbeInflightHighUpward(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_UP);
  if (params_.probe_up_ignore_inflight_hi) {
    return;
  }
  if (params_.probe_up_simplify_inflight_hi) {
    // Raise inflight_hi exponentially if it was utilized this round.
    probe_bw_.probe_up_acked += congestion_event.bytes_acked;
    if (!congestion_event.end_of_round_trip) {
      return;
    }
    if (!model_.inflight_hi_limited_in_round() ||
        model_.loss_events_in_round() > 0) {
      probe_bw_.probe_up_acked = 0;
      return;
    }
  } else {
    if (congestion_event.prior_bytes_in_flight < congestion_event.prior_cwnd) {
      QUIC_DVLOG(3) << this
                    << " Raising inflight_hi early return: Not cwnd limited.";
      // Not fully utilizing cwnd, so can't safely grow.
      return;
    }

    if (congestion_event.prior_cwnd < model_.inflight_hi()) {
      QUIC_DVLOG(3)
          << this
          << " Raising inflight_hi early return: inflight_hi not fully used.";
      // Not fully using inflight_hi, so don't grow it.
      return;
    }

    // Increase inflight_hi by the number of probe_up_bytes within
    // probe_up_acked.
    probe_bw_.probe_up_acked += congestion_event.bytes_acked;
  }

  if (probe_bw_.probe_up_acked >= probe_bw_.probe_up_bytes) {
    uint64_t delta = probe_bw_.probe_up_acked / probe_bw_.probe_up_bytes;
    probe_bw_.probe_up_acked -= delta * probe_bw_.probe_up_bytes;
    QuicByteCount new_inflight_hi =
        model_.inflight_hi() + delta * kDefaultTCPMSS;
    if (new_inflight_hi > model_.inflight_hi()) {
      QUIC_DVLOG(3) << this << " Raising inflight_hi from "
                    << model_.inflight_hi() << " to " << new_inflight_hi
                    << ". probe_up_bytes:" << probe_bw_.probe_up_bytes
                    << ", delta:" << delta
                    << ", (new)probe_up_acked:" << probe_bw_.probe_up_acked;

      model_.set_inflight_hi(new_inflight_hi);
    } else {
      QUIC_BUG(quic_bug_10463_4)
          << "Not growing inflight_hi due to wrap around. Old value:"
          << model_.inflight_hi() << ", new value:" << new_inflight_hi;
    }
  }

  if (congestion_event.end_of_round_trip) {
    RaiseInflightHighSlope();
  }
}

void Bbr3Sender::UpdateProbeCruise(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_CRUISE);
  MaybeAdaptUpperBounds(congestion_event);
  QUICHE_DCHECK(!probe_bw_.is_sample_from_probing);

  if (IsTimeToProbeBandwidth(congestion_event)) {
    EnterProbeRefill(/*probe_up_rounds=*/0, congestion_event.event_time);
    return;
  }
}

void Bbr3Sender::UpdateProbeRefill(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_REFILL);
  MaybeAdaptUpperBounds(congestion_event);
  QUICHE_DCHECK(!probe_bw_.is_sample_from_probing);

  if (probe_bw_.rounds_in_phase > 0 && congestion_event.end_of_round_trip) {
    EnterProbeUp(congestion_event.event_time);
    return;
  }
}

void Bbr3Sender::UpdateProbeUp(const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_UP);
  if (MaybeAdaptUpperBounds(congestion_event) == ADAPTED_PROBED_TOO_HIGH) {
    EnterProbeDown(/*probed_too_high=*/true, /*stopped_risky_probe=*/false,
                   congestion_event.event_time);
    return;
  }

  ProbeInflightHighUpward(congestion_event);

  bool is_risky = false;
  bool is_queuing = false;
  if (probe_bw_.last_cycle_probed_too_high &&
      congestion_event.prior_bytes_in_flight >= model_.inflight_hi()) {
    is_risky = true;
    QUIC_DVLOG(3) << this << " Probe is too risky. last_cycle_probed_too_high:"
                  << probe_bw_.last_cycle_probed_too_high
                  << ", prior_in_flight:"
                  << congestion_event.prior_bytes_in_flight
                  << ", inflight_hi:" << model_.inflight_hi();
  } else if (probe_bw_.rounds_in_phase > 0) {
    if (params_.max_probe_up_queue_rounds > 0) {
      if (congestion_event.end_of_round_trip) {
        model_.CheckPersistentQueue(congestion_event,
                                    params_.full_bw_threshold);
        if (model_.rounds_with_queueing() >=
            params_.max_probe_up_queue_rounds) {
          is_queuing = true;
        }
      }
    } else {
      QuicByteCount queuing_threshold_extra_bytes =
          model_.QueueingThresholdExtraBytes();
      if (params_.add_ack_height_to_queueing_threshold) {
        queuing_threshold_extra_bytes += model_.MaxAckHeight();
      }
      QuicByteCount queuing_threshold =
          (params_.full_bw_threshold * model_.BDP()) +
          queuing_threshold_extra_bytes;

      is_queuing = congestion_event.bytes_in_flight >= queuing_threshold;

      QUIC_DVLOG(3) << this
                    << " Checking if building up a queue. prior_in_flight:"
                    << congestion_event.prior_bytes_in_flight
                    << ", post_in_flight:" << congestion_event.bytes_in_flight
                    << ", threshold:" << queuing_threshold
                    << ", is_queuing:" << is_queuing
                    << ", max_bw:" << model_.MaxBandwidth()
                    << ", min_rtt:" << model_.MinRtt();
    }
  }

  if (is_risky || is_queuing) {
    EnterProbeDown(/*probed_too_high=*/false, /*stopped_risky_probe=*/is_risky,
                   congestion_event.event_time);
  }
}

void Bbr3Sender::EnterProbeDown(bool probed_too_high, bool stopped_risky_probe,
                                QuicTime now) {
  QUIC_DVLOG(2) << this
                << " Phase change: " << ProbePhaseToString(probe_bw_.phase)
                << " ==> "
                << "PROBE_DOWN" << " after " << now - probe_bw_.phase_start_time
                << ", or " << probe_bw_.rounds_in_phase
                << " rounds. probed_too_high:" << probed_too_high
                << ", stopped_risky_probe:" << stopped_risky_probe << "  @ "
                << now;
  probe_bw_.last_cycle_probed_too_high = probed_too_high;
  probe_bw_.last_cycle_stopped_risky_probe = stopped_risky_probe;

  probe_bw_.cycle_start_time = now;
  probe_bw_.phase = ProbePhase::PROBE_DOWN;
  probe_bw_.rounds_in_phase = 0;
  probe_bw_.phase_start_time = now;
  ++connection_stats_->bbr_num_cycles;
  if (params_.bw_lo_mode_ != Bbr2Params::QuicBandwidthLoMode::DEFAULT) {
    model_.clear_bandwidth_lo();
  }

  // Pick probe wait time.
  probe_bw_.rounds_since_probe =
      RandomUint64(params_.probe_bw_max_probe_rand_rounds);
  probe_bw_.probe_wait_time =
      params_.probe_bw_probe_base_duration +
      QuicTime::Delta::FromMicroseconds(RandomUint64(
          params_.probe_bw_probe_max_rand_duration.ToMicroseconds()));

  probe_bw_.probe_up_bytes = std::numeric_limits<QuicByteCount>::max();
  probe_bw_.has_advanced_max_bw = false;
  model_.RestartRoundEarly();
}

void Bbr3Sender::EnterProbeCruise(QuicTime now) {
  if (probe_bw_.phase == ProbePhase::PROBE_DOWN) {
    ExitProbeDown();
  }
  QUIC_DVLOG(2) << this
                << " Phase change: " << ProbePhaseToString(probe_bw_.phase)
                << " ==> "
                << "PROBE_CRUISE" << " after "
                << now - probe_bw_.phase_start_time << ", or "
                << probe_bw_.rounds_in_phase << " rounds.  @ " << now;

  model_.cap_inflight_lo(model_.inflight_hi());
  probe_bw_.phase = ProbePhase::PROBE_CRUISE;
  probe_bw_.rounds_in_phase = 0;
  probe_bw_.phase_start_time = now;
  probe_bw_.is_sample_from_probing = false;
}

void Bbr3Sender::EnterProbeRefill(uint64_t probe_up_rounds, QuicTime now) {
  if (probe_bw_.phase == ProbePhase::PROBE_DOWN) {
    ExitProbeDown();
  }
  QUIC_DVLOG(2) << this
                << " Phase change: " << ProbePhaseToString(probe_bw_.phase)
                << " ==> "
                << "PROBE_REFILL" << " after "
                << now - probe_bw_.phase_start_time << ", or "
                << probe_bw_.rounds_in_phase
                << " rounds. probe_up_rounds:" << probe_up_rounds << "  @ "
                << now;
  probe_bw_.phase = ProbePhase::PROBE_REFILL;
  probe_bw_.rounds_in_phase = 0;
  probe_bw_.phase_start_time = now;
  probe_bw_.is_sample_from_probing = false;
  probe_bw_.last_cycle_stopped_risky_probe = false;

  model_.clear_bandwidth_lo();
  model_.clear_inflight_lo();
  probe_bw_.probe_up_rounds = probe_up_rounds;
  probe_bw_.probe_up_acked = 0;
  model_.RestartRoundEarly();
}

void Bbr3Sender::EnterProbeUp(QuicTime now) {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_REFILL);
  QUIC_DVLOG(2) << this
                << " Phase change: " << ProbePhaseToString(probe_bw_.phase)
                << " ==> "
                << "PROBE_UP" << " after " << now - probe_bw_.phase_start_time
                << ", or " << probe_bw_.rounds_in_phase << " rounds.  @ "
                << now;
  probe_bw_.phase = ProbePhase::PROBE_UP;
  probe_bw_.rounds_in_phase = 0;
  probe_bw_.phase_start_time = now;
  probe_bw_.is_sample_from_probing = true;
  RaiseInflightHighSlope();

  model_.RestartRoundEarly();
}

void Bbr3Sender::ExitProbeDown() {
  QUICHE_DCHECK_EQ(probe_bw_.phase, ProbePhase::PROBE_DOWN);
  if (!probe_bw_.has_advanced_max_bw) {
    QUIC_DVLOG(2) << this << " Advancing max bw filter at end of cycle.";
    model_.AdvanceMaxBandwidthFilter();
    probe_bw_.has_advanced_max_bw = true;
  }
}

void Bbr3Sender::EnterProbeRtt() {
  model_.set_pacing_gain(1.0);
  model_.set_cwnd_gain(1.0);
  probe_rtt_.exit_time = QuicTime::Zero();
}

Bbr2Mode Bbr3Sender::OnCongestionEventProbeRtt(
    const Bbr2CongestionEvent& congestion_event) {
  if (probe_rtt_.exit_time == QuicTime::Zero()) {
    if (congestion_event.bytes_in_flight <= InflightTarget() ||
        congestion_event.bytes_in_flight <= GetMinimumCongestionWindow()) {
      probe_rtt_.exit_time =
          congestion_event.event_time + params_.probe_rtt_duration;
      QUIC_DVLOG(2) << this << " PROBE_RTT exit time set to "
                    << probe_rtt_.exit_time
                    << ". bytes_inflight:" << congestion_event.bytes_in_flight
                    << ", inflight_target:" << InflightTarget()
                    << ", min_congestion_window:"
                    << GetMinimumCongestionWindow() << "  @ "
                    << congestion_event.event_time;
    }
    return Bbr2Mode::PROBE_RTT;
  }

  return congestion_event.event_time > probe_rtt_.exit_time
             ? Bbr2Mode::PROBE_BW
             : Bbr2Mode::PROBE_RTT;
}

QuicByteCount Bbr3Sender::InflightTarget() const {
  return model_.BDP(model_.MaxBandwidth(),
                    params_.probe_rtt_inflight_target_bdp_fraction);
}

float Bbr3Sender::PacingGainForPhase(ProbePhase phase) const {
  if (phase == ProbePhase::PROBE_UP) {
    return params_.probe_bw_probe_up_pacing_gain;
  }
  if (phase == ProbePhase::PROBE_DOWN) {
    return params_.probe_bw_probe_down_pacing_gain;
  }
  return params_.probe_bw_default_pacing_gain;
}

}  // namespace quic
