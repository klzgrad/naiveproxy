// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

std::ostream& operator<<(std::ostream& os, const SendTimeState& s) {
  os << "{valid:" << s.is_valid << ", app_limited:" << s.is_app_limited
     << ", total_sent:" << s.total_bytes_sent
     << ", total_acked:" << s.total_bytes_acked
     << ", total_lost:" << s.total_bytes_lost
     << ", inflight:" << s.bytes_in_flight << "}";
  return os;
}

QuicByteCount MaxAckHeightTracker::Update(QuicBandwidth bandwidth_estimate,
                                          QuicRoundTripCount round_trip_count,
                                          QuicTime ack_time,
                                          QuicByteCount bytes_acked) {
  if (aggregation_epoch_start_time_ == QuicTime::Zero()) {
    aggregation_epoch_bytes_ = bytes_acked;
    aggregation_epoch_start_time_ = ack_time;
    ++num_ack_aggregation_epochs_;
    return 0;
  }

  // Compute how many bytes are expected to be delivered, assuming max bandwidth
  // is correct.
  QuicByteCount expected_bytes_acked =
      bandwidth_estimate * (ack_time - aggregation_epoch_start_time_);
  // Reset the current aggregation epoch as soon as the ack arrival rate is less
  // than or equal to the max bandwidth.
  if (aggregation_epoch_bytes_ <=
      ack_aggregation_bandwidth_threshold_ * expected_bytes_acked) {
    QUIC_DVLOG(3) << "Starting a new aggregation epoch because "
                     "aggregation_epoch_bytes_ "
                  << aggregation_epoch_bytes_
                  << " is smaller than expected. "
                     "ack_aggregation_bandwidth_threshold_:"
                  << ack_aggregation_bandwidth_threshold_
                  << ", expected_bytes_acked:" << expected_bytes_acked
                  << ", bandwidth_estimate:" << bandwidth_estimate
                  << ", aggregation_duration:"
                  << (ack_time - aggregation_epoch_start_time_)
                  << ", new_aggregation_epoch:" << ack_time
                  << ", new_aggregation_bytes_acked:" << bytes_acked;
    // Reset to start measuring a new aggregation epoch.
    aggregation_epoch_bytes_ = bytes_acked;
    aggregation_epoch_start_time_ = ack_time;
    ++num_ack_aggregation_epochs_;
    return 0;
  }

  aggregation_epoch_bytes_ += bytes_acked;

  // Compute how many extra bytes were delivered vs max bandwidth.
  QuicByteCount extra_bytes_acked =
      aggregation_epoch_bytes_ - expected_bytes_acked;
  QUIC_DVLOG(3) << "Updating MaxAckHeight. ack_time:" << ack_time
                << ", round trip count:" << round_trip_count
                << ", bandwidth_estimate:" << bandwidth_estimate
                << ", bytes_acked:" << bytes_acked
                << ", expected_bytes_acked:" << expected_bytes_acked
                << ", aggregation_epoch_bytes_:" << aggregation_epoch_bytes_
                << ", extra_bytes_acked:" << extra_bytes_acked;
  max_ack_height_filter_.Update(extra_bytes_acked, round_trip_count);
  return extra_bytes_acked;
}

BandwidthSampler::BandwidthSampler(
    const QuicUnackedPacketMap* unacked_packet_map,
    QuicRoundTripCount max_height_tracker_window_length)
    : total_bytes_sent_(0),
      total_bytes_acked_(0),
      total_bytes_lost_(0),
      total_bytes_neutered_(0),
      total_bytes_sent_at_last_acked_packet_(0),
      last_acked_packet_sent_time_(QuicTime::Zero()),
      last_acked_packet_ack_time_(QuicTime::Zero()),
      is_app_limited_(started_as_app_limited_),
      connection_state_map_(),
      max_tracked_packets_(GetQuicFlag(FLAGS_quic_max_tracked_packet_count)),
      unacked_packet_map_(unacked_packet_map),
      max_ack_height_tracker_(max_height_tracker_window_length),
      total_bytes_acked_after_last_ack_event_(0),
      overestimate_avoidance_(false) {}

