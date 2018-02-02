// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_ack_frame.h"

#include <algorithm>

#include "net/quic/core/quic_constants.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"

using std::max;
using std::min;

namespace net {

namespace {
const QuicPacketNumber kMaxPrintRange = 128;
}  // namespace

PacketNumberQueue::const_iterator::const_iterator(const const_iterator& other) =
    default;
PacketNumberQueue::const_iterator::const_iterator(const_iterator&& other) =
    default;
PacketNumberQueue::const_iterator::~const_iterator() {}

PacketNumberQueue::const_iterator::const_iterator(
    typename QuicIntervalSet<QuicPacketNumber>::const_iterator it)
    : vector_it_(it), use_deque_it_(false) {}

PacketNumberQueue::const_reverse_iterator::const_reverse_iterator(
    const const_reverse_iterator& other) = default;
PacketNumberQueue::const_reverse_iterator::const_reverse_iterator(
    const_reverse_iterator&& other) = default;
PacketNumberQueue::const_reverse_iterator::~const_reverse_iterator() {}

PacketNumberQueue::const_iterator::const_iterator(
    typename QuicDeque<Interval<QuicPacketNumber>>::const_iterator it)
    : deque_it_(it), use_deque_it_(true) {}

PacketNumberQueue::const_reverse_iterator::const_reverse_iterator(
    const typename QuicIntervalSet<QuicPacketNumber>::const_reverse_iterator&
        it)
    : vector_it_(it), use_deque_it_(false) {}

PacketNumberQueue::const_reverse_iterator::const_reverse_iterator(
    const typename QuicDeque<
        Interval<QuicPacketNumber>>::const_reverse_iterator& it)
    : deque_it_(it), use_deque_it_(true) {}

bool IsAwaitingPacket(const QuicAckFrame& ack_frame,
                      QuicPacketNumber packet_number,
                      QuicPacketNumber peer_least_packet_awaiting_ack) {
  return packet_number >= peer_least_packet_awaiting_ack &&
         !ack_frame.packets.Contains(packet_number);
}

QuicAckFrame::QuicAckFrame()
    : deprecated_largest_observed(0),
      ack_delay_time(QuicTime::Delta::Infinite()) {}

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
  os << " ] }\n";
  return os;
}

QuicPacketNumber LargestAcked(const QuicAckFrame& frame) {
  if (!FLAGS_quic_reloadable_flag_quic_deprecate_largest_observed) {
    return frame.deprecated_largest_observed;
  }

  if (!frame.packets.Empty() &&
      frame.packets.Max() != frame.deprecated_largest_observed) {
    QUIC_BUG << "Peer last received packet: " << frame.packets.Max()
             << " which is not equal to largest observed: "
             << frame.deprecated_largest_observed;
  }

  return frame.packets.Empty() ? 0 : frame.packets.Max();
}

PacketNumberQueue::PacketNumberQueue()
    : use_deque_(FLAGS_quic_reloadable_flag_quic_frames_deque3) {
  if (use_deque_) {
    QUIC_FLAG_COUNT(quic_reloadable_flag_quic_frames_deque3);
  }
}

PacketNumberQueue::PacketNumberQueue(const PacketNumberQueue& other) = default;
PacketNumberQueue::PacketNumberQueue(PacketNumberQueue&& other) = default;
PacketNumberQueue::~PacketNumberQueue() {}

PacketNumberQueue& PacketNumberQueue::operator=(
    const PacketNumberQueue& other) = default;
PacketNumberQueue& PacketNumberQueue::operator=(PacketNumberQueue&& other) =
    default;

