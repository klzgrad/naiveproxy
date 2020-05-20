// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_received_packet_manager.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

namespace {

// The maximum number of packets to ack immediately after a missing packet for
// fast retransmission to kick in at the sender.  This limit is created to
// reduce the number of acks sent that have no benefit for fast retransmission.
// Set to the number of nacks needed for fast retransmit plus one for protection
// against an ack loss
const size_t kMaxPacketsAfterNewMissing = 4;

// One quarter RTT delay when doing ack decimation.
const float kAckDecimationDelay = 0.25;
// One eighth RTT delay when doing ack decimation.
const float kShortAckDecimationDelay = 0.125;
}  // namespace

QuicReceivedPacketManager::QuicReceivedPacketManager()
    : QuicReceivedPacketManager(nullptr) {}

QuicReceivedPacketManager::QuicReceivedPacketManager(QuicConnectionStats* stats)
    : ack_frame_updated_(false),
      max_ack_ranges_(0),
      time_largest_observed_(QuicTime::Zero()),
      save_timestamps_(false),
      stats_(stats),
      ack_mode_(GetQuicReloadableFlag(quic_enable_ack_decimation)
                    ? ACK_DECIMATION
                    : TCP_ACKING),
      num_retransmittable_packets_received_since_last_ack_sent_(0),
      min_received_before_ack_decimation_(kMinReceivedBeforeAckDecimation),
      ack_frequency_before_ack_decimation_(
          kDefaultRetransmittablePacketsBeforeAck),
      ack_decimation_delay_(kAckDecimationDelay),
      unlimited_ack_decimation_(false),
      fast_ack_after_quiescence_(false),
      one_immediate_ack_(false),
      local_max_ack_delay_(
          QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs)),
      ack_timeout_(QuicTime::Zero()),
      time_of_previous_received_packet_(QuicTime::Zero()),
      was_last_packet_missing_(false) {
  if (ack_mode_ == ACK_DECIMATION) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_enable_ack_decimation);
  }
}

QuicReceivedPacketManager::~QuicReceivedPacketManager() {}

void QuicReceivedPacketManager::SetFromConfig(const QuicConfig& config,
                                              Perspective perspective) {
  if (GetQuicReloadableFlag(quic_enable_ack_decimation) &&
      config.HasClientSentConnectionOption(kACD0, perspective)) {
    ack_mode_ = TCP_ACKING;
  }
  if (config.HasClientSentConnectionOption(kACKD, perspective)) {
    ack_mode_ = ACK_DECIMATION;
  }
  if (config.HasClientSentConnectionOption(kAKD2, perspective)) {
    ack_mode_ = ACK_DECIMATION_WITH_REORDERING;
  }
  if (config.HasClientSentConnectionOption(kAKD3, perspective)) {
    ack_mode_ = ACK_DECIMATION;
    ack_decimation_delay_ = kShortAckDecimationDelay;
  }
  if (config.HasClientSentConnectionOption(kAKD4, perspective)) {
    ack_mode_ = ACK_DECIMATION_WITH_REORDERING;
    ack_decimation_delay_ = kShortAckDecimationDelay;
  }
  if (config.HasClientSentConnectionOption(kAKDU, perspective)) {
    unlimited_ack_decimation_ = true;
  }
  if (config.HasClientSentConnectionOption(kACKQ, perspective)) {
    fast_ack_after_quiescence_ = true;
  }
  if (config.HasClientSentConnectionOption(k1ACK, perspective)) {
    one_immediate_ack_ = true;
  }
}

void QuicReceivedPacketManager::RecordPacketReceived(
    const QuicPacketHeader& header,
    QuicTime receipt_time) {
  const QuicPacketNumber packet_number = header.packet_number;
  DCHECK(IsAwaitingPacket(packet_number)) << " packet_number:" << packet_number;
  was_last_packet_missing_ = IsMissing(packet_number);
  if (!ack_frame_updated_) {
    ack_frame_.received_packet_times.clear();
  }
  ack_frame_updated_ = true;

  if (LargestAcked(ack_frame_).IsInitialized() &&
      LargestAcked(ack_frame_) > packet_number) {
    // Record how out of order stats.
    ++stats_->packets_reordered;
    stats_->max_sequence_reordering =
        std::max(stats_->max_sequence_reordering,
                 LargestAcked(ack_frame_) - packet_number);
    int64_t reordering_time_us =
        (receipt_time - time_largest_observed_).ToMicroseconds();
    stats_->max_time_reordering_us =
        std::max(stats_->max_time_reordering_us, reordering_time_us);
  }
  if (!LargestAcked(ack_frame_).IsInitialized() ||
      packet_number > LargestAcked(ack_frame_)) {
    ack_frame_.largest_acked = packet_number;
    time_largest_observed_ = receipt_time;
  }
  ack_frame_.packets.Add(packet_number);

  if (save_timestamps_) {
    // The timestamp format only handles packets in time order.
    if (!ack_frame_.received_packet_times.empty() &&
        ack_frame_.received_packet_times.back().second > receipt_time) {
      QUIC_LOG(WARNING)
          << "Receive time went backwards from: "
          << ack_frame_.received_packet_times.back().second.ToDebuggingValue()
          << " to " << receipt_time.ToDebuggingValue();
    } else {
      ack_frame_.received_packet_times.push_back(
          std::make_pair(packet_number, receipt_time));
    }
  }

  if (least_received_packet_number_.IsInitialized()) {
    least_received_packet_number_ =
        std::min(least_received_packet_number_, packet_number);
  } else {
    least_received_packet_number_ = packet_number;
  }
}

