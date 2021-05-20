// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/congestion_control/bbr2_misc.h"

#include "quic/core/congestion_control/bandwidth_sampler.h"
#include "quic/core/quic_bandwidth.h"
#include "quic/core/quic_time.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

RoundTripCounter::RoundTripCounter() : round_trip_count_(0) {}

void RoundTripCounter::OnPacketSent(QuicPacketNumber packet_number) {
  QUICHE_DCHECK(!last_sent_packet_.IsInitialized() ||
                last_sent_packet_ < packet_number);
  last_sent_packet_ = packet_number;
}

bool RoundTripCounter::OnPacketsAcked(QuicPacketNumber last_acked_packet) {
  if (!end_of_round_trip_.IsInitialized() ||
      last_acked_packet > end_of_round_trip_) {
    round_trip_count_++;
    end_of_round_trip_ = last_sent_packet_;
    return true;
  }
  return false;
}

void RoundTripCounter::RestartRound() {
  end_of_round_trip_ = last_sent_packet_;
}

MinRttFilter::MinRttFilter(QuicTime::Delta initial_min_rtt,
                           QuicTime initial_min_rtt_timestamp)
    : min_rtt_(initial_min_rtt),
      min_rtt_timestamp_(initial_min_rtt_timestamp) {}

void MinRttFilter::Update(QuicTime::Delta sample_rtt, QuicTime now) {
  if (sample_rtt < min_rtt_ || min_rtt_timestamp_ == QuicTime::Zero()) {
    min_rtt_ = sample_rtt;
    min_rtt_timestamp_ = now;
  }
}

void MinRttFilter::ForceUpdate(QuicTime::Delta sample_rtt, QuicTime now) {
  min_rtt_ = sample_rtt;
  min_rtt_timestamp_ = now;
}

Bbr2NetworkModel::Bbr2NetworkModel(const Bbr2Params* params,
                                   QuicTime::Delta initial_rtt,
                                   QuicTime initial_rtt_timestamp,
                                   float cwnd_gain,
                                   float pacing_gain,
                                   const BandwidthSampler* old_sampler)
    : params_(params),
      bandwidth_sampler_([](QuicRoundTripCount max_height_tracker_window_length,
                            const BandwidthSampler* old_sampler) {
        if (old_sampler != nullptr) {
          return BandwidthSampler(*old_sampler);
        }
        return BandwidthSampler(/*unacked_packet_map=*/nullptr,
                                max_height_tracker_window_length);
      }(params->initial_max_ack_height_filter_window, old_sampler)),
      min_rtt_filter_(initial_rtt, initial_rtt_timestamp),
      cwnd_gain_(cwnd_gain),
      pacing_gain_(pacing_gain) {}

void Bbr2NetworkModel::OnPacketSent(QuicTime sent_time,
                                    QuicByteCount bytes_in_flight,
                                    QuicPacketNumber packet_number,
                                    QuicByteCount bytes,
                                    HasRetransmittableData is_retransmittable) {
  round_trip_counter_.OnPacketSent(packet_number);

  bandwidth_sampler_.OnPacketSent(sent_time, packet_number, bytes,
                                  bytes_in_flight, is_retransmittable);
}

