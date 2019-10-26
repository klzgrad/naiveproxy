// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

namespace {
// Sensitivity in response to losses. 0 means no loss response.
// 0.3 is also used by TCP bbr and cubic.
const float kBeta = 0.3;
}  // namespace

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

const SendTimeState& SendStateOfLargestPacket(
    const Bbr2CongestionEvent& congestion_event) {
  const auto& last_acked_sample = congestion_event.last_acked_sample;
  const auto& last_lost_sample = congestion_event.last_lost_sample;

  if (!last_lost_sample.packet_number.IsInitialized()) {
    return last_acked_sample.bandwidth_sample.state_at_send;
  }

  if (!last_acked_sample.packet_number.IsInitialized()) {
    return last_lost_sample.send_time_state;
  }

  DCHECK_NE(last_acked_sample.packet_number, last_lost_sample.packet_number);

  if (last_acked_sample.packet_number < last_lost_sample.packet_number) {
    return last_lost_sample.send_time_state;
  }
  return last_acked_sample.bandwidth_sample.state_at_send;
}

Bbr2NetworkModel::Bbr2NetworkModel(const Bbr2Params* params,
                                   QuicTime::Delta initial_rtt,
                                   QuicTime initial_rtt_timestamp,
                                   float cwnd_gain,
                                   float pacing_gain)
    : params_(params),
      bandwidth_sampler_(nullptr, params->initial_max_ack_height_filter_window),
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

  for (const auto& packet : acked_packets) {
    const BandwidthSample bandwidth_sample =
        bandwidth_sampler_.OnPacketAcknowledged(event_time,
                                                packet.packet_number);
    if (!bandwidth_sample.state_at_send.is_valid) {
      // From the sampler's perspective, the packet has never been sent, or
      // the packet has been acked or marked as lost previously.
      continue;
    }

    congestion_event->last_sample_is_app_limited =
        bandwidth_sample.state_at_send.is_app_limited;
    if (!bandwidth_sample.rtt.IsZero()) {
      congestion_event->sample_min_rtt =
          std::min(congestion_event->sample_min_rtt, bandwidth_sample.rtt);
    }
    if (!bandwidth_sample.state_at_send.is_app_limited ||
        bandwidth_sample.bandwidth > MaxBandwidth()) {
      congestion_event->sample_max_bandwidth = std::max(
          congestion_event->sample_max_bandwidth, bandwidth_sample.bandwidth);
    }

    if (bandwidth_sample.bandwidth > bandwidth_latest_) {
      bandwidth_latest_ = bandwidth_sample.bandwidth;
    }

    // |inflight_sample| is the total bytes acked while |packet| is inflight.
    QuicByteCount inflight_sample =
        total_bytes_acked() - bandwidth_sample.state_at_send.total_bytes_acked;
    if (inflight_sample > inflight_latest_) {
      inflight_latest_ = inflight_sample;
    }

    congestion_event->last_acked_sample = {packet.packet_number,
                                           bandwidth_sample, inflight_sample};
  }

  min_rtt_filter_.Update(congestion_event->sample_min_rtt, event_time);
  if (!congestion_event->sample_max_bandwidth.IsZero()) {
    max_bandwidth_filter_.Update(congestion_event->sample_max_bandwidth);
  }

  for (const LostPacket& packet : lost_packets) {
    const SendTimeState send_time_state =
        bandwidth_sampler_.OnPacketLost(packet.packet_number);
    if (send_time_state.is_valid) {
      congestion_event->last_lost_sample = {packet.packet_number,
                                            send_time_state};
    }
  }

  congestion_event->bytes_in_flight = bytes_in_flight();

  congestion_event->bytes_acked = total_bytes_acked() - prior_bytes_acked;
  congestion_event->bytes_lost = total_bytes_lost() - prior_bytes_lost;
  bytes_lost_in_round_ += congestion_event->bytes_lost;

  bandwidth_sampler_.OnAckEventEnd(BandwidthEstimate(), RoundTripCount());

  if (!congestion_event->end_of_round_trip) {
    return;
  }

  // Per round-trip updates.
  AdaptLowerBounds(*congestion_event);
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

    bandwidth_lo_ = std::max(bandwidth_latest_, bandwidth_lo_ * (1.0 - kBeta));
    QUIC_DVLOG(3) << "bandwidth_lo_ updated to " << bandwidth_lo_
                  << ", bandwidth_latest_ is " << bandwidth_latest_;

    inflight_lo_ =
        std::max<QuicByteCount>(inflight_latest_, inflight_lo_ * (1.0 - kBeta));
  }
}

void Bbr2NetworkModel::OnCongestionEventFinish(
    QuicPacketNumber least_unacked_packet,
    const Bbr2CongestionEvent& congestion_event) {
  if (congestion_event.end_of_round_trip) {
    const auto& last_acked_sample = congestion_event.last_acked_sample;
    if (last_acked_sample.bandwidth_sample.state_at_send.is_valid) {
      bandwidth_latest_ = last_acked_sample.bandwidth_sample.bandwidth;
      inflight_latest_ = last_acked_sample.inflight_sample;
    }

    bytes_lost_in_round_ = 0;
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
  const SendTimeState& send_state = SendStateOfLargestPacket(congestion_event);
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
  round_trip_counter_.RestartRound();
}

QuicByteCount Bbr2NetworkModel::inflight_hi_with_headroom() const {
  QuicByteCount headroom = inflight_hi_ * Params().inflight_hi_headroom;

  return inflight_hi_ > headroom ? inflight_hi_ - headroom : 0;
}

}  // namespace quic