bool QuicReceivedPacketManager::IsMissing(QuicPacketNumber packet_number) {
  return LargestAcked(ack_frame_).IsInitialized() &&
         packet_number < LargestAcked(ack_frame_) &&
         !ack_frame_.packets.Contains(packet_number);
}

bool QuicReceivedPacketManager::IsAwaitingPacket(
    QuicPacketNumber packet_number) const {
  return quic::IsAwaitingPacket(ack_frame_, packet_number,
                                peer_least_packet_awaiting_ack_);
}

const QuicFrame QuicReceivedPacketManager::GetUpdatedAckFrame(
    QuicTime approximate_now) {
  if (time_largest_observed_ == QuicTime::Zero()) {
    // We have received no packets.
    ack_frame_.ack_delay_time = QuicTime::Delta::Infinite();
  } else {
    // Ensure the delta is zero if approximate now is "in the past".
    ack_frame_.ack_delay_time = approximate_now < time_largest_observed_
                                    ? QuicTime::Delta::Zero()
                                    : approximate_now - time_largest_observed_;
  }
  while (max_ack_ranges_ > 0 &&
         ack_frame_.packets.NumIntervals() > max_ack_ranges_) {
    ack_frame_.packets.RemoveSmallestInterval();
  }
  // Clear all packet times if any are too far from largest observed.
  // It's expected this is extremely rare.
  for (auto it = ack_frame_.received_packet_times.begin();
       it != ack_frame_.received_packet_times.end();) {
    if (LargestAcked(ack_frame_) - it->first >=
        std::numeric_limits<uint8_t>::max()) {
      it = ack_frame_.received_packet_times.erase(it);
    } else {
      ++it;
    }
  }

  return QuicFrame(&ack_frame_);
}

void QuicReceivedPacketManager::DontWaitForPacketsBefore(
    QuicPacketNumber least_unacked) {
  if (!least_unacked.IsInitialized()) {
    return;
  }
  // ValidateAck() should fail if peer_least_packet_awaiting_ack shrinks.
  DCHECK(!peer_least_packet_awaiting_ack_.IsInitialized() ||
         peer_least_packet_awaiting_ack_ <= least_unacked);
  if (!peer_least_packet_awaiting_ack_.IsInitialized() ||
      least_unacked > peer_least_packet_awaiting_ack_) {
    peer_least_packet_awaiting_ack_ = least_unacked;
    bool packets_updated = ack_frame_.packets.RemoveUpTo(least_unacked);
    if (packets_updated) {
      // Ack frame gets updated because packets set is updated because of stop
      // waiting frame.
      ack_frame_updated_ = true;
    }
  }
  DCHECK(ack_frame_.packets.Empty() ||
         !peer_least_packet_awaiting_ack_.IsInitialized() ||
         ack_frame_.packets.Min() >= peer_least_packet_awaiting_ack_);
}