BandwidthSampler::BandwidthSampler(const BandwidthSampler& other)
    : total_bytes_sent_(other.total_bytes_sent_),
      total_bytes_acked_(other.total_bytes_acked_),
      total_bytes_lost_(other.total_bytes_lost_),
      total_bytes_neutered_(other.total_bytes_neutered_),
      total_bytes_sent_at_last_acked_packet_(
          other.total_bytes_sent_at_last_acked_packet_),
      last_acked_packet_sent_time_(other.last_acked_packet_sent_time_),
      last_acked_packet_ack_time_(other.last_acked_packet_ack_time_),
      last_sent_packet_(other.last_sent_packet_),
      started_as_app_limited_(other.started_as_app_limited_),
      is_app_limited_(other.is_app_limited_),
      end_of_app_limited_phase_(other.end_of_app_limited_phase_),
      connection_state_map_(other.connection_state_map_),
      recent_ack_points_(other.recent_ack_points_),
      a0_candidates_(other.a0_candidates_),
      max_tracked_packets_(other.max_tracked_packets_),
      unacked_packet_map_(other.unacked_packet_map_),
      max_ack_height_tracker_(other.max_ack_height_tracker_),
      total_bytes_acked_after_last_ack_event_(
          other.total_bytes_acked_after_last_ack_event_),
      overestimate_avoidance_(other.overestimate_avoidance_) {}

void BandwidthSampler::EnableOverestimateAvoidance() {
  if (overestimate_avoidance_) {
    return;
  }

  overestimate_avoidance_ = true;
  // TODO(wub): Change the default value of
  // --quic_ack_aggregation_bandwidth_threshold to 2.0.
  max_ack_height_tracker_.SetAckAggregationBandwidthThreshold(2.0);
}

BandwidthSampler::~BandwidthSampler() {}

void BandwidthSampler::OnPacketSent(
    QuicTime sent_time,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    QuicByteCount bytes_in_flight,
    HasRetransmittableData has_retransmittable_data) {
  last_sent_packet_ = packet_number;

  if (has_retransmittable_data != HAS_RETRANSMITTABLE_DATA) {
    return;
  }

  total_bytes_sent_ += bytes;

  // If there are no packets in flight, the time at which the new transmission
  // opens can be treated as the A_0 point for the purpose of bandwidth
  // sampling. This underestimates bandwidth to some extent, and produces some
  // artificially low samples for most packets in flight, but it provides with
  // samples at important points where we would not have them otherwise, most
  // importantly at the beginning of the connection.
  if (bytes_in_flight == 0) {
    last_acked_packet_ack_time_ = sent_time;
    if (overestimate_avoidance_) {
      recent_ack_points_.Clear();
      recent_ack_points_.Update(sent_time, total_bytes_acked_);
      a0_candidates_.clear();
      a0_candidates_.push_back(recent_ack_points_.MostRecentPoint());
    }
    total_bytes_sent_at_last_acked_packet_ = total_bytes_sent_;

    // In this situation ack compression is not a concern, set send rate to
    // effectively infinite.
    last_acked_packet_sent_time_ = sent_time;
  }

  if (!connection_state_map_.IsEmpty() &&
      packet_number >
          connection_state_map_.last_packet() + max_tracked_packets_) {
    if (unacked_packet_map_ != nullptr) {
      QUIC_BUG << "BandwidthSampler in-flight packet map has exceeded maximum "
                  "number of tracked packets("
               << max_tracked_packets_
               << ").  First tracked: " << connection_state_map_.first_packet()
               << "; last tracked: " << connection_state_map_.last_packet()
               << "; least unacked: " << unacked_packet_map_->GetLeastUnacked()
               << "; packet number: " << packet_number << "; largest observed: "
               << unacked_packet_map_->largest_acked();
    } else {
      QUIC_BUG << "BandwidthSampler in-flight packet map has exceeded maximum "
                  "number of tracked packets.";
    }
  }

  bool success = connection_state_map_.Emplace(packet_number, sent_time, bytes,
                                               bytes_in_flight + bytes, *this);
  QUIC_BUG_IF(!success) << "BandwidthSampler failed to insert the packet "
                           "into the map, most likely because it's already "
                           "in it.";
}

void BandwidthSampler::OnPacketNeutered(QuicPacketNumber packet_number) {
  connection_state_map_.Remove(
      packet_number, [&](const ConnectionStateOnSentPacket& sent_packet) {
        QUIC_CODE_COUNT(quic_bandwidth_sampler_packet_neutered);
        total_bytes_neutered_ += sent_packet.size;
      });
}

