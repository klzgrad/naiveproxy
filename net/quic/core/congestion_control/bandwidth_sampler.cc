// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/bandwidth_sampler.h"

#include <algorithm>

#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"

namespace net {
BandwidthSampler::BandwidthSampler()
    : total_bytes_sent_(0),
      total_bytes_acked_(0),
      total_bytes_sent_at_last_acked_packet_(0),
      last_acked_packet_sent_time_(QuicTime::Zero()),
      last_acked_packet_ack_time_(QuicTime::Zero()),
      last_sent_packet_(0),
      is_app_limited_(false),
      end_of_app_limited_phase_(0),
      connection_state_map_() {}

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
    total_bytes_sent_at_last_acked_packet_ = total_bytes_sent_;

    // In this situation ack compression is not a concern, set send rate to
    // effectively infinite.
    last_acked_packet_sent_time_ = sent_time;
  }

  if (!connection_state_map_.IsEmpty() &&
      packet_number >
          connection_state_map_.last_packet() + kMaxTrackedPackets) {
    QUIC_BUG << "BandwidthSampler in-flight packet map has exceeded maximum "
                "number "
                "of tracked packets.";
  }

  bool success =
      connection_state_map_.Emplace(packet_number, sent_time, bytes, *this);
  QUIC_BUG_IF(!success) << "BandwidthSampler failed to insert the packet "
                           "into the map, most likely because it's already "
                           "in it.";
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
  connection_state_map_.Remove(packet_number);
  return sample;
}

BandwidthSample BandwidthSampler::OnPacketAcknowledgedInner(
    QuicTime ack_time,
    QuicPacketNumber packet_number,
    const ConnectionStateOnSentPacket& sent_packet) {
  total_bytes_acked_ += sent_packet.size;
  total_bytes_sent_at_last_acked_packet_ = sent_packet.total_bytes_sent;
  last_acked_packet_sent_time_ = sent_packet.sent_time;
  last_acked_packet_ack_time_ = ack_time;

  // Exit app-limited phase once a packet that was sent while the connection is
  // not app-limited is acknowledged.
  if (is_app_limited_ && packet_number > end_of_app_limited_phase_) {
    is_app_limited_ = false;
  }

  // There might have been no packets acknowledged at the moment when the
  // current packet was sent. In that case, there is no bandwidth sample to
  // make.
  if (sent_packet.last_acked_packet_sent_time == QuicTime::Zero()) {
    return BandwidthSample();
  }

  // Infinite rate indicates that the sampler is supposed to discard the
  // current send rate sample and use only the ack rate.
  QuicBandwidth send_rate = QuicBandwidth::Infinite();
  if (sent_packet.sent_time > sent_packet.last_acked_packet_sent_time) {
    send_rate = QuicBandwidth::FromBytesAndTimeDelta(
        sent_packet.total_bytes_sent -
            sent_packet.total_bytes_sent_at_last_acked_packet,
        sent_packet.sent_time - sent_packet.last_acked_packet_sent_time);
  }

  // During the slope calculation, ensure that ack time of the current packet is
  // always larger than the time of the previous packet, otherwise division by
  // zero or integer underflow can occur.
  if (ack_time <= sent_packet.last_acked_packet_ack_time) {
    QUIC_BUG << "Time of the previously acked packet is larger than the time "
                "of the current packet.";
    return BandwidthSample();
  }
  QuicBandwidth ack_rate = QuicBandwidth::FromBytesAndTimeDelta(
      total_bytes_acked_ -
          sent_packet.total_bytes_acked_at_the_last_acked_packet,
      ack_time - sent_packet.last_acked_packet_ack_time);

  BandwidthSample sample;
  sample.bandwidth = std::min(send_rate, ack_rate);
  // Note: this sample does not account for delayed acknowledgement time.  This
  // means that the RTT measurements here can be artificially high, especially
  // on low bandwidth connections.
  sample.rtt = ack_time - sent_packet.sent_time;
  // A sample is app-limited if the packet was sent during the app-limited
  // phase.
  sample.is_app_limited = sent_packet.is_app_limited;
  return sample;
}

void BandwidthSampler::OnPacketLost(QuicPacketNumber packet_number) {
  // TODO(vasilvv): see the comment for the case of missing packets in
  // BandwidthSampler::OnPacketAcknowledged on why this does not raise a
  // QUIC_BUG when removal fails.
  connection_state_map_.Remove(packet_number);
}

void BandwidthSampler::OnAppLimited() {
  is_app_limited_ = true;
  end_of_app_limited_phase_ = last_sent_packet_;
}

void BandwidthSampler::RemoveObsoletePackets(QuicPacketNumber least_unacked) {
  while (!connection_state_map_.IsEmpty() &&
         connection_state_map_.first_packet() < least_unacked) {
    connection_state_map_.Remove(connection_state_map_.first_packet());
  }
}

QuicByteCount BandwidthSampler::total_bytes_acked() const {
  return total_bytes_acked_;
}

bool BandwidthSampler::is_app_limited() const {
  return is_app_limited_;
}

QuicPacketNumber BandwidthSampler::end_of_app_limited_phase() const {
  return end_of_app_limited_phase_;
}

}  // namespace net
