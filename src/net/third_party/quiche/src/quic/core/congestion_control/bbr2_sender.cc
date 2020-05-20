// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_sender.h"

#include <cstddef>

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_drain.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

namespace {
// Constants based on TCP defaults.
// The minimum CWND to ensure delayed acks don't reduce bandwidth measurements.
// Does not inflate the pacing rate.
const QuicByteCount kDefaultMinimumCongestionWindow = 4 * kMaxSegmentSize;

const float kInitialPacingGain = 2.885f;

const int kMaxModeChangesPerCongestionEvent = 4;
}  // namespace

// Call |member_function_call| based on the current Bbr2Mode we are in. e.g.
//
//   auto result = BBR2_MODE_DISPATCH(Foo());
//
// is equivalent to:
//
//   Bbr2ModeBase& Bbr2Sender::GetCurrentMode() {
//     if (mode_ == Bbr2Mode::STARTUP) { return startup_; }
//     if (mode_ == Bbr2Mode::DRAIN) { return drain_; }
//     ...
//   }
//   auto result = GetCurrentMode().Foo();
//
// Except that BBR2_MODE_DISPATCH guarantees the call to Foo() is non-virtual.
//
#define BBR2_MODE_DISPATCH(member_function_call)     \
  (mode_ == Bbr2Mode::STARTUP                        \
       ? (startup_.member_function_call)             \
       : (mode_ == Bbr2Mode::PROBE_BW                \
              ? (probe_bw_.member_function_call)     \
              : (mode_ == Bbr2Mode::DRAIN            \
                     ? (drain_.member_function_call) \
                     : (probe_rtt_or_die().member_function_call))))

Bbr2Sender::Bbr2Sender(QuicTime now,
                       const RttStats* rtt_stats,
                       const QuicUnackedPacketMap* unacked_packets,
                       QuicPacketCount initial_cwnd_in_packets,
                       QuicPacketCount max_cwnd_in_packets,
                       QuicRandom* random,
                       QuicConnectionStats* stats,
                       BbrSender* old_sender)
    : mode_(Bbr2Mode::STARTUP),
      rtt_stats_(rtt_stats),
      unacked_packets_(unacked_packets),
      random_(random),
      connection_stats_(stats),
      params_(kDefaultMinimumCongestionWindow,
              max_cwnd_in_packets * kDefaultTCPMSS),
      model_(&params_,
             rtt_stats->SmoothedOrInitialRtt(),
             rtt_stats->last_update_time(),
             /*cwnd_gain=*/1.0,
             /*pacing_gain=*/kInitialPacingGain,
             old_sender ? &old_sender->sampler_ : nullptr),
      initial_cwnd_(
          cwnd_limits().ApplyLimits(initial_cwnd_in_packets * kDefaultTCPMSS)),
      cwnd_(initial_cwnd_),
      pacing_rate_(kInitialPacingGain * QuicBandwidth::FromBytesAndTimeDelta(
                                            cwnd_,
                                            rtt_stats->SmoothedOrInitialRtt())),
      startup_(this, &model_, now),
      drain_(this, &model_),
      probe_bw_(this, &model_),
      probe_rtt_(this, &model_),
      last_sample_is_app_limited_(false) {
  QUIC_DVLOG(2) << this << " Initializing Bbr2Sender. mode:" << mode_
                << ", PacingRate:" << pacing_rate_ << ", Cwnd:" << cwnd_
                << ", CwndLimits:" << cwnd_limits() << "  @ " << now;
  DCHECK_EQ(mode_, Bbr2Mode::STARTUP);
}

void Bbr2Sender::SetFromConfig(const QuicConfig& config,
                               Perspective perspective) {
  if (config.HasClientRequestedIndependentOption(kBBR9, perspective)) {
    params_.flexible_app_limited = true;
  }
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation) &&
      config.HasClientRequestedIndependentOption(kBSAO, perspective)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(
        quic_avoid_overestimate_bandwidth_with_aggregation, 4, 4);
    model_.EnableOverestimateAvoidance();
  }
  if (config.HasClientRequestedIndependentOption(kB2NA, perspective)) {
    params_.add_ack_height_to_queueing_threshold = false;
  }
  if (config.HasClientRequestedIndependentOption(kB2RP, perspective)) {
    params_.avoid_unnecessary_probe_rtt = false;
  }
}