BandwidthSamplerInterface::CongestionEventSample
BandwidthSampler::OnCongestionEvent(QuicTime ack_time,
                                    const AckedPacketVector& acked_packets,
                                    const LostPacketVector& lost_packets,
                                    QuicBandwidth max_bandwidth,
                                    QuicBandwidth est_bandwidth_upper_bound,
                                    QuicRoundTripCount round_trip_count) {
  CongestionEventSample event_sample;

  SendTimeState last_lost_packet_send_state;

  for (const LostPacket& packet : lost_packets) {
    SendTimeState send_state =
        OnPacketLost(packet.packet_number, packet.bytes_lost);
    if (send_state.is_valid) {
      last_lost_packet_send_state = send_state;
    }
  }

  if (acked_packets.empty()) {
    // Only populate send state for a loss-only event.
    event_sample.last_packet_send_state = last_lost_packet_send_state;
    return event_sample;
  }

  SendTimeState last_acked_packet_send_state;
  for (const auto& packet : acked_packets) {
    BandwidthSample sample =
        OnPacketAcknowledged(ack_time, packet.packet_number);
    if (!sample.state_at_send.is_valid) {
      continue;
    }

    last_acked_packet_send_state = sample.state_at_send;

    if (!sample.rtt.IsZero()) {
      event_sample.sample_rtt = std::min(event_sample.sample_rtt, sample.rtt);
    }
    if (sample.bandwidth > event_sample.sample_max_bandwidth) {
      event_sample.sample_max_bandwidth = sample.bandwidth;
      event_sample.sample_is_app_limited = sample.state_at_send.is_app_limited;
    }
    const QuicByteCount inflight_sample =
        total_bytes_acked() - last_acked_packet_send_state.total_bytes_acked;
    if (inflight_sample > event_sample.sample_max_inflight) {
      event_sample.sample_max_inflight = inflight_sample;
    }
  }

  if (!last_lost_packet_send_state.is_valid) {
    event_sample.last_packet_send_state = last_acked_packet_send_state;
  } else if (!last_acked_packet_send_state.is_valid) {
    event_sample.last_packet_send_state = last_lost_packet_send_state;
  } else {
    // If two packets are inflight and an alarm is armed to lose a packet and it
    // wakes up late, then the first of two in flight packets could have been
    // acknowledged before the wakeup, which re-evaluates loss detection, and
    // could declare the later of the two lost.
    event_sample.last_packet_send_state =
        lost_packets.back().packet_number > acked_packets.back().packet_number
            ? last_lost_packet_send_state
            : last_acked_packet_send_state;
  }

  max_bandwidth = std::max(max_bandwidth, event_sample.sample_max_bandwidth);
  event_sample.extra_acked = OnAckEventEnd(
      std::min(est_bandwidth_upper_bound, max_bandwidth), round_trip_count);

  return event_sample;
}

QuicByteCount BandwidthSampler::OnAckEventEnd(
    QuicBandwidth bandwidth_estimate,
    QuicRoundTripCount round_trip_count) {
  const QuicByteCount newly_acked_bytes =
      total_bytes_acked_ - total_bytes_acked_after_last_ack_event_;

  if (newly_acked_bytes == 0) {
    return 0;
  }
  total_bytes_acked_after_last_ack_event_ = total_bytes_acked_;

  QuicByteCount extra_acked = max_ack_height_tracker_.Update(
      bandwidth_estimate, round_trip_count, last_acked_packet_ack_time_,
      newly_acked_bytes);
  // If |extra_acked| is zero, i.e. this ack event marks the start of a new ack
  // aggregation epoch, save LessRecentPoint, which is the last ack point of the
  // previous epoch, as a A0 candidate.
  if (overestimate_avoidance_ && extra_acked == 0) {
    a0_candidates_.push_back(recent_ack_points_.LessRecentPoint());
    QUIC_DVLOG(1) << "New a0_candidate:" << a0_candidates_.back();
  }
  return extra_acked;
}