void PacketNumberQueue::Add(QuicPacketNumber packet_number) {
  if (use_deque_) {
    // Check if the deque is empty
    if (packet_number_deque_.empty()) {
      packet_number_deque_.push_front(
          Interval<QuicPacketNumber>(packet_number, packet_number + 1));
      return;
    }
    Interval<QuicPacketNumber> back = packet_number_deque_.back();

    // Check for the typical case,
    // when the next packet in order is acked
    if (back.max() == packet_number) {
      packet_number_deque_.back().SetMax(packet_number + 1);
      return;
    }
    // Check if the next packet in order is skipped
    if (back.max() < packet_number) {
      packet_number_deque_.push_back(
          Interval<QuicPacketNumber>(packet_number, packet_number + 1));
      return;
    }

    Interval<QuicPacketNumber> front = packet_number_deque_.front();
    // Check if the packet can be  popped on the front
    if (front.min() > packet_number + 1) {
      packet_number_deque_.push_front(
          Interval<QuicPacketNumber>(packet_number, packet_number + 1));
      return;
    }
    if (front.min() == packet_number + 1) {
      packet_number_deque_.front().SetMin(packet_number);
      return;
    }

    int i = packet_number_deque_.size() - 1;
    // Iterating through the queue backwards
    // to find a proper place for the packet
    while (i >= 0) {
      Interval<QuicPacketNumber> packet_interval = packet_number_deque_[i];
      DCHECK(packet_interval.min() < packet_interval.max());
      // Check if the packet is contained in an interval already
      if (packet_interval.Contains(packet_number)) {
        return;
      }

      // Check if the packet can extend an interval.
      if (packet_interval.max() == packet_number) {
        packet_number_deque_[i].SetMax(packet_number + 1);
        return;
      }
      // Check if the packet can extend an interval
      // and merge two intervals if needed.
      // There is no need to merge an interval in the previous
      // if statement, as all merges will happen here.
      if (packet_interval.min() == packet_number + 1) {
        packet_number_deque_[i].SetMin(packet_number);
        if (i > 0 && packet_number == packet_number_deque_[i - 1].max()) {
          packet_number_deque_[i - 1].SetMax(packet_interval.max());
          packet_number_deque_.erase(packet_number_deque_.begin() + i);
        }
        return;
      }

      // Check if we need to make a new interval for the packet
      if (packet_interval.max() < packet_number + 1) {
        packet_number_deque_.insert(
            packet_number_deque_.begin() + i + 1,
            Interval<QuicPacketNumber>(packet_number, packet_number + 1));
        return;
      }
      i--;
    }
  } else {
    packet_number_intervals_.Add(packet_number, packet_number + 1);
  }
}

void PacketNumberQueue::AddRange(QuicPacketNumber lower,
                                 QuicPacketNumber higher) {
  if (lower >= higher) {
    return;
  }
  if (use_deque_) {
    if (packet_number_deque_.empty()) {
      packet_number_deque_.push_front(
          Interval<QuicPacketNumber>(lower, higher));
      return;
    }
    Interval<QuicPacketNumber> back = packet_number_deque_.back();

    if (back.max() == lower) {
      // Check for the typical case,
      // when the next packet in order is acked
      packet_number_deque_.back().SetMax(higher);
      return;
    }
    if (back.max() < lower) {
      // Check if the next packet in order is skipped
      packet_number_deque_.push_back(Interval<QuicPacketNumber>(lower, higher));
      return;
    }
    Interval<QuicPacketNumber> front = packet_number_deque_.front();
    // Check if the packets are being added in reverse order
    if (front.min() == higher) {
      packet_number_deque_.front().SetMin(lower);
    } else if (front.min() > higher) {
      packet_number_deque_.push_front(
          Interval<QuicPacketNumber>(lower, higher));

    } else {
      // Ranges must be above or below all existing ranges.
      QUIC_BUG << "AddRange only supports adding packets above or below the "
               << "current min:" << Min() << " and max:" << Max();
    }
  } else {
    packet_number_intervals_.Add(lower, higher);
  }
}

bool PacketNumberQueue::RemoveUpTo(QuicPacketNumber higher) {
  if (Empty()) {
    return false;
  }
  const QuicPacketNumber old_min = Min();
  if (use_deque_) {
    while (!packet_number_deque_.empty()) {
      Interval<QuicPacketNumber> front = packet_number_deque_.front();
      if (front.max() < higher) {
        packet_number_deque_.pop_front();
      } else if (front.min() < higher && front.max() >= higher) {
        packet_number_deque_.front().SetMin(higher);
        if (front.max() == higher) {
          packet_number_deque_.pop_front();
        }
        break;
      } else {
        break;
      }
    }
  } else {
    packet_number_intervals_.Difference(0, higher);
  }

  return Empty() || old_min != Min();
}