Limits<QuicByteCount> Bbr2Sender::GetCwndLimitsByMode() const {
  switch (mode_) {
    case Bbr2Mode::STARTUP:
      return startup_.GetCwndLimits();
    case Bbr2Mode::PROBE_BW:
      return probe_bw_.GetCwndLimits();
    case Bbr2Mode::DRAIN:
      return drain_.GetCwndLimits();
    case Bbr2Mode::PROBE_RTT:
      return probe_rtt_.GetCwndLimits();
    default:
      QUIC_NOTREACHED();
      return Unlimited<QuicByteCount>();
  }
}

const Limits<QuicByteCount>& Bbr2Sender::cwnd_limits() const {
  return params().cwnd_limits;
}

void Bbr2Sender::AdjustNetworkParameters(const NetworkParams& params) {
  model_.UpdateNetworkParameters(params.bandwidth, params.rtt);

  if (mode_ == Bbr2Mode::STARTUP) {
    const QuicByteCount prior_cwnd = cwnd_;

    // Normally UpdateCongestionWindow updates |cwnd_| towards the target by a
    // small step per congestion event, by changing |cwnd_| to the bdp at here
    // we are reducing the number of updates needed to arrive at the target.
    cwnd_ = model_.BDP(model_.BandwidthEstimate());
    UpdateCongestionWindow(0);
    if (!params.allow_cwnd_to_decrease) {
      cwnd_ = std::max(cwnd_, prior_cwnd);
    }
  }
}

void Bbr2Sender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  if (mode_ == Bbr2Mode::STARTUP) {
    // The cwnd limits is unchanged and still applies to the new cwnd.
    cwnd_ = cwnd_limits().ApplyLimits(congestion_window * kDefaultTCPMSS);
  }
}

void Bbr2Sender::OnCongestionEvent(bool /*rtt_updated*/,
                                   QuicByteCount prior_in_flight,
                                   QuicTime event_time,
                                   const AckedPacketVector& acked_packets,
                                   const LostPacketVector& lost_packets) {
  QUIC_DVLOG(3) << this
                << " OnCongestionEvent. prior_in_flight:" << prior_in_flight
                << " prior_cwnd:" << cwnd_ << "  @ " << event_time;
  Bbr2CongestionEvent congestion_event;
  congestion_event.prior_cwnd = cwnd_;
  congestion_event.prior_bytes_in_flight = prior_in_flight;
  congestion_event.is_probing_for_bandwidth =
      BBR2_MODE_DISPATCH(IsProbingForBandwidth());

  model_.OnCongestionEventStart(event_time, acked_packets, lost_packets,
                                &congestion_event);

  // Number of mode changes allowed for this congestion event.
  int mode_changes_allowed = kMaxModeChangesPerCongestionEvent;
  while (true) {
    Bbr2Mode next_mode = BBR2_MODE_DISPATCH(
        OnCongestionEvent(prior_in_flight, event_time, acked_packets,
                          lost_packets, congestion_event));

    if (next_mode == mode_) {
      break;
    }

    QUIC_DVLOG(2) << this << " Mode change:  " << mode_ << " ==> " << next_mode
                  << "  @ " << event_time;
    BBR2_MODE_DISPATCH(Leave(event_time, &congestion_event));
    mode_ = next_mode;
    BBR2_MODE_DISPATCH(Enter(event_time, &congestion_event));
    --mode_changes_allowed;
    if (mode_changes_allowed < 0) {
      QUIC_BUG << "Exceeded max number of mode changes per congestion event.";
      break;
    }
  }

  UpdatePacingRate(congestion_event.bytes_acked);
  QUIC_BUG_IF(pacing_rate_.IsZero()) << "Pacing rate must not be zero!";

  UpdateCongestionWindow(congestion_event.bytes_acked);
  QUIC_BUG_IF(cwnd_ == 0u) << "Congestion window must not be zero!";

  model_.OnCongestionEventFinish(unacked_packets_->GetLeastUnacked(),
                                 congestion_event);
  last_sample_is_app_limited_ = congestion_event.last_sample_is_app_limited;
  if (congestion_event.bytes_in_flight == 0 &&
      params().avoid_unnecessary_probe_rtt) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_avoid_unnecessary_probe_rtt, 2, 2);
    OnEnterQuiescence(event_time);
  }

  QUIC_DVLOG(3)
      << this << " END CongestionEvent(acked:" << acked_packets
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

