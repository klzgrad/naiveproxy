// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

RoundTripCounter::RoundTripCounter() : round_trip_count_(0) {}

void RoundTripCounter::OnPacketSent(QuicPacketNumber packet_number) {
  DCHECK(!last_sent_packet_.IsInitialized() ||
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
        if (GetQuicReloadableFlag(quic_bbr_copy_sampler_state_from_v1_to_v2) &&
            old_sampler != nullptr) {
          QUIC_RELOADABLE_FLAG_COUNT(quic_bbr_copy_sampler_state_from_v1_to_v2);
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
  if (!fix_zero_bw_on_loss_only_event_ ||
      (prior_bytes_acked != total_bytes_acked())) {
    QUIC_BUG_IF((prior_bytes_acked != total_bytes_acked()) &&
                sample.sample_max_bandwidth.IsZero())
        << total_bytes_acked() - prior_bytes_acked << " bytes from "
        << acked_packets.size()
        << " packets have been acked, but sample_max_bandwidth is zero.";
    if (!sample.sample_is_app_limited ||
        sample.sample_max_bandwidth > MaxBandwidth()) {
      congestion_event->sample_max_bandwidth = sample.sample_max_bandwidth;
      max_bandwidth_filter_.Update(congestion_event->sample_max_bandwidth);
    }
  } else {
    if (acked_packets.empty()) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr_fix_zero_bw_on_loss_only_event, 3,
                                   4);
    } else {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_bbr_fix_zero_bw_on_loss_only_event, 4,
                                   4);
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

  // |bandwidth_latest_| and |inflight_latest_| only increased within a round.
  if (sample.sample_max_bandwidth > bandwidth_latest_) {
    bandwidth_latest_ = sample.sample_max_bandwidth;
  }

  if (sample.sample_max_inflight > inflight_latest_) {
    inflight_latest_ = sample.sample_max_inflight;
  }

  if (!congestion_event->end_of_round_trip) {
    return;
  }

  // Per round-trip updates.
  AdaptLowerBounds(*congestion_event);

  if (!sample.sample_max_bandwidth.IsZero()) {
    bandwidth_latest_ = sample.sample_max_bandwidth;
  }

  if (sample.sample_max_inflight > 0) {
    inflight_latest_ = sample.sample_max_inflight;
  }
}

void Bbr2NetworkModel::AdaptLowerBounds(
    const Bbr2CongestionEvent& congestion_event) {
  if (!congestion_event.end_of_round_trip ||
      congestion_event.is_probing_for_bandwidth) {
    return;
  }

  if (bytes_lost_in_round_ > 0) {
    if (bandwidth_lo_.IsInfinite()) {
      bandwidth_lo_ = MaxBandwidth();
    }
    if (inflight_lo_ == inflight_lo_default()) {
      inflight_lo_ = congestion_event.prior_cwnd;
    }

    bandwidth_lo_ =
        std::max(bandwidth_latest_, bandwidth_lo_ * (1.0 - Params().beta));
    QUIC_DVLOG(3) << "bandwidth_lo_ updated to " << bandwidth_lo_
                  << ", bandwidth_latest_ is " << bandwidth_latest_;

    inflight_lo_ = std::max<QuicByteCount>(
        inflight_latest_, inflight_lo_ * (1.0 - Params().beta));
  }
}

void Bbr2NetworkModel::OnCongestionEventFinish(
    QuicPacketNumber least_unacked_packet,
    const Bbr2CongestionEvent& congestion_event) {
  if (congestion_event.end_of_round_trip) {
    bytes_lost_in_round_ = 0;
    loss_events_in_round_ = 0;
  }

  bandwidth_sampler_.RemoveObsoletePackets(least_unacked_packet);
}

void Bbr2NetworkModel::UpdateNetworkParameters(QuicBandwidth bandwidth,
                                               QuicTime::Delta rtt) {
  if (!bandwidth.IsInfinite() && bandwidth > MaxBandwidth()) {
    max_bandwidth_filter_.Update(bandwidth);
  }

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
    const Bbr2CongestionEvent& congestion_event) const {
  const SendTimeState& send_state = congestion_event.last_packet_send_state;
  if (!send_state.is_valid) {
    // Not enough information.
    return false;
  }

  const QuicByteCount inflight_at_send = BytesInFlight(send_state);
  // TODO(wub): Consider total_bytes_lost() - send_state.total_bytes_lost, which
  // is the total bytes lost when the largest numbered packet was inflight.
  // bytes_lost_in_round_, OTOH, is the total bytes lost in the "current" round.
  const QuicByteCount bytes_lost_in_round = bytes_lost_in_round_;

  QUIC_DVLOG(3) << "IsInflightTooHigh: bytes_lost_in_round:"
                << bytes_lost_in_round << ", lost_in_round_threshold:"
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

void Bbr2NetworkModel::RestartRound() {
  bytes_lost_in_round_ = 0;
  loss_events_in_round_ = 0;
  round_trip_counter_.RestartRound();
}

QuicByteCount Bbr2NetworkModel::inflight_hi_with_headroom() const {
  QuicByteCount headroom = inflight_hi_ * Params().inflight_hi_headroom;

  return inflight_hi_ > headroom ? inflight_hi_ - headroom : 0;
}

}  // namespace quic
