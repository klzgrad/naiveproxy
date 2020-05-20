// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_RECEIVED_PACKET_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_RECEIVED_PACKET_MANAGER_H_

#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class RttStats;

namespace test {
class QuicConnectionPeer;
class QuicReceivedPacketManagerPeer;
class UberReceivedPacketManagerPeer;
}  // namespace test

struct QuicConnectionStats;

// Records all received packets by a connection.
class QUIC_EXPORT_PRIVATE QuicReceivedPacketManager {
 public:
  QuicReceivedPacketManager();
  explicit QuicReceivedPacketManager(QuicConnectionStats* stats);
  QuicReceivedPacketManager(const QuicReceivedPacketManager&) = delete;
  QuicReceivedPacketManager& operator=(const QuicReceivedPacketManager&) =
      delete;
  virtual ~QuicReceivedPacketManager();

  void SetFromConfig(const QuicConfig& config, Perspective perspective);

  // Updates the internal state concerning which packets have been received.
  // header: the packet header.
  // timestamp: the arrival time of the packet.
  virtual void RecordPacketReceived(const QuicPacketHeader& header,
                                    QuicTime receipt_time);

  // Checks whether |packet_number| is missing and less than largest observed.
  virtual bool IsMissing(QuicPacketNumber packet_number);

  // Checks if we're still waiting for the packet with |packet_number|.
  virtual bool IsAwaitingPacket(QuicPacketNumber packet_number) const;

  // Retrieves a frame containing a QuicAckFrame.  The ack frame may not be
  // changed outside QuicReceivedPacketManager and must be serialized before
  // another packet is received, or it will change.
  const QuicFrame GetUpdatedAckFrame(QuicTime approximate_now);

  // Deletes all missing packets before least unacked. The connection won't
  // process any packets with packet number before |least_unacked| that it
  // received after this call.
  void DontWaitForPacketsBefore(QuicPacketNumber least_unacked);

  // Called to update ack_timeout_ to the time when an ACK needs to be sent. A
  // caller can decide whether and when to send an ACK by retrieving
  // ack_timeout_. If ack_timeout_ is not initialized, no ACK needs to be sent.
  // Otherwise, ACK needs to be sent by the specified time.
  void MaybeUpdateAckTimeout(bool should_last_packet_instigate_acks,
                             QuicPacketNumber last_received_packet_number,
                             QuicTime time_of_last_received_packet,
                             QuicTime now,
                             const RttStats* rtt_stats);

  // Resets ACK related states, called after an ACK is successfully sent.
  void ResetAckStates();

  // Returns true if there are any missing packets.
  bool HasMissingPackets() const;

  // Returns true when there are new missing packets to be reported within 3
  // packets of the largest observed.
  virtual bool HasNewMissingPackets() const;

  QuicPacketNumber peer_least_packet_awaiting_ack() const {
    return peer_least_packet_awaiting_ack_;
  }

  virtual bool ack_frame_updated() const;

  QuicPacketNumber GetLargestObserved() const;

  // Returns peer first sending packet number to our best knowledge. Considers
  // least_received_packet_number_ as peer first sending packet number. Please
  // note, this function should only be called when at least one packet has been
  // received.
  QuicPacketNumber PeerFirstSendingPacketNumber() const;

  // Returns true if ack frame is empty.
  bool IsAckFrameEmpty() const;

  void set_connection_stats(QuicConnectionStats* stats) { stats_ = stats; }

  // For logging purposes.
  const QuicAckFrame& ack_frame() const { return ack_frame_; }

  void set_max_ack_ranges(size_t max_ack_ranges) {
    max_ack_ranges_ = max_ack_ranges;
  }

  void set_save_timestamps(bool save_timestamps) {
    save_timestamps_ = save_timestamps;
  }

  size_t min_received_before_ack_decimation() const {
    return min_received_before_ack_decimation_;
  }
  void set_min_received_before_ack_decimation(size_t new_value) {
    min_received_before_ack_decimation_ = new_value;
  }

  size_t ack_frequency_before_ack_decimation() const {
    return ack_frequency_before_ack_decimation_;
  }
  void set_ack_frequency_before_ack_decimation(size_t new_value) {
    DCHECK_GT(new_value, 0u);
    ack_frequency_before_ack_decimation_ = new_value;
  }

  QuicTime::Delta local_max_ack_delay() const { return local_max_ack_delay_; }
  void set_local_max_ack_delay(QuicTime::Delta local_max_ack_delay) {
    local_max_ack_delay_ = local_max_ack_delay;
  }

  QuicTime ack_timeout() const { return ack_timeout_; }

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicReceivedPacketManagerPeer;
  friend class test::UberReceivedPacketManagerPeer;

  // Sets ack_timeout_ to |time| if ack_timeout_ is not initialized or > time.
  void MaybeUpdateAckTimeoutTo(QuicTime time);

  // Least packet number of the the packet sent by the peer for which it
  // hasn't received an ack.
  QuicPacketNumber peer_least_packet_awaiting_ack_;

  // Received packet information used to produce acks.
  QuicAckFrame ack_frame_;

  // True if |ack_frame_| has been updated since UpdateReceivedPacketInfo was
  // last called.
  bool ack_frame_updated_;

  // Maximum number of ack ranges allowed to be stored in the ack frame.
  size_t max_ack_ranges_;

  // The time we received the largest_observed packet number, or zero if
  // no packet numbers have been received since UpdateReceivedPacketInfo.
  // Needed for calculating ack_delay_time.
  QuicTime time_largest_observed_;

  // If true, save timestamps in the ack_frame_.
  bool save_timestamps_;

  // Least packet number received from peer.
  QuicPacketNumber least_received_packet_number_;

  QuicConnectionStats* stats_;

  AckMode ack_mode_;
  // How many retransmittable packets have arrived without sending an ack.
  QuicPacketCount num_retransmittable_packets_received_since_last_ack_sent_;
  // Ack decimation will start happening after this many packets are received.
  size_t min_received_before_ack_decimation_;
  // Before ack decimation starts (if enabled), we ack every n-th packet.
  size_t ack_frequency_before_ack_decimation_;
  // The max delay in fraction of min_rtt to use when sending decimated acks.
  float ack_decimation_delay_;
  // When true, removes ack decimation's max number of packets(10) before
  // sending an ack.
  bool unlimited_ack_decimation_;
  // When true, use a 1ms delayed ack timer if it's been an SRTT since a packet
  // was received.
  bool fast_ack_after_quiescence_;
  // When true, only send 1 immediate ACK when reordering is detected.
  bool one_immediate_ack_;

  // The local node's maximum ack delay time. This is the maximum amount of
  // time to wait before sending an acknowledgement.
  QuicTime::Delta local_max_ack_delay_;
  // Time that an ACK needs to be sent. 0 means no ACK is pending. Used when
  // decide_when_to_send_acks_ is true.
  QuicTime ack_timeout_;

  // The time the previous ack-instigating packet was received and processed.
  QuicTime time_of_previous_received_packet_;
  // Whether the most recent packet was missing before it was received.
  bool was_last_packet_missing_;

  // Last sent largest acked, which gets updated when ACK was successfully sent.
  QuicPacketNumber last_sent_largest_acked_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_RECEIVED_PACKET_MANAGER_H_