void Bbr2NetworkModel::OnCongestionEventStart(
    QuicTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets,
    Bbr2CongestionEvent* congestion_event) {
  const QuicByteCount prior_bytes_acked = total_bytes_acked();
  const QuicByteCount prior_bytes_lost = total_bytes_lost();

  congestion_event->event_time = event_time;
  congestion_event->end_of_round_trip =
      acked_packets.empty() ? false
                            : round_trip_counter_.OnPacketsAcked(
                                  acked_packets.rbegin()->packet_number);

  BandwidthSamplerInterface::CongestionEventSample sample =
      bandwidth_sampler_.OnCongestionEvent(event_time, acked_packets,
                                           lost_packets, MaxBandwidth(),
                                           bandwidth_lo(), RoundTripCount());

  if (sample.last_packet_send_state.is_valid) {
    congestion_event->last_packet_send_state = sample.last_packet_send_state;
    congestion_event->last_sample_is_app_limited =
        sample.last_packet_send_state.is_app_limited;
  }

  // Avoid updating |max_bandwidth_filter_| if a) this is a loss-only event, or
  // b) all packets in |acked_packets| did not generate valid samples. (e.g. ack
  // of ack-only packets). In both cases, total_bytes_acked() will not change.
  if (prior_bytes_acked != total_bytes_acked()) {
    QUIC_LOG_IF(WARNING, sample.sample_max_bandwidth.IsZero())
        << total_bytes_acked() - prior_bytes_acked << " bytes from "
        << acked_packets.size()
        << " packets have been acked, but sample_max_bandwidth is zero.";
    if (!sample.sample_is_app_limited ||
        sample.sample_max_bandwidth > MaxBandwidth()) {
      congestion_event->sample_max_bandwidth = sample.sample_max_bandwidth;
      max_bandwidth_filter_.Update(congestion_event->sample_max_bandwidth);
    }
  }

  if (!sample.sample_rtt.IsInfinite()) {
    congestion_event->sample_min_rtt = sample.sample_rtt;
    min_rtt_filter_.Update(congestion_event->sample_min_rtt, event_time);
  }

  congestion_event->bytes_acked = total_bytes_acked() - prior_bytes_acked;
  congestion_event->bytes_lost = total_bytes_lost() - prior_bytes_lost;

  if (congestion_event->prior_bytes_in_flight >=
      congestion_event->bytes_acked + congestion_event->bytes_lost) {
    congestion_event->bytes_in_flight =
        congestion_event->prior_bytes_in_flight -
        congestion_event->bytes_acked - congestion_event->bytes_lost;
  } else {
    QUIC_LOG_FIRST_N(ERROR, 1)
        << "prior_bytes_in_flight:" << congestion_event->prior_bytes_in_flight
        << " is smaller than the sum of bytes_acked:"
        << congestion_event->bytes_acked
        << " and bytes_lost:" << congestion_event->bytes_lost;
    congestion_event->bytes_in_flight = 0;
  }

  if (congestion_event->bytes_lost > 0) {
    bytes_lost_in_round_ += congestion_event->bytes_lost;
    loss_events_in_round_++;
  }

  if (congestion_event->bytes_acked > 0 &&
      congestion_event->last_packet_send_state.is_valid &&
      total_bytes_acked() >
          congestion_event->last_packet_send_state.total_bytes_acked) {
    QuicByteCount bytes_delivered =
        total_bytes_acked() -
        congestion_event->last_packet_send_state.total_bytes_acked;
    max_bytes_delivered_in_round_ =
        std::max(max_bytes_delivered_in_round_, bytes_delivered);
  }

  // |bandwidth_latest_| and |inflight_latest_| only increased within a round.
  if (sample.sample_max_bandwidth > bandwidth_latest_) {
    bandwidth_latest_ = sample.sample_max_bandwidth;
  }

  if (sample.sample_max_inflight > inflight_latest_) {
    inflight_latest_ = sample.sample_max_inflight;
  }

  // Adapt lower bounds(bandwidth_lo and inflight_lo).
  AdaptLowerBounds(*congestion_event);

  if (!congestion_event->end_of_round_trip) {
    return;
  }

  if (!sample.sample_max_bandwidth.IsZero()) {
    bandwidth_latest_ = sample.sample_max_bandwidth;
  }

  if (sample.sample_max_inflight > 0) {
    inflight_latest_ = sample.sample_max_inflight;
  }
}

