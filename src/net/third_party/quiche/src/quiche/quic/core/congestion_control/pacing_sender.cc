// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/congestion_control/pacing_sender.h"

#include <algorithm>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {
namespace {

// Configured maximum size of the burst coming out of quiescence.  The burst
// is never larger than the current CWND in packets.
static const uint32_t kInitialUnpacedBurst = 10;

}  // namespace

PacingSender::PacingSender()
    : sender_(nullptr),
      max_pacing_rate_(QuicBandwidth::Zero()),
      application_driven_pacing_rate_(QuicBandwidth::Infinite()),
      burst_tokens_(kInitialUnpacedBurst),
      ideal_next_packet_send_time_(QuicTime::Zero()),
      initial_burst_size_(kInitialUnpacedBurst),
      lumpy_tokens_(0),
      pacing_limited_(false) {}

PacingSender::~PacingSender() {}

void PacingSender::set_sender(SendAlgorithmInterface* sender) {
  QUICHE_DCHECK(sender != nullptr);
  sender_ = sender;
}

void PacingSender::OnCongestionEvent(bool rtt_updated,
                                     QuicByteCount bytes_in_flight,
                                     QuicTime event_time,
                                     const AckedPacketVector& acked_packets,
                                     const LostPacketVector& lost_packets,
                                     QuicPacketCount num_ect,
                                     QuicPacketCount num_ce) {
  QUICHE_DCHECK(sender_ != nullptr);
  if (!lost_packets.empty()) {
    // Clear any burst tokens when entering recovery.
    burst_tokens_ = 0;
  }
  sender_->OnCongestionEvent(rtt_updated, bytes_in_flight, event_time,
                             acked_packets, lost_packets, num_ect, num_ce);
}

void PacingSender::OnPacketSent(
    QuicTime sent_time, QuicByteCount bytes_in_flight,
    QuicPacketNumber packet_number, QuicByteCount bytes,
    HasRetransmittableData has_retransmittable_data) {
  QUICHE_DCHECK(sender_ != nullptr);
  QUIC_DVLOG(3) << "Packet " << packet_number << " with " << bytes
                << " bytes sent at " << sent_time
                << ". bytes_in_flight: " << bytes_in_flight;
  sender_->OnPacketSent(sent_time, bytes_in_flight, packet_number, bytes,
                        has_retransmittable_data);
  if (has_retransmittable_data != HAS_RETRANSMITTABLE_DATA) {
    return;
  }

  if (remove_non_initial_burst_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_pacing_remove_non_initial_burst, 1, 2);
  } else {
    // If in recovery, the connection is not coming out of quiescence.
    if (bytes_in_flight == 0 && !sender_->InRecovery()) {
      // Add more burst tokens anytime the connection is leaving quiescence, but
      // limit it to the equivalent of a single bulk write, not exceeding the
      // current CWND in packets.
      burst_tokens_ =
          std::min(initial_burst_size_,
                   static_cast<uint32_t>(sender_->GetCongestionWindow() /
                                         kDefaultTCPMSS));
    }
  }

  if (burst_tokens_ > 0) {
    --burst_tokens_;
    ideal_next_packet_send_time_ = QuicTime::Zero();
    pacing_limited_ = false;
    return;
  }

  // The next packet should be sent as soon as the current packet has been
  // transferred.  PacingRate is based on bytes in flight including this packet.
  QuicTime::Delta delay =
      PacingRate(bytes_in_flight + bytes).TransferTime(bytes);
  if (!pacing_limited_ || lumpy_tokens_ == 0) {
    // Reset lumpy_tokens_ if either application or cwnd throttles sending or
    // token runs out.
    lumpy_tokens_ = std::max(
        1u, std::min(static_cast<uint32_t>(GetQuicFlag(quic_lumpy_pacing_size)),
                     static_cast<uint32_t>(
                         (sender_->GetCongestionWindow() *
                          GetQuicFlag(quic_lumpy_pacing_cwnd_fraction)) /
                         kDefaultTCPMSS)));
    if (sender_->BandwidthEstimate() <
        QuicBandwidth::FromKBitsPerSecond(
            GetQuicFlag(quic_lumpy_pacing_min_bandwidth_kbps))) {
      // Below 1.2Mbps, send 1 packet at once, because one full-sized packet
      // is about 10ms of queueing.
      lumpy_tokens_ = 1u;
    }
    if ((bytes_in_flight + bytes) >= sender_->GetCongestionWindow()) {
      // Don't add lumpy_tokens if the congestion controller is CWND limited.
      lumpy_tokens_ = 1u;
    }
  }
  --lumpy_tokens_;
  if (pacing_limited_) {
    // Make up for lost time since pacing throttles the sending.
    ideal_next_packet_send_time_ = ideal_next_packet_send_time_ + delay;
  } else {
    ideal_next_packet_send_time_ =
        std::max(ideal_next_packet_send_time_ + delay, sent_time + delay);
  }
  // Stop making up for lost time if underlying sender prevents sending.
  pacing_limited_ = sender_->CanSend(bytes_in_flight + bytes);
}

