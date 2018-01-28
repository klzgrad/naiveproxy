// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_

#include <deque>
#include <ostream>
#include <string>

#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_flags.h"

namespace net {

// A sequence of packet numbers where each number is unique. Intended to be used
// in a sliding window fashion, where smaller old packet numbers are removed and
// larger new packet numbers are added, with the occasional random access.
class QUIC_EXPORT_PRIVATE PacketNumberQueue {
 public:
  PacketNumberQueue();
  PacketNumberQueue(const PacketNumberQueue& other);
  PacketNumberQueue(PacketNumberQueue&& other);
  ~PacketNumberQueue();

  PacketNumberQueue& operator=(const PacketNumberQueue& other);
  PacketNumberQueue& operator=(PacketNumberQueue&& other);

  class QUIC_EXPORT_PRIVATE const_iterator {
   public:
    const_iterator(const const_iterator& other);
    const_iterator(const_iterator&& other);
    ~const_iterator();

    explicit const_iterator(
        typename QuicIntervalSet<QuicPacketNumber>::const_iterator it);

    explicit const_iterator(
        typename QuicDeque<Interval<QuicPacketNumber>>::const_iterator it);

    typedef std::input_iterator_tag iterator_category;
    typedef Interval<QuicPacketNumber> value_type;
    typedef value_type& reference;
    typedef value_type* pointer;
    typedef typename std::vector<value_type>::iterator::difference_type
        difference_type;

    inline const Interval<QuicPacketNumber>& operator*() {
      if (use_deque_it_) {
        return *deque_it_;
      } else {
        return *vector_it_;
      }
    }

    inline const_iterator& operator++() {
      if (use_deque_it_) {
        deque_it_++;
      } else {
        vector_it_++;
      }
      return *this;
    }

    inline const_iterator& operator--() {
      if (use_deque_it_) {
        deque_it_--;
      } else {
        vector_it_--;
      }
      return *this;
    }

    inline const_iterator& operator++(int) {
      if (use_deque_it_) {
        ++deque_it_;
      } else {
        ++vector_it_;
      }
      return *this;
    }

    inline bool operator==(const const_iterator& other) {
      if (use_deque_it_ != other.use_deque_it_) {
        return false;
      }

      if (use_deque_it_) {
        return deque_it_ == other.deque_it_;
      } else {
        return vector_it_ == other.vector_it_;
      }
    }

    inline bool operator!=(const const_iterator& other) {
      return !(*this == other);
    }

   private:
    typename QuicIntervalSet<QuicPacketNumber>::const_iterator vector_it_;
    typename QuicDeque<Interval<QuicPacketNumber>>::const_iterator deque_it_;
    const bool use_deque_it_;
  };

  class QUIC_EXPORT_PRIVATE const_reverse_iterator {
   public:
    const_reverse_iterator(const const_reverse_iterator& other);
    const_reverse_iterator(const_reverse_iterator&& other);
    ~const_reverse_iterator();

    explicit const_reverse_iterator(
        const typename QuicIntervalSet<
            QuicPacketNumber>::const_reverse_iterator& it);

    explicit const_reverse_iterator(
        const typename QuicDeque<
            Interval<QuicPacketNumber>>::const_reverse_iterator& it);

    typedef std::input_iterator_tag iterator_category;
    typedef Interval<QuicPacketNumber> value_type;
    typedef value_type& reference;
    typedef value_type* pointer;
    typedef typename std::vector<value_type>::iterator::difference_type
        difference_type;

    inline const Interval<QuicPacketNumber>& operator*() {
      if (use_deque_it_) {
        return *deque_it_;
      } else {
        return *vector_it_;
      }
    }

    inline const Interval<QuicPacketNumber>* operator->() {
      if (use_deque_it_) {
        return &*deque_it_;
      } else {
        return &*vector_it_;
      }
    }

    inline const_reverse_iterator& operator++() {
      if (use_deque_it_) {
        deque_it_++;
      } else {
        vector_it_++;
      }
      return *this;
    }

    inline const_reverse_iterator& operator--() {
      if (use_deque_it_) {
        deque_it_--;
      } else {
        vector_it_--;
      }
      return *this;
    }

    inline const_reverse_iterator& operator++(int) {
      if (use_deque_it_) {
        ++deque_it_;
      } else {
        ++vector_it_;
      }
      return *this;
    }

    inline bool operator==(const const_reverse_iterator& other) {
      if (use_deque_it_ != other.use_deque_it_) {
        return false;
      }

      if (use_deque_it_) {
        return deque_it_ == other.deque_it_;
      } else {
        return vector_it_ == other.vector_it_;
      }
    }

    inline bool operator!=(const const_reverse_iterator& other) {
      return !(*this == other);
    }

   private:
    typename QuicIntervalSet<QuicPacketNumber>::const_reverse_iterator
        vector_it_;
    typename QuicDeque<Interval<QuicPacketNumber>>::const_reverse_iterator
        deque_it_;
    const bool use_deque_it_;
  };

  // Adds |packet_number| to the set of packets in the queue.
  void Add(QuicPacketNumber packet_number);

  // Adds packets between [lower, higher) to the set of packets in the queue. It
  // is undefined behavior to call this with |higher| < |lower|.
  void AddRange(QuicPacketNumber lower, QuicPacketNumber higher);

  // Removes packets with values less than |higher| from the set of packets in
  // the queue. Returns true if packets were removed.
  bool RemoveUpTo(QuicPacketNumber higher);

  // Removes the smallest interval in the queue.
  void RemoveSmallestInterval();

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
  size_t NumPacketsSlow() const;

  // Returns the number of disjoint packet number intervals contained in the
  // queue.
  size_t NumIntervals() const;

  // Returns the length of last interval.
  QuicPacketNumber LastIntervalLength() const;

  // Returns iterators over the packet number intervals.
  const_iterator begin() const;
  const_iterator end() const;
  const_reverse_iterator rbegin() const;
  const_reverse_iterator rend() const;

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const PacketNumberQueue& q);

 private:
  // TODO(lilika): Remove QuicIntervalSet<QuicPacketNumber>
  // once FLAGS_quic_reloadable_flag_quic_frames_deque2 is removed
  QuicIntervalSet<QuicPacketNumber> packet_number_intervals_;
  QuicDeque<Interval<QuicPacketNumber>> packet_number_deque_;
  bool use_deque_;
};

struct QUIC_EXPORT_PRIVATE QuicAckFrame {
  QuicAckFrame();
  QuicAckFrame(const QuicAckFrame& other);
  ~QuicAckFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicAckFrame& ack_frame);

  // The highest packet number we've observed from the peer.
  // This is being deprecated.
  QuicPacketNumber deprecated_largest_observed;

  // Time elapsed since largest_observed() was received until this Ack frame was
  // sent.
  QuicTime::Delta ack_delay_time;

  // Vector of <packet_number, time> for when packets arrived.
  PacketTimeVector received_packet_times;

  // Set of packets.
  PacketNumberQueue packets;
};

// The highest acked packet number we've observed from the peer. If no packets
// have been observed, return 0.
QUIC_EXPORT_PRIVATE QuicPacketNumber LargestAcked(const QuicAckFrame& frame);

// True if the packet number is greater than largest_observed or is listed
// as missing.
// Always returns false for packet numbers less than least_unacked.
QUIC_EXPORT_PRIVATE bool IsAwaitingPacket(
    const QuicAckFrame& ack_frame,
    QuicPacketNumber packet_number,
    QuicPacketNumber peer_least_packet_awaiting_ack);

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_ACK_FRAME_H_