void Bbr2NetworkModel::AdaptLowerBounds(
    const Bbr2CongestionEvent& congestion_event) {
  if (Params().bw_lo_mode_ != Bbr2Params::DEFAULT) {
    QUICHE_DCHECK(Params().bw_startup);
    if (congestion_event.bytes_lost == 0) {
      return;
    }
    // Ignore losses from packets sent when probing for more bandwidth in
    // STARTUP or PROBE_UP when they're lost in DRAIN or PROBE_DOWN.
    if (pacing_gain_ < 1) {
      return;
    }
    // Decrease bandwidth_lo whenever there is loss.
    // Set bandwidth_lo_ if it is not yet set.
    if (bandwidth_lo_.IsInfinite()) {
      bandwidth_lo_ = MaxBandwidth();
    }
    switch (Params().bw_lo_mode_) {
      case Bbr2Params::MIN_RTT_REDUCTION:
        bandwidth_lo_ =
            bandwidth_lo_ - QuicBandwidth::FromBytesAndTimeDelta(
                                congestion_event.bytes_lost, MinRtt());
        break;
      case Bbr2Params::INFLIGHT_REDUCTION: {
        // Use a max of BDP and inflight to avoid starving app-limited flows.
        const QuicByteCount effective_inflight =
            std::max(BDP(), congestion_event.prior_bytes_in_flight);
        // This could use bytes_lost_in_round if the bandwidth_lo_ was saved
        // when entering 'recovery', but this BBRv2 implementation doesn't have
        // recovery defined.
        bandwidth_lo_ = bandwidth_lo_ *
                        ((effective_inflight - congestion_event.bytes_lost) /
                         static_cast<double>(effective_inflight));
        break;
      }
      case Bbr2Params::CWND_REDUCTION:
        bandwidth_lo_ =
            bandwidth_lo_ *
            ((congestion_event.prior_cwnd - congestion_event.bytes_lost) /
             static_cast<double>(congestion_event.prior_cwnd));
        break;
      case Bbr2Params::DEFAULT:
        QUIC_BUG << "Unreachable case DEFAULT.";
    }
    if (pacing_gain_ > Params().startup_full_bw_threshold) {
      // In STARTUP, pacing_gain_ is applied to bandwidth_lo_, so this backs
      // that multiplication out to allow the pacing rate to decrease,
      // but not below bandwidth_latest_ * startup_full_bw_threshold.
      bandwidth_lo_ =
          std::max(bandwidth_lo_,
                   bandwidth_latest_ *
                       (Params().startup_full_bw_threshold / pacing_gain_));
    } else {
      // Ensure bandwidth_lo isn't lower than bandwidth_latest_.
      bandwidth_lo_ = std::max(bandwidth_lo_, bandwidth_latest_);
    }
    // This early return ignores inflight_lo as well.
    return;
  }
  if (!congestion_event.end_of_round_trip ||
      congestion_event.is_probing_for_bandwidth) {
    return;
  }

  if (bytes_lost_in_round_ > 0) {
    if (bandwidth_lo_.IsInfinite()) {
      bandwidth_lo_ = MaxBandwidth();
    }
    bandwidth_lo_ =
        std::max(bandwidth_latest_, bandwidth_lo_ * (1.0 - Params().beta));
    QUIC_DVLOG(3) << "bandwidth_lo_ updated to " << bandwidth_lo_
                  << ", bandwidth_latest_ is " << bandwidth_latest_;

    if (Params().ignore_inflight_lo) {
      return;
    }
    if (inflight_lo_ == inflight_lo_default()) {
      inflight_lo_ = congestion_event.prior_cwnd;
    }
    inflight_lo_ = std::max<QuicByteCount>(
        inflight_latest_, inflight_lo_ * (1.0 - Params().beta));
  }
}

void Bbr2NetworkModel::OnCongestionEventFinish(
    QuicPacketNumber least_unacked_packet,
    const Bbr2CongestionEvent& congestion_event) {
  if (congestion_event.end_of_round_trip) {
    if (!reset_max_bytes_delivered_) {
      bytes_lost_in_round_ = 0;
      loss_events_in_round_ = 0;
    } else {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_reset_max_bytes_delivered, 1, 2);
      OnNewRound();
    }
  }

  bandwidth_sampler_.RemoveObsoletePackets(least_unacked_packet);
}

void Bbr2NetworkModel::UpdateNetworkParameters(QuicTime::Delta rtt) {
  if (!rtt.IsZero()) {
    min_rtt_filter_.Update(rtt, MinRttTimestamp());
  }
}

bool Bbr2NetworkModel::MaybeExpireMinRtt(
    const Bbr2CongestionEvent& congestion_event) {
  if (congestion_event.event_time <
      (MinRttTimestamp() + Params().probe_rtt_period)) {
    return false;
  }
  if (congestion_event.sample_min_rtt.IsInfinite()) {
    return false;
  }
  QUIC_DVLOG(3) << "Replacing expired min rtt of " << min_rtt_filter_.Get()
                << " by " << congestion_event.sample_min_rtt << "  @ "
                << congestion_event.event_time;
  min_rtt_filter_.ForceUpdate(congestion_event.sample_min_rtt,
                              congestion_event.event_time);
  return true;
}

bool Bbr2NetworkModel::IsCongestionWindowLimited(
    const Bbr2CongestionEvent& congestion_event) const {
  QuicByteCount prior_bytes_in_flight = congestion_event.bytes_in_flight +
                                        congestion_event.bytes_acked +
                                        congestion_event.bytes_lost;
  return prior_bytes_in_flight >= congestion_event.prior_cwnd;
}