void PacingSender::OnApplicationLimited() {
  // The send is application limited, stop making up for lost time.
  pacing_limited_ = false;
}

void PacingSender::SetBurstTokens(uint32_t burst_tokens) {
  initial_burst_size_ = burst_tokens;
  burst_tokens_ = std::min(
      initial_burst_size_,
      static_cast<uint32_t>(sender_->GetCongestionWindow() / kDefaultTCPMSS));
}

QuicTime::Delta PacingSender::TimeUntilSend(
    QuicTime now, QuicByteCount bytes_in_flight) const {
  QUICHE_DCHECK(sender_ != nullptr);

  if (!sender_->CanSend(bytes_in_flight)) {
    // The underlying sender prevents sending.
    return QuicTime::Delta::Infinite();
  }

  if (remove_non_initial_burst_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_pacing_remove_non_initial_burst, 2, 2);
    if (burst_tokens_ > 0 || lumpy_tokens_ > 0) {
      // Don't pace if we have burst or lumpy tokens available.
      QUIC_DVLOG(1) << "Can send packet now. burst_tokens:" << burst_tokens_
                    << ", lumpy_tokens:" << lumpy_tokens_;
      return QuicTime::Delta::Zero();
    }
  } else {
    if (burst_tokens_ > 0 || bytes_in_flight == 0 || lumpy_tokens_ > 0) {
      // Don't pace if we have burst tokens available or leaving quiescence.
      QUIC_DVLOG(1) << "Sending packet now. burst_tokens:" << burst_tokens_
                    << ", bytes_in_flight:" << bytes_in_flight
                    << ", lumpy_tokens:" << lumpy_tokens_;
      return QuicTime::Delta::Zero();
    }
  }

  // If the next send time is within the alarm granularity, send immediately.
  if (ideal_next_packet_send_time_ > now + kAlarmGranularity) {
    QUIC_DVLOG(1) << "Delaying packet: "
                  << (ideal_next_packet_send_time_ - now).ToMicroseconds();
    return ideal_next_packet_send_time_ - now;
  }

  QUIC_DVLOG(1) << "Can send packet now. ideal_next_packet_send_time: "
                << ideal_next_packet_send_time_ << ", now: " << now;
  return QuicTime::Delta::Zero();
}

QuicBandwidth PacingSender::PacingRate(QuicByteCount bytes_in_flight) const {
  QUICHE_DCHECK(sender_ != nullptr);
  if (!max_pacing_rate_.IsZero()) {
    return QuicBandwidth::FromBitsPerSecond(
        std::min(max_pacing_rate_.ToBitsPerSecond(),
                 sender_->PacingRate(bytes_in_flight).ToBitsPerSecond()));
  }
  return sender_->PacingRate(bytes_in_flight);
}

}  // namespace quic