void QuicReceivedPacketManager::MaybeUpdateAckTimeout(
    bool should_last_packet_instigate_acks,
    QuicPacketNumber last_received_packet_number,
    QuicTime time_of_last_received_packet,
    QuicTime now,
    const RttStats* rtt_stats) {
  if (!ack_frame_updated_) {
    // ACK frame has not been updated, nothing to do.
    return;
  }

  if (was_last_packet_missing_ && last_sent_largest_acked_.IsInitialized() &&
      last_received_packet_number < last_sent_largest_acked_) {
    // Only ack immediately if an ACK frame was sent with a larger largest acked
    // than the newly received packet number.
    ack_timeout_ = now;
    return;
  }

  if (!should_last_packet_instigate_acks) {
    return;
  }

  ++num_retransmittable_packets_received_since_last_ack_sent_;
  if (ack_mode_ != TCP_ACKING &&
      last_received_packet_number >= PeerFirstSendingPacketNumber() +
                                         min_received_before_ack_decimation_) {
    // Ack up to 10 packets at once unless ack decimation is unlimited.
    if (!unlimited_ack_decimation_ &&
        num_retransmittable_packets_received_since_last_ack_sent_ >=
            kMaxRetransmittablePacketsBeforeAck) {
      ack_timeout_ = now;
      return;
    }
    // Wait for the minimum of the ack decimation delay or the delayed ack time
    // before sending an ack.
    QuicTime::Delta ack_delay = std::min(
        local_max_ack_delay_, rtt_stats->min_rtt() * ack_decimation_delay_);
    if (GetQuicReloadableFlag(quic_ack_delay_alarm_granularity)) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_ack_delay_alarm_granularity);
      ack_delay = std::max(ack_delay, kAlarmGranularity);
    }
    if (fast_ack_after_quiescence_ && now - time_of_previous_received_packet_ >
                                          rtt_stats->SmoothedOrInitialRtt()) {
      // Ack the first packet out of queiscence faster, because QUIC does
      // not pace the first few packets and commonly these may be handshake
      // or TLP packets, which we'd like to acknowledge quickly.
      ack_delay = kAlarmGranularity;
    }
    MaybeUpdateAckTimeoutTo(now + ack_delay);
  } else {
    // Ack with a timer or every 2 packets by default.
    if (num_retransmittable_packets_received_since_last_ack_sent_ >=
        ack_frequency_before_ack_decimation_) {
      ack_timeout_ = now;
    } else if (fast_ack_after_quiescence_ &&
               (now - time_of_previous_received_packet_) >
                   rtt_stats->SmoothedOrInitialRtt()) {
      // Ack the first packet out of queiscence faster, because QUIC does
      // not pace the first few packets and commonly these may be handshake
      // or TLP packets, which we'd like to acknowledge quickly.
      MaybeUpdateAckTimeoutTo(now + kAlarmGranularity);
    } else {
      MaybeUpdateAckTimeoutTo(now + local_max_ack_delay_);
    }
  }

  // If there are new missing packets to report, send an ack immediately.
  if (HasNewMissingPackets()) {
    if (ack_mode_ == ACK_DECIMATION_WITH_REORDERING) {
      // Wait the minimum of an eighth min_rtt and the existing ack time.
      QuicTime ack_time = now + kShortAckDecimationDelay * rtt_stats->min_rtt();
      MaybeUpdateAckTimeoutTo(ack_time);
    } else {
      ack_timeout_ = now;
    }
  }

  if (fast_ack_after_quiescence_) {
    time_of_previous_received_packet_ = time_of_last_received_packet;
  }
}

void QuicReceivedPacketManager::ResetAckStates() {
  ack_frame_updated_ = false;
  ack_timeout_ = QuicTime::Zero();
  num_retransmittable_packets_received_since_last_ack_sent_ = 0;
  last_sent_largest_acked_ = LargestAcked(ack_frame_);
}

void QuicReceivedPacketManager::MaybeUpdateAckTimeoutTo(QuicTime time) {
  if (!ack_timeout_.IsInitialized() || ack_timeout_ > time) {
    ack_timeout_ = time;
  }
}

bool QuicReceivedPacketManager::HasMissingPackets() const {
  if (ack_frame_.packets.Empty()) {
    return false;
  }
  if (ack_frame_.packets.NumIntervals() > 1) {
    return true;
  }
  return peer_least_packet_awaiting_ack_.IsInitialized() &&
         ack_frame_.packets.Min() > peer_least_packet_awaiting_ack_;
}

bool QuicReceivedPacketManager::HasNewMissingPackets() const {
  if (one_immediate_ack_) {
    return HasMissingPackets() && ack_frame_.packets.LastIntervalLength() == 1;
  }
  return HasMissingPackets() &&
         ack_frame_.packets.LastIntervalLength() <= kMaxPacketsAfterNewMissing;
}

bool QuicReceivedPacketManager::ack_frame_updated() const {
  return ack_frame_updated_;
}

QuicPacketNumber QuicReceivedPacketManager::GetLargestObserved() const {
  return LargestAcked(ack_frame_);
}

QuicPacketNumber QuicReceivedPacketManager::PeerFirstSendingPacketNumber()
    const {
  if (!least_received_packet_number_.IsInitialized()) {
    QUIC_BUG << "No packets have been received yet";
    return QuicPacketNumber(1);
  }
  return least_received_packet_number_;
}

bool QuicReceivedPacketManager::IsAckFrameEmpty() const {
  return ack_frame_.packets.Empty();
}

}  // namespace quic