void PacketNumberQueue::RemoveSmallestInterval() {
  if (use_deque_) {
    QUIC_BUG_IF(packet_number_deque_.size() < 2)
        << (Empty() ? "No intervals to remove."
                    : "Can't remove the last interval.");
    packet_number_deque_.pop_front();
  } else {
    QUIC_BUG_IF(packet_number_intervals_.Size() < 2)
        << (Empty() ? "No intervals to remove."
                    : "Can't remove the last interval.");
    packet_number_intervals_.Difference(*packet_number_intervals_.begin());
  }
}

bool PacketNumberQueue::Contains(QuicPacketNumber packet_number) const {
  if (use_deque_) {
    if (packet_number_deque_.empty()) {
      return false;
    }
    if (packet_number_deque_.front().min() > packet_number ||
        packet_number_deque_.back().max() <= packet_number) {
      return false;
    }
    for (Interval<QuicPacketNumber> interval : packet_number_deque_) {
      if (interval.Contains(packet_number)) {
        return true;
      }
    }
    return false;
  } else {
    return packet_number_intervals_.Contains(packet_number);
  }
}

bool PacketNumberQueue::Empty() const {
  if (use_deque_) {
    return packet_number_deque_.empty();
  } else {
    return packet_number_intervals_.Empty();
  }
}

QuicPacketNumber PacketNumberQueue::Min() const {
  DCHECK(!Empty());
  if (use_deque_) {
    return packet_number_deque_.front().min();
  } else {
    return packet_number_intervals_.begin()->min();
  }
}

QuicPacketNumber PacketNumberQueue::Max() const {
  DCHECK(!Empty());
  if (use_deque_) {
    return packet_number_deque_.back().max() - 1;
  } else {
    return packet_number_intervals_.rbegin()->max() - 1;
  }
}

size_t PacketNumberQueue::NumPacketsSlow() const {
  if (use_deque_) {
    int n_packets = 0;
    for (Interval<QuicPacketNumber> interval : packet_number_deque_) {
      n_packets += interval.Length();
    }
    return n_packets;
  } else {
    size_t num_packets = 0;
    for (const auto& interval : packet_number_intervals_) {
      num_packets += interval.Length();
    }
    return num_packets;
  }
}

size_t PacketNumberQueue::NumIntervals() const {
  if (use_deque_) {
    return packet_number_deque_.size();
  } else {
    return packet_number_intervals_.Size();
  }
}

PacketNumberQueue::const_iterator PacketNumberQueue::begin() const {
  if (use_deque_) {
    return PacketNumberQueue::const_iterator(packet_number_deque_.begin());
  } else {
    return PacketNumberQueue::const_iterator(packet_number_intervals_.begin());
  }
}

PacketNumberQueue::const_iterator PacketNumberQueue::end() const {
  if (use_deque_) {
    return const_iterator(packet_number_deque_.end());
  } else {
    return const_iterator(packet_number_intervals_.end());
  }
}

PacketNumberQueue::const_reverse_iterator PacketNumberQueue::rbegin() const {
  if (use_deque_) {
    return const_reverse_iterator(packet_number_deque_.rbegin());
  } else {
    return const_reverse_iterator(packet_number_intervals_.rbegin());
  }
}

PacketNumberQueue::const_reverse_iterator PacketNumberQueue::rend() const {
  if (use_deque_) {
    return const_reverse_iterator(packet_number_deque_.rend());
  } else {
    return const_reverse_iterator(packet_number_intervals_.rend());
  }
}

QuicPacketNumber PacketNumberQueue::LastIntervalLength() const {
  DCHECK(!Empty());
  if (use_deque_) {
    return packet_number_deque_.back().Length();
  } else {
    return packet_number_intervals_.rbegin()->Length();
  }
}

// Largest min...max range for packet numbers where we print the numbers
// explicitly. If bigger than this, we print as a range  [a,d] rather
// than [a b c d]

std::ostream& operator<<(std::ostream& os, const PacketNumberQueue& q) {
  for (const Interval<QuicPacketNumber>& interval : q) {
    // Print as a range if there is a pathological condition.
    if ((interval.min() >= interval.max()) ||
        (interval.max() - interval.min() > kMaxPrintRange)) {
      // If min>max, it's really a bug, so QUIC_BUG it to
      // catch it in development.
      QUIC_BUG_IF(interval.min() >= interval.max())
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

}  // namespace net