bool Bbr2NetworkModel::IsInflightTooHigh(
    const Bbr2CongestionEvent& congestion_event,
    int64_t max_loss_events) const {
  const SendTimeState& send_state = congestion_event.last_packet_send_state;
  if (!send_state.is_valid) {
    // Not enough information.
    return false;
  }

  if (loss_events_in_round() < max_loss_events) {
    return false;
  }

  const QuicByteCount inflight_at_send = BytesInFlight(send_state);
  // TODO(wub): Consider total_bytes_lost() - send_state.total_bytes_lost, which
  // is the total bytes lost when the largest numbered packet was inflight.
  // bytes_lost_in_round_, OTOH, is the total bytes lost in the "current" round.
  const QuicByteCount bytes_lost_in_round = bytes_lost_in_round_;

  QUIC_DVLOG(3) << "IsInflightTooHigh: loss_events_in_round:"
                << loss_events_in_round()

                << " bytes_lost_in_round:" << bytes_lost_in_round
                << ", lost_in_round_threshold:"
                << inflight_at_send * Params().loss_threshold;

  if (inflight_at_send > 0 && bytes_lost_in_round > 0) {
    QuicByteCount lost_in_round_threshold =
        inflight_at_send * Params().loss_threshold;
    if (bytes_lost_in_round > lost_in_round_threshold) {
      return true;
    }
  }

  return false;
}

void Bbr2NetworkModel::RestartRoundEarly() {
  if (!reset_max_bytes_delivered_) {
    bytes_lost_in_round_ = 0;
    loss_events_in_round_ = 0;
    max_bytes_delivered_in_round_ = 0;
  } else {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr2_reset_max_bytes_delivered, 2, 2);
    OnNewRound();
  }
  round_trip_counter_.RestartRound();
}

void Bbr2NetworkModel::OnNewRound() {
  QUICHE_DCHECK(reset_max_bytes_delivered_);
  bytes_lost_in_round_ = 0;
  loss_events_in_round_ = 0;
  max_bytes_delivered_in_round_ = 0;
}

void Bbr2NetworkModel::cap_inflight_lo(QuicByteCount cap) {
  if (Params().ignore_inflight_lo) {
    return;
  }
  if (inflight_lo_ != inflight_lo_default() && inflight_lo_ > cap) {
    inflight_lo_ = cap;
  }
}

QuicByteCount Bbr2NetworkModel::inflight_hi_with_headroom() const {
  QuicByteCount headroom = inflight_hi_ * Params().inflight_hi_headroom;

  return inflight_hi_ > headroom ? inflight_hi_ - headroom : 0;
}

Bbr2NetworkModel::BandwidthGrowth Bbr2NetworkModel::CheckBandwidthGrowth(
    const Bbr2CongestionEvent& congestion_event) {
  QUICHE_DCHECK(!full_bandwidth_reached_);
  QUICHE_DCHECK(congestion_event.end_of_round_trip);
  if (congestion_event.last_sample_is_app_limited) {
    return APP_LIMITED;
  }

  QuicBandwidth threshold =
      full_bandwidth_baseline_ * Params().startup_full_bw_threshold;

  if (MaxBandwidth() >= threshold) {
    QUIC_DVLOG(3) << " CheckBandwidthGrowth at end of round. max_bandwidth:"
                  << MaxBandwidth() << ", threshold:" << threshold
                  << " (Still growing)  @ " << congestion_event.event_time;
    full_bandwidth_baseline_ = MaxBandwidth();
    rounds_without_bandwidth_growth_ = 0;
    return GROWTH;
  }

  ++rounds_without_bandwidth_growth_;
  BandwidthGrowth return_value = NO_GROWTH;
  if (rounds_without_bandwidth_growth_ >= Params().startup_full_bw_rounds) {
    full_bandwidth_reached_ = true;
    return_value = EXIT;
  }
  QUIC_DVLOG(3) << " CheckBandwidthGrowth at end of round. max_bandwidth:"
                << MaxBandwidth() << ", threshold:" << threshold
                << " rounds_without_growth:" << rounds_without_bandwidth_growth_
                << " full_bw_reached:" << full_bandwidth_reached_ << "  @ "
                << congestion_event.event_time;

  return return_value;
}

}  // namespace quic