void Bbr2Sender::UpdatePacingRate(QuicByteCount bytes_acked) {
  if (BandwidthEstimate().IsZero()) {
    return;
  }

  if (model_.total_bytes_acked() == bytes_acked) {
    // After the first ACK, cwnd_ is still the initial congestion window.
    pacing_rate_ = QuicBandwidth::FromBytesAndTimeDelta(cwnd_, model_.MinRtt());
    return;
  }

  QuicBandwidth target_rate = model_.pacing_gain() * model_.BandwidthEstimate();
  if (startup_.FullBandwidthReached()) {
    pacing_rate_ = target_rate;
    return;
  }

  if (target_rate > pacing_rate_) {
    pacing_rate_ = target_rate;
  }
}

void Bbr2Sender::UpdateCongestionWindow(QuicByteCount bytes_acked) {
  QuicByteCount target_cwnd = GetTargetCongestionWindow(model_.cwnd_gain());

  const QuicByteCount prior_cwnd = cwnd_;
  if (startup_.FullBandwidthReached()) {
    target_cwnd += model_.MaxAckHeight();
    cwnd_ = std::min(prior_cwnd + bytes_acked, target_cwnd);
  } else if (prior_cwnd < target_cwnd || prior_cwnd < 2 * initial_cwnd_) {
    cwnd_ = prior_cwnd + bytes_acked;
  }
  const QuicByteCount desired_cwnd = cwnd_;

  cwnd_ = GetCwndLimitsByMode().ApplyLimits(cwnd_);
  const QuicByteCount model_limited_cwnd = cwnd_;

  cwnd_ = cwnd_limits().ApplyLimits(cwnd_);

  QUIC_DVLOG(3) << this << " Updating CWND. target_cwnd:" << target_cwnd
                << ", max_ack_height:" << model_.MaxAckHeight()
                << ", full_bw:" << startup_.FullBandwidthReached()
                << ", bytes_acked:" << bytes_acked
                << ", inflight_lo:" << model_.inflight_lo()
                << ", inflight_hi:" << model_.inflight_hi() << ". (prior_cwnd) "
                << prior_cwnd << " => (desired_cwnd) " << desired_cwnd
                << " => (model_limited_cwnd) " << model_limited_cwnd
                << " => (final_cwnd) " << cwnd_;
}

QuicByteCount Bbr2Sender::GetTargetCongestionWindow(float gain) const {
  return std::max(model_.BDP(model_.BandwidthEstimate(), gain),
                  cwnd_limits().Min());
}

void Bbr2Sender::OnPacketSent(QuicTime sent_time,
                              QuicByteCount bytes_in_flight,
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
  if (bytes_in_flight == 0 && params().avoid_unnecessary_probe_rtt) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_avoid_unnecessary_probe_rtt, 1, 2);
    OnExitQuiescence(sent_time);
  }
  model_.OnPacketSent(sent_time, bytes_in_flight, packet_number, bytes,
                      is_retransmittable);
}

void Bbr2Sender::OnPacketNeutered(QuicPacketNumber packet_number) {
  model_.OnPacketNeutered(packet_number);
}

bool Bbr2Sender::CanSend(QuicByteCount bytes_in_flight) {
  const bool result = bytes_in_flight < GetCongestionWindow();
  return result;
}

QuicByteCount Bbr2Sender::GetCongestionWindow() const {
  // TODO(wub): Implement Recovery?
  return cwnd_;
}

QuicBandwidth Bbr2Sender::PacingRate(QuicByteCount /*bytes_in_flight*/) const {
  return pacing_rate_;
}

void Bbr2Sender::OnApplicationLimited(QuicByteCount bytes_in_flight) {
  if (bytes_in_flight >= GetCongestionWindow()) {
    return;
  }
  if (params().flexible_app_limited && IsPipeSufficientlyFull()) {
    return;
  }

  model_.OnApplicationLimited();
  QUIC_DVLOG(2) << this << " Becoming application limited. Last sent packet: "
                << model_.last_sent_packet()
                << ", CWND: " << GetCongestionWindow();
}

QuicByteCount Bbr2Sender::GetTargetBytesInflight() const {
  QuicByteCount bdp = model_.BDP(model_.BandwidthEstimate());
  return std::min(bdp, GetCongestionWindow());
}

void Bbr2Sender::PopulateConnectionStats(QuicConnectionStats* stats) const {
  stats->num_ack_aggregation_epochs = model_.num_ack_aggregation_epochs();
}