BandwidthSample BandwidthSampler::OnPacketAcknowledged(
    QuicTime ack_time,
    QuicPacketNumber packet_number) {
  ConnectionStateOnSentPacket* sent_packet_pointer =
      connection_state_map_.GetEntry(packet_number);
  if (sent_packet_pointer == nullptr) {
    // See the TODO below.
    return BandwidthSample();
  }
  BandwidthSample sample =
      OnPacketAcknowledgedInner(ack_time, packet_number, *sent_packet_pointer);
  return sample;
}

BandwidthSample BandwidthSampler::OnPacketAcknowledgedInner(
    QuicTime ack_time,
    QuicPacketNumber packet_number,
    const ConnectionStateOnSentPacket& sent_packet) {
  total_bytes_acked_ += sent_packet.size;
  total_bytes_sent_at_last_acked_packet_ =
      sent_packet.send_time_state.total_bytes_sent;
  last_acked_packet_sent_time_ = sent_packet.sent_time;
  last_acked_packet_ack_time_ = ack_time;
  if (overestimate_avoidance_) {
    recent_ack_points_.Update(ack_time, total_bytes_acked_);
  }

  if (started_as_app_limited_) {
    if (is_app_limited_) {
      // Exit app-limited phase in two cases:
      // (1) end_of_app_limited_phase_ is not initialized, i.e., so far all
      // packets are sent while there are buffered packets or pending data.
      // (2) The current acked packet is after the sent packet marked as the end
      // of the app limit phase.
      if (!end_of_app_limited_phase_.IsInitialized() ||
          packet_number > end_of_app_limited_phase_) {
        QUIC_RELOADABLE_FLAG_COUNT(quic_bw_sampler_app_limited_starting_value);
        is_app_limited_ = false;
      }
    }
  } else {
    // Exit app-limited phase once a packet that was sent while the connection
    // is not app-limited is acknowledged.
    if (is_app_limited_ && end_of_app_limited_phase_.IsInitialized() &&
        packet_number > end_of_app_limited_phase_) {
      is_app_limited_ = false;
    }
  }

  // There might have been no packets acknowledged at the moment when the
  // current packet was sent. In that case, there is no bandwidth sample to
  // make.
  if (sent_packet.last_acked_packet_sent_time == QuicTime::Zero()) {
    QUIC_BUG << "sent_packet.last_acked_packet_sent_time is zero";
    return BandwidthSample();
  }

  // Infinite rate indicates that the sampler is supposed to discard the
  // current send rate sample and use only the ack rate.
  QuicBandwidth send_rate = QuicBandwidth::Infinite();
  if (sent_packet.sent_time > sent_packet.last_acked_packet_sent_time) {
    send_rate = QuicBandwidth::FromBytesAndTimeDelta(
        sent_packet.send_time_state.total_bytes_sent -
            sent_packet.total_bytes_sent_at_last_acked_packet,
        sent_packet.sent_time - sent_packet.last_acked_packet_sent_time);
  }

  AckPoint a0;
  if (overestimate_avoidance_ &&
      ChooseA0Point(sent_packet.send_time_state.total_bytes_acked, &a0)) {
    QUIC_DVLOG(2) << "Using a0 point: " << a0;
  } else {
    a0.ack_time = sent_packet.last_acked_packet_ack_time,
    a0.total_bytes_acked = sent_packet.send_time_state.total_bytes_acked;
  }

  // During the slope calculation, ensure that ack time of the current packet is
  // always larger than the time of the previous packet, otherwise division by
  // zero or integer underflow can occur.
  if (ack_time <= a0.ack_time) {
    // TODO(wub): Compare this code count before and after fixing clock jitter
    // issue.
    if (a0.ack_time == sent_packet.sent_time) {
      // This is the 1st packet after quiescense.
      QUIC_CODE_COUNT_N(quic_prev_ack_time_larger_than_current_ack_time, 1, 2);
    } else {
      QUIC_CODE_COUNT_N(quic_prev_ack_time_larger_than_current_ack_time, 2, 2);
    }
    QUIC_BUG << "Time of the previously acked packet:"
             << a0.ack_time.ToDebuggingValue()
             << " is larger than the ack time of the current packet:"
             << ack_time.ToDebuggingValue()
             << ". acked packet number:" << packet_number
             << ", total_bytes_acked_:" << total_bytes_acked_
             << ", overestimate_avoidance_:" << overestimate_avoidance_
             << ", sent_packet:" << sent_packet;
    return BandwidthSample();
  }
  QuicBandwidth ack_rate = QuicBandwidth::FromBytesAndTimeDelta(
      total_bytes_acked_ - a0.total_bytes_acked, ack_time - a0.ack_time);

  BandwidthSample sample;
  sample.bandwidth = std::min(send_rate, ack_rate);
  // Note: this sample does not account for delayed acknowledgement time.  This
  // means that the RTT measurements here can be artificially high, especially
  // on low bandwidth connections.
  sample.rtt = ack_time - sent_packet.sent_time;
  SentPacketToSendTimeState(sent_packet, &sample.state_at_send);

  QUIC_BUG_IF(sample.bandwidth.IsZero())
      << "ack_rate: " << ack_rate << ", send_rate: " << send_rate
      << ". acked packet number:" << packet_number
      << ", overestimate_avoidance_:" << overestimate_avoidance_ << "a1:{"
      << total_bytes_acked_ << "@" << ack_time << "}, a0:{"
      << a0.total_bytes_acked << "@" << a0.ack_time
      << "}, sent_packet:" << sent_packet;
  return sample;
}

