// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_

#include <ostream>

#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

// A sequence of packet numbers where each number is unique. Intended to be used
// in a sliding window fashion, where smaller old packet numbers are removed and
// larger new packet numbers are added, with the occasional random access.
class QUICHE_EXPORT PacketNumberQueue {
 public:
  PacketNumberQueue();
  PacketNumberQueue(const PacketNumberQueue& other);
  PacketNumberQueue(PacketNumberQueue&& other);
  ~PacketNumberQueue();

  PacketNumberQueue& operator=(const PacketNumberQueue& other);
  PacketNumberQueue& operator=(PacketNumberQueue&& other);

  using const_iterator = QuicIntervalSet<QuicPacketNumber>::const_iterator;
  using const_reverse_iterator =
      QuicIntervalSet<QuicPacketNumber>::const_reverse_iterator;

  // Adds |packet_number| to the set of packets in the queue.
  void Add(QuicPacketNumber packet_number);

  // Adds packets between [lower, higher) to the set of packets in the queue.
  // No-op if |higher| < |lower|.
  // NOTE(wub): Only used in tests as of Nov 2019.
  void AddRange(QuicPacketNumber lower, QuicPacketNumber higher);

  // Removes packets with values less than |higher| from the set of packets in
  // the queue. Returns true if packets were removed.
  bool RemoveUpTo(QuicPacketNumber higher);

  // Removes the smallest interval in the queue.
  void RemoveSmallestInterval();

  // Clear this packet number queue.
  void Clear();

  // Returns true if the queue contains |packet_number|.
  bool Contains(QuicPacketNumber packet_number) const;

  // Returns true if the queue is empty.
  bool Empty() const;

  // Returns the minimum packet number stored in the queue. It is undefined
  // behavior to call this if the queue is empty.
  QuicPacketNumber Min() const;

  // Returns the maximum packet number stored in the queue. It is undefined
  // behavior to call this if the queue is empty.
  QuicPacketNumber Max() const;

  // Returns the number of unique packets stored in the queue. Inefficient; only
  // exposed for testing.
  QuicPacketCount NumPacketsSlow() const;

  // Returns the number of disjoint packet number intervals contained in the
  // queue.
  size_t NumIntervals() const;

  // Returns the length of last interval.
  QuicPacketCount LastIntervalLength() const;

  // Returns iterators over the packet number intervals.
  const_iterator begin() const;
  const_iterator end() const;
  const_reverse_iterator rbegin() const;
  const_reverse_iterator rend() const;

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const PacketNumberQueue& q);

 private:
  QuicIntervalSet<QuicPacketNumber> packet_number_intervals_;
};

struct QUICHE_EXPORT QuicAckFrame {
  QuicAckFrame();
  QuicAckFrame(const QuicAckFrame& other);
  ~QuicAckFrame();

  void Clear();

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const QuicAckFrame& ack_frame);

  // The highest packet number we've observed from the peer. When |packets| is
  // not empty, it should always be equal to packets.Max(). The |LargestAcked|
  // function ensures this invariant in debug mode.
  QuicPacketNumber largest_acked;

  // Time elapsed since largest_observed() was received until this Ack frame was
  // sent.
  QuicTime::Delta ack_delay_time = QuicTime::Delta::Infinite();

  // Vector of <packet_number, time> for when packets arrived.
  // For IETF versions, packet numbers and timestamps in this vector are both in
  // ascending orders. Packets received out of order are not saved here.
  PacketTimeVector received_packet_times;

  // Set of packets.
  PacketNumberQueue packets;

  // ECN counters.
  std::optional<QuicEcnCounts> ecn_counters;
};

// The highest acked packet number we've observed from the peer. If no packets
// have been observed, return 0.
inline QUICHE_EXPORT QuicPacketNumber LargestAcked(const QuicAckFrame& frame) {
  QUICHE_DCHECK(frame.packets.Empty() ||
                frame.packets.Max() == frame.largest_acked);
  return frame.largest_acked;
}

// True if the packet number is greater than largest_observed or is listed
// as missing.
// Always returns false for packet numbers less than least_unacked.
QUICHE_EXPORT bool IsAwaitingPacket(
    const QuicAckFrame& ack_frame, QuicPacketNumber packet_number,
    QuicPacketNumber peer_least_packet_awaiting_ack);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_