void Bbr2Sender::OnEnterQuiescence(QuicTime now) {
  last_quiescence_start_ = now;
}

void Bbr2Sender::OnExitQuiescence(QuicTime now) {
  if (last_quiescence_start_ != QuicTime::Zero()) {
    Bbr2Mode next_mode = BBR2_MODE_DISPATCH(
        OnExitQuiescence(now, std::min(now, last_quiescence_start_)));
    if (next_mode != mode_) {
      BBR2_MODE_DISPATCH(Leave(now, nullptr));
      mode_ = next_mode;
      BBR2_MODE_DISPATCH(Enter(now, nullptr));
    }
    last_quiescence_start_ = QuicTime::Zero();
  }
}

bool Bbr2Sender::ShouldSendProbingPacket() const {
  // TODO(wub): Implement ShouldSendProbingPacket properly.
  if (!BBR2_MODE_DISPATCH(IsProbingForBandwidth())) {
    return false;
  }

  // TODO(b/77975811): If the pipe is highly under-utilized, consider not
  // sending a probing transmission, because the extra bandwidth is not needed.
  // If flexible_app_limited is enabled, check if the pipe is sufficiently full.
  if (params().flexible_app_limited) {
    const bool is_pipe_sufficiently_full = IsPipeSufficientlyFull();
    QUIC_DVLOG(3) << this << " CWND: " << GetCongestionWindow()
                  << ", inflight: " << unacked_packets_->bytes_in_flight()
                  << ", pacing_rate: " << PacingRate(0)
                  << ", flexible_app_limited: true, ShouldSendProbingPacket: "
                  << !is_pipe_sufficiently_full;
    return !is_pipe_sufficiently_full;
  } else {
    return true;
  }
}

bool Bbr2Sender::IsPipeSufficientlyFull() const {
  QuicByteCount bytes_in_flight = unacked_packets_->bytes_in_flight();
  // See if we need more bytes in flight to see more bandwidth.
  if (mode_ == Bbr2Mode::STARTUP) {
    // STARTUP exits if it doesn't observe a 25% bandwidth increase, so the CWND
    // must be more than 25% above the target.
    return bytes_in_flight >= GetTargetCongestionWindow(1.5);
  }
  if (model_.pacing_gain() > 1) {
    // Super-unity PROBE_BW doesn't exit until 1.25 * BDP is achieved.
    return bytes_in_flight >= GetTargetCongestionWindow(model_.pacing_gain());
  }
  // If bytes_in_flight are above the target congestion window, it should be
  // possible to observe the same or more bandwidth if it's available.
  return bytes_in_flight >= GetTargetCongestionWindow(1.1);
}

std::string Bbr2Sender::GetDebugState() const {
  std::ostringstream stream;
  stream << ExportDebugState();
  return stream.str();
}

Bbr2Sender::DebugState Bbr2Sender::ExportDebugState() const {
  DebugState s;
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

  s.startup = startup_.ExportDebugState();
  s.drain = drain_.ExportDebugState();
  s.probe_bw = probe_bw_.ExportDebugState();
  s.probe_rtt = probe_rtt_.ExportDebugState();

  return s;
}

std::ostream& operator<<(std::ostream& os, const Bbr2Sender::DebugState& s) {
  os << "mode: " << s.mode << "\n";
  os << "round_trip_count: " << s.round_trip_count << "\n";
  os << "bandwidth_hi ~ lo ~ est: " << s.bandwidth_hi << " ~ " << s.bandwidth_lo
     << " ~ " << s.bandwidth_est << "\n";
  os << "min_rtt: " << s.min_rtt << "\n";
  os << "min_rtt_timestamp: " << s.min_rtt_timestamp << "\n";
  os << "congestion_window: " << s.congestion_window << "\n";
  os << "pacing_rate: " << s.pacing_rate << "\n";
  os << "last_sample_is_app_limited: " << s.last_sample_is_app_limited << "\n";

  if (s.mode == Bbr2Mode::STARTUP) {
    os << s.startup;
  }

  if (s.mode == Bbr2Mode::DRAIN) {
    os << s.drain;
  }

  if (s.mode == Bbr2Mode::PROBE_BW) {
    os << s.probe_bw;
  }

  if (s.mode == Bbr2Mode::PROBE_RTT) {
    os << s.probe_rtt;
  }

  return os;
}

}  // namespace quic