bool BandwidthSampler::ChooseA0Point(QuicByteCount total_bytes_acked,
                                     AckPoint* a0) {
  if (a0_candidates_.empty()) {
    QUIC_BUG << "No A0 point candicates. total_bytes_acked:"
             << total_bytes_acked;
    return false;
  }

  if (a0_candidates_.size() == 1) {
    *a0 = a0_candidates_.front();
    return true;
  }

  for (size_t i = 1; i < a0_candidates_.size(); ++i) {
    if (a0_candidates_[i].total_bytes_acked > total_bytes_acked) {
      *a0 = a0_candidates_[i - 1];
      if (i > 1) {
        a0_candidates_.pop_front_n(i - 1);
      }
      return true;
    }
  }

  // All candidates' total_bytes_acked is <= |total_bytes_acked|.
  *a0 = a0_candidates_.back();
  a0_candidates_.pop_front_n(a0_candidates_.size() - 1);
  return true;
}

SendTimeState BandwidthSampler::OnPacketLost(QuicPacketNumber packet_number,
                                             QuicPacketLength bytes_lost) {
  // TODO(vasilvv): see the comment for the case of missing packets in
  // BandwidthSampler::OnPacketAcknowledged on why this does not raise a
  // QUIC_BUG when removal fails.
  SendTimeState send_time_state;

  total_bytes_lost_ += bytes_lost;
  ConnectionStateOnSentPacket* sent_packet_pointer =
      connection_state_map_.GetEntry(packet_number);
  if (sent_packet_pointer != nullptr) {
    SentPacketToSendTimeState(*sent_packet_pointer, &send_time_state);
  }

  return send_time_state;
}

void BandwidthSampler::SentPacketToSendTimeState(
    const ConnectionStateOnSentPacket& sent_packet,
    SendTimeState* send_time_state) const {
  *send_time_state = sent_packet.send_time_state;
  send_time_state->is_valid = true;
}

void BandwidthSampler::OnAppLimited() {
  is_app_limited_ = true;
  end_of_app_limited_phase_ = last_sent_packet_;
}

void BandwidthSampler::RemoveObsoletePackets(QuicPacketNumber least_unacked) {
  // A packet can become obsolete when it is removed from QuicUnackedPacketMap's
  // view of inflight before it is acked or marked as lost. For example, when
  // QuicSentPacketManager::RetransmitCryptoPackets retransmits a crypto packet,
  // the packet is removed from QuicUnackedPacketMap's inflight, but is not
  // marked as acked or lost in the BandwidthSampler.
  connection_state_map_.RemoveUpTo(least_unacked);
}

QuicByteCount BandwidthSampler::total_bytes_sent() const {
  return total_bytes_sent_;
}

QuicByteCount BandwidthSampler::total_bytes_acked() const {
  return total_bytes_acked_;
}

QuicByteCount BandwidthSampler::total_bytes_lost() const {
  return total_bytes_lost_;
}

QuicByteCount BandwidthSampler::total_bytes_neutered() const {
  return total_bytes_neutered_;
}

bool BandwidthSampler::is_app_limited() const {
  return is_app_limited_;
}

QuicPacketNumber BandwidthSampler::end_of_app_limited_phase() const {
  return end_of_app_limited_phase_;
}

}  // namespace quic
