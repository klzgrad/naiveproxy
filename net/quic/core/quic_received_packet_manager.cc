// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_received_packet_manager.h"

#include <limits>
#include <utility>

#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

namespace net {

namespace {

// The maximum number of packets to ack immediately after a missing packet for
// fast retransmission to kick in at the sender.  This limit is created to
// reduce the number of acks sent that have no benefit for fast retransmission.
// Set to the number of nacks needed for fast retransmit plus one for protection
// against an ack loss
const size_t kMaxPacketsAfterNewMissing = 4;
}

QuicReceivedPacketManager::QuicReceivedPacketManager(QuicConnectionStats* stats)
    : peer_least_packet_awaiting_ack_(0),
      ack_frame_updated_(false),
      max_ack_ranges_(0),
      time_largest_observed_(QuicTime::Zero()),
      stats_(stats) {
  ack_frame_.largest_observed = 0;
}

QuicReceivedPacketManager::~QuicReceivedPacketManager() {}

void QuicReceivedPacketManager::RecordPacketReceived(
    const QuicPacketHeader& header,
    QuicTime receipt_time) {
  QuicPacketNumber packet_number = header.packet_number;
  DCHECK(IsAwaitingPacket(packet_number)) << " packet_number:" << packet_number;
  if (!ack_frame_updated_) {
    ack_frame_.received_packet_times.clear();
  }
  ack_frame_updated_ = true;
  ack_frame_.packets.Add(header.packet_number);

  if (ack_frame_.largest_observed > packet_number) {
    // Record how out of order stats.
    ++stats_->packets_reordered;
    stats_->max_sequence_reordering =
        std::max(stats_->max_sequence_reordering,
                 ack_frame_.largest_observed - packet_number);
    int64_t reordering_time_us =
        (receipt_time - time_largest_observed_).ToMicroseconds();
    stats_->max_time_reordering_us =
        std::max(stats_->max_time_reordering_us, reordering_time_us);
  }
  if (packet_number > ack_frame_.largest_observed) {
    ack_frame_.largest_observed = packet_number;
    time_largest_observed_ = receipt_time;
  }

  ack_frame_.received_packet_times.push_back(
      std::make_pair(packet_number, receipt_time));
}

bool QuicReceivedPacketManager::IsMissing(QuicPacketNumber packet_number) {
  return packet_number < ack_frame_.largest_observed &&
         !ack_frame_.packets.Contains(packet_number);
}

bool QuicReceivedPacketManager::IsAwaitingPacket(
    QuicPacketNumber packet_number) {
  return net::IsAwaitingPacket(ack_frame_, packet_number,
                               peer_least_packet_awaiting_ack_);
}

const QuicFrame QuicReceivedPacketManager::GetUpdatedAckFrame(
    QuicTime approximate_now) {
  ack_frame_updated_ = false;
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
  for (PacketTimeVector::iterator it = ack_frame_.received_packet_times.begin();
       it != ack_frame_.received_packet_times.end();) {
    if (ack_frame_.largest_observed - it->first >=
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
  // ValidateAck() should fail if peer_least_packet_awaiting_ack shrinks.
  DCHECK_LE(peer_least_packet_awaiting_ack_, least_unacked);
  if (least_unacked > peer_least_packet_awaiting_ack_) {
    peer_least_packet_awaiting_ack_ = least_unacked;
    bool packets_updated = ack_frame_.packets.RemoveUpTo(least_unacked);
    if (packets_updated) {
      // Ack frame gets updated because packets set is updated because of stop
      // waiting frame.
      ack_frame_updated_ = true;
    }
  }
  DCHECK(ack_frame_.packets.Empty() ||
         ack_frame_.packets.Min() >= peer_least_packet_awaiting_ack_);
}

bool QuicReceivedPacketManager::HasMissingPackets() const {
  return ack_frame_.packets.NumIntervals() > 1 ||
         (!ack_frame_.packets.Empty() &&
          ack_frame_.packets.Min() >
              std::max(QuicPacketNumber(1), peer_least_packet_awaiting_ack_));
}

bool QuicReceivedPacketManager::HasNewMissingPackets() const {
  return HasMissingPackets() &&
         ack_frame_.packets.LastIntervalLength() <= kMaxPacketsAfterNewMissing;
}

bool QuicReceivedPacketManager::ack_frame_updated() const {
  return ack_frame_updated_;
}

QuicPacketNumber QuicReceivedPacketManager::GetLargestObserved() const {
  return ack_frame_.largest_observed;
}

}  // namespace net
