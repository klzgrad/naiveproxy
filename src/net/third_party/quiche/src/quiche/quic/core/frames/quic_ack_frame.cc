// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_ack_frame.h"

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"

namespace quic {

namespace {

const QuicPacketCount kMaxPrintRange = 128;

}  // namespace

bool IsAwaitingPacket(const QuicAckFrame& ack_frame,
                      QuicPacketNumber packet_number,
                      QuicPacketNumber peer_least_packet_awaiting_ack) {
  QUICHE_DCHECK(packet_number.IsInitialized());
  return (!peer_least_packet_awaiting_ack.IsInitialized() ||
          packet_number >= peer_least_packet_awaiting_ack) &&
         !ack_frame.packets.Contains(packet_number);
}

QuicAckFrame::QuicAckFrame() = default;

QuicAckFrame::QuicAckFrame(const QuicAckFrame& other) = default;

QuicAckFrame::~QuicAckFrame() {}

std::ostream& operator<<(std::ostream& os, const QuicAckFrame& ack_frame) {
  os << "{ largest_acked: " << LargestAcked(ack_frame)
     << ", ack_delay_time: " << ack_frame.ack_delay_time.ToMicroseconds()
     << ", packets: [ " << ack_frame.packets << " ]"
     << ", received_packets: [ ";
  for (const std::pair<QuicPacketNumber, QuicTime>& p :
       ack_frame.received_packet_times) {
    os << p.first << " at " << p.second.ToDebuggingValue() << " ";
  }
  os << " ]";
  os << ", ecn_counters_populated: " << ack_frame.ecn_counters.has_value();
  if (ack_frame.ecn_counters.has_value()) {
    os << ", ect_0_count: " << ack_frame.ecn_counters->ect0
       << ", ect_1_count: " << ack_frame.ecn_counters->ect1
       << ", ecn_ce_count: " << ack_frame.ecn_counters->ce;
  }

  os << " }\n";
  return os;
}

void QuicAckFrame::Clear() {
  largest_acked.Clear();
  ack_delay_time = QuicTime::Delta::Infinite();
  received_packet_times.clear();
  packets.Clear();
}

PacketNumberQueue::PacketNumberQueue() {}
PacketNumberQueue::PacketNumberQueue(const PacketNumberQueue& other) = default;
PacketNumberQueue::PacketNumberQueue(PacketNumberQueue&& other) = default;
PacketNumberQueue::~PacketNumberQueue() {}

PacketNumberQueue& PacketNumberQueue::operator=(
    const PacketNumberQueue& other) = default;
PacketNumberQueue& PacketNumberQueue::operator=(PacketNumberQueue&& other) =
    default;

void PacketNumberQueue::Add(QuicPacketNumber packet_number) {
  if (!packet_number.IsInitialized()) {
    return;
  }
  packet_number_intervals_.AddOptimizedForAppend(packet_number,
                                                 packet_number + 1);
}

void PacketNumberQueue::AddRange(QuicPacketNumber lower,
                                 QuicPacketNumber higher) {
  if (!lower.IsInitialized() || !higher.IsInitialized() || lower >= higher) {
    return;
  }

  packet_number_intervals_.AddOptimizedForAppend(lower, higher);
}

bool PacketNumberQueue::RemoveUpTo(QuicPacketNumber higher) {
  if (!higher.IsInitialized() || Empty()) {
    return false;
  }
  return packet_number_intervals_.TrimLessThan(higher);
}

void PacketNumberQueue::RemoveSmallestInterval() {
  // TODO(wub): Move this QUIC_BUG to upper level.
  QUIC_BUG_IF(quic_bug_12614_1, packet_number_intervals_.Size() < 2)
      << (Empty() ? "No intervals to remove."
                  : "Can't remove the last interval.");
  packet_number_intervals_.PopFront();
}

void PacketNumberQueue::Clear() { packet_number_intervals_.Clear(); }

bool PacketNumberQueue::Contains(QuicPacketNumber packet_number) const {
  if (!packet_number.IsInitialized()) {
    return false;
  }
  return packet_number_intervals_.Contains(packet_number);
}

bool PacketNumberQueue::Empty() const {
  return packet_number_intervals_.Empty();
}

QuicPacketNumber PacketNumberQueue::Min() const {
  QUICHE_DCHECK(!Empty());
  return packet_number_intervals_.begin()->min();
}

QuicPacketNumber PacketNumberQueue::Max() const {
  QUICHE_DCHECK(!Empty());
  return packet_number_intervals_.rbegin()->max() - 1;
}

QuicPacketCount PacketNumberQueue::NumPacketsSlow() const {
  QuicPacketCount n_packets = 0;
  for (const auto& interval : packet_number_intervals_) {
    n_packets += interval.Length();
  }
  return n_packets;
}

size_t PacketNumberQueue::NumIntervals() const {
  return packet_number_intervals_.Size();
}

PacketNumberQueue::const_iterator PacketNumberQueue::begin() const {
  return packet_number_intervals_.begin();
}

PacketNumberQueue::const_iterator PacketNumberQueue::end() const {
  return packet_number_intervals_.end();
}

PacketNumberQueue::const_reverse_iterator PacketNumberQueue::rbegin() const {
  return packet_number_intervals_.rbegin();
}

PacketNumberQueue::const_reverse_iterator PacketNumberQueue::rend() const {
  return packet_number_intervals_.rend();
}

QuicPacketCount PacketNumberQueue::LastIntervalLength() const {
  QUICHE_DCHECK(!Empty());
  return packet_number_intervals_.rbegin()->Length();
}

// Largest min...max range for packet numbers where we print the numbers
// explicitly. If bigger than this, we print as a range  [a,d] rather
// than [a b c d]

std::ostream& operator<<(std::ostream& os, const PacketNumberQueue& q) {
  for (const QuicInterval<QuicPacketNumber>& interval : q) {
    // Print as a range if there is a pathological condition.
    if ((interval.min() >= interval.max()) ||
        (interval.max() - interval.min() > kMaxPrintRange)) {
      // If min>max, it's really a bug, so QUIC_BUG it to
      // catch it in development.
      QUIC_BUG_IF(quic_bug_12614_2, interval.min() >= interval.max())
          << "Ack Range minimum (" << interval.min() << "Not less than max ("
          << interval.max() << ")";
      // print range as min...max rather than full list.
      // in the event of a bug, the list could be very big.
      os << interval.min() << "..." << (interval.max() - 1) << " ";
    } else {
      for (QuicPacketNumber packet_number = interval.min();
           packet_number < interval.max(); ++packet_number) {
        os << packet_number << " ";
      }
    }
  }
  return os;
}

}  // namespace quic
