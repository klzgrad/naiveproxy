// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/pacing_sender.h"

#include "net/quic/platform/api/quic_logging.h"

namespace net {
namespace {

// The estimated system alarm granularity.
static const QuicTime::Delta kAlarmGranularity =
    QuicTime::Delta::FromMilliseconds(1);

// Configured maximum size of the burst coming out of quiescence.  The burst
// is never larger than the current CWND in packets.
static const uint32_t kInitialUnpacedBurst = 10;

}  // namespace

PacingSender::PacingSender()
    : sender_(nullptr),
      max_pacing_rate_(QuicBandwidth::Zero()),
      burst_tokens_(kInitialUnpacedBurst),
      last_delayed_packet_sent_time_(QuicTime::Zero()),
      ideal_next_packet_send_time_(QuicTime::Zero()),
      was_last_send_delayed_(false),
      initial_burst_size_(kInitialUnpacedBurst) {}

PacingSender::~PacingSender() {}

void PacingSender::set_sender(SendAlgorithmInterface* sender) {
  DCHECK(sender != nullptr);
  sender_ = sender;
}

void PacingSender::OnCongestionEvent(bool rtt_updated,
                                     QuicByteCount bytes_in_flight,
                                     QuicTime event_time,
                                     const AckedPacketVector& acked_packets,
                                     const LostPacketVector& lost_packets) {
  DCHECK(sender_ != nullptr);
  if (!lost_packets.empty()) {
    // Clear any burst tokens when entering recovery.
    burst_tokens_ = 0;
  }
  sender_->OnCongestionEvent(rtt_updated, bytes_in_flight, event_time,
                             acked_packets, lost_packets);
}

void PacingSender::OnPacketSent(
    QuicTime sent_time,
    QuicByteCount bytes_in_flight,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData has_retransmittable_data) {
  DCHECK(sender_ != nullptr);
  sender_->OnPacketSent(sent_time, bytes_in_flight, packet_number, bytes,
                        has_retransmittable_data);
  if (has_retransmittable_data != HAS_RETRANSMITTABLE_DATA) {
    return;
  }
  // If in recovery, the connection is not coming out of quiescence.
  if (bytes_in_flight == 0 && !sender_->InRecovery()) {
    // Add more burst tokens anytime the connection is leaving quiescence, but
    // limit it to the equivalent of a single bulk write, not exceeding the
    // current CWND in packets.
    burst_tokens_ = std::min(
        initial_burst_size_,
        static_cast<uint32_t>(sender_->GetCongestionWindow() / kDefaultTCPMSS));
  }
  if (burst_tokens_ > 0) {
    --burst_tokens_;
    was_last_send_delayed_ = false;
    last_delayed_packet_sent_time_ = QuicTime::Zero();
    ideal_next_packet_send_time_ = QuicTime::Zero();
    return;
  }
  // The next packet should be sent as soon as the current packet has been
  // transferred.  PacingRate is based on bytes in flight including this packet.
  QuicTime::Delta delay =
      PacingRate(bytes_in_flight + bytes).TransferTime(bytes);
  // If the last send was delayed, and the alarm took a long time to get
  // invoked, allow the connection to make up for lost time.
  if (was_last_send_delayed_) {
    ideal_next_packet_send_time_ = ideal_next_packet_send_time_ + delay;
    // The send was application limited if it takes longer than the
    // pacing delay between sent packets.
    const bool application_limited =
        last_delayed_packet_sent_time_.IsInitialized() &&
        sent_time > last_delayed_packet_sent_time_ + delay;
    const bool making_up_for_lost_time =
        ideal_next_packet_send_time_ <= sent_time;
    // As long as we're making up time and not application limited,
    // continue to consider the packets delayed, allowing the packets to be
    // sent immediately.
    if (making_up_for_lost_time && !application_limited) {
      last_delayed_packet_sent_time_ = sent_time;
    } else {
      was_last_send_delayed_ = false;
      last_delayed_packet_sent_time_ = QuicTime::Zero();
    }
  } else {
    ideal_next_packet_send_time_ =
        std::max(ideal_next_packet_send_time_ + delay, sent_time + delay);
  }
}

QuicTime::Delta PacingSender::TimeUntilSend(QuicTime now,
                                            QuicByteCount bytes_in_flight) {
  DCHECK(sender_ != nullptr);

  if (!sender_->CanSend(bytes_in_flight)) {
    // The underlying sender prevents sending.
    return QuicTime::Delta::Infinite();
  }

  if (burst_tokens_ > 0 || bytes_in_flight == 0) {
    // Don't pace if we have burst tokens available or leaving quiescence.
    return QuicTime::Delta::Zero();
  }

  // If the next send time is within the alarm granularity, send immediately.
  if (ideal_next_packet_send_time_ > now + kAlarmGranularity) {
    QUIC_DVLOG(1) << "Delaying packet: "
                  << (ideal_next_packet_send_time_ - now).ToMicroseconds();
    was_last_send_delayed_ = true;
    return ideal_next_packet_send_time_ - now;
  }

  QUIC_DVLOG(1) << "Sending packet now";
  return QuicTime::Delta::Zero();
}

QuicBandwidth PacingSender::PacingRate(QuicByteCount bytes_in_flight) const {
  DCHECK(sender_ != nullptr);
  if (!max_pacing_rate_.IsZero()) {
    return QuicBandwidth::FromBitsPerSecond(
        std::min(max_pacing_rate_.ToBitsPerSecond(),
                 sender_->PacingRate(bytes_in_flight).ToBitsPerSecond()));
  }
  return sender_->PacingRate(bytes_in_flight);
}

}  // namespace net
