// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_UNACKED_PACKET_MAP_H_
#define QUICHE_QUIC_CORE_QUIC_UNACKED_PACKET_MAP_H_

#include <cstddef>
#include <deque>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_transmission_info.h"
#include "net/third_party/quiche/src/quic/core/session_notifier_interface.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicUnackedPacketMapPeer;
}  // namespace test

// Class which tracks unacked packets for three purposes:
// 1) Track retransmittable data, including multiple transmissions of frames.
// 2) Track packets and bytes in flight for congestion control.
// 3) Track sent time of packets to provide RTT measurements from acks.
class QUIC_EXPORT_PRIVATE QuicUnackedPacketMap {
 public:
  QuicUnackedPacketMap(Perspective perspective);
  QuicUnackedPacketMap(const QuicUnackedPacketMap&) = delete;
  QuicUnackedPacketMap& operator=(const QuicUnackedPacketMap&) = delete;
  ~QuicUnackedPacketMap();

  // Adds |serialized_packet| to the map and marks it as sent at |sent_time|.
  // Marks the packet as in flight if |set_in_flight| is true.
  // Packets marked as in flight are expected to be marked as missing when they
  // don't arrive, indicating the need for retransmission.
  // Any AckNotifierWrappers in |serialized_packet| are swapped from the
  // serialized packet into the QuicTransmissionInfo.
  void AddSentPacket(SerializedPacket* serialized_packet,
                     TransmissionType transmission_type,
                     QuicTime sent_time,
                     bool set_in_flight);

  // Returns true if the packet |packet_number| is unacked.
  bool IsUnacked(QuicPacketNumber packet_number) const;

  // Notifies session_notifier that frames have been acked. Returns true if any
  // new data gets acked, returns false otherwise.
  bool NotifyFramesAcked(const QuicTransmissionInfo& info,
                         QuicTime::Delta ack_delay,
                         QuicTime receive_timestamp);

  // Notifies session_notifier that frames in |info| are considered as lost.
  void NotifyFramesLost(const QuicTransmissionInfo& info,
                        TransmissionType type);

  // Notifies session_notifier to retransmit frames in |info| with
  // |transmission_type|.
  void RetransmitFrames(const QuicTransmissionInfo& info,
                        TransmissionType type);

  // Marks |info| as no longer in flight.
  void RemoveFromInFlight(QuicTransmissionInfo* info);

  // Marks |packet_number| as no longer in flight.
  void RemoveFromInFlight(QuicPacketNumber packet_number);

  // Called to neuter all unencrypted packets to ensure they do not get
  // retransmitted. Returns a vector of neutered packet numbers.
  QuicInlinedVector<QuicPacketNumber, 2> NeuterUnencryptedPackets();

  // Called to neuter packets in handshake packet number space to ensure they do
  // not get retransmitted. Returns a vector of neutered packet numbers.
  // TODO(fayang): Consider to combine this with NeuterUnencryptedPackets.
  QuicInlinedVector<QuicPacketNumber, 2> NeuterHandshakePackets();

  // Returns true if |packet_number| has retransmittable frames. This will
  // return false if all frames of this packet are either non-retransmittable or
  // have been acked.
  bool HasRetransmittableFrames(QuicPacketNumber packet_number) const;

  // Returns true if |info| has retransmittable frames. This will return false
  // if all frames of this packet are either non-retransmittable or have been
  // acked.
  bool HasRetransmittableFrames(const QuicTransmissionInfo& info) const;

  // Returns true if there are any unacked packets which have retransmittable
  // frames.
  bool HasUnackedRetransmittableFrames() const;

  // Returns true if there are no packets present in the unacked packet map.
  bool empty() const { return unacked_packets_.empty(); }

  // Returns the largest packet number that has been sent.
  QuicPacketNumber largest_sent_packet() const { return largest_sent_packet_; }

  QuicPacketNumber largest_sent_largest_acked() const {
    return largest_sent_largest_acked_;
  }

  // Returns the largest packet number that has been acked.
  QuicPacketNumber largest_acked() const { return largest_acked_; }

  // Returns the sum of bytes from all packets in flight.
  QuicByteCount bytes_in_flight() const { return bytes_in_flight_; }
  QuicPacketCount packets_in_flight() const { return packets_in_flight_; }

  // Returns the smallest packet number of a serialized packet which has not
  // been acked by the peer.  If there are no unacked packets, returns 0.
  QuicPacketNumber GetLeastUnacked() const;

  // This can not be a QuicCircularDeque since pointers into this are
  // assumed to be stable.
  typedef std::deque<QuicTransmissionInfo> UnackedPacketMap;

  typedef UnackedPacketMap::const_iterator const_iterator;
  typedef UnackedPacketMap::iterator iterator;

  const_iterator begin() const { return unacked_packets_.begin(); }
  const_iterator end() const { return unacked_packets_.end(); }
  iterator begin() { return unacked_packets_.begin(); }
  iterator end() { return unacked_packets_.end(); }

  // Returns true if there are unacked packets that are in flight.
  bool HasInFlightPackets() const;

  // Returns the QuicTransmissionInfo associated with |packet_number|, which
  // must be unacked.
  const QuicTransmissionInfo& GetTransmissionInfo(
      QuicPacketNumber packet_number) const;

  // Returns mutable QuicTransmissionInfo associated with |packet_number|, which
  // must be unacked.
  QuicTransmissionInfo* GetMutableTransmissionInfo(
      QuicPacketNumber packet_number);

  // Returns the time that the last unacked packet was sent.
  QuicTime GetLastInFlightPacketSentTime() const;

  // Returns the time that the last unacked crypto packet was sent.
  QuicTime GetLastCryptoPacketSentTime() const;

  // Returns the number of unacked packets.
  size_t GetNumUnackedPacketsDebugOnly() const;

  // Returns true if there are multiple packets in flight.
  // TODO(fayang): Remove this method and use packets_in_flight_ instead.
  bool HasMultipleInFlightPackets() const;

  // Returns true if there are any pending crypto packets.
  bool HasPendingCryptoPackets() const;

  // Returns true if there is any unacked non-crypto stream data.
  bool HasUnackedStreamData() const {
    return session_notifier_->HasUnackedStreamData();
  }

  // Removes any retransmittable frames from this transmission or an associated
  // transmission.  It removes now useless transmissions, and disconnects any
  // other packets from other transmissions.
  void RemoveRetransmittability(QuicTransmissionInfo* info);

  // Looks up the QuicTransmissionInfo by |packet_number| and calls
  // RemoveRetransmittability.
  void RemoveRetransmittability(QuicPacketNumber packet_number);

  // Increases the largest acked.  Any packets less or equal to
  // |largest_acked| are discarded if they are only for the RTT purposes.
  void IncreaseLargestAcked(QuicPacketNumber largest_acked);

  // Called when |packet_number| gets acked. Maybe increase the largest acked of
  // |packet_number_space|.
  void MaybeUpdateLargestAckedOfPacketNumberSpace(
      PacketNumberSpace packet_number_space,
      QuicPacketNumber packet_number);

  // Remove any packets no longer needed for retransmission, congestion, or
  // RTT measurement purposes.
  void RemoveObsoletePackets();

  // Try to aggregate acked contiguous stream frames. For noncontiguous stream
  // frames or control frames, notify the session notifier they get acked
  // immediately.
  void MaybeAggregateAckedStreamFrame(const QuicTransmissionInfo& info,
                                      QuicTime::Delta ack_delay,
                                      QuicTime receive_timestamp);

  // Notify the session notifier of any stream data aggregated in
  // aggregated_stream_frame_.  No effect if the stream frame has an invalid
  // stream id.
  void NotifyAggregatedStreamFrameAcked(QuicTime::Delta ack_delay);

  // Returns packet number space that |packet_number| belongs to. Please use
  // GetPacketNumberSpace(EncryptionLevel) whenever encryption level is
  // available.
  PacketNumberSpace GetPacketNumberSpace(QuicPacketNumber packet_number) const;

  // Returns packet number space of |encryption_level|.
  PacketNumberSpace GetPacketNumberSpace(
      EncryptionLevel encryption_level) const;

  // Returns largest acked packet number of |packet_number_space|.
  QuicPacketNumber GetLargestAckedOfPacketNumberSpace(
      PacketNumberSpace packet_number_space) const;

  // Returns largest sent retransmittable packet number of
  // |packet_number_space|.
  QuicPacketNumber GetLargestSentRetransmittableOfPacketNumberSpace(
      PacketNumberSpace packet_number_space) const;

  // Returns largest sent packet number of |encryption_level|.
  QuicPacketNumber GetLargestSentPacketOfPacketNumberSpace(
      EncryptionLevel encryption_level) const;

  // Returns last in flight packet sent time of |packet_number_space|.
  QuicTime GetLastInFlightPacketSentTime(
      PacketNumberSpace packet_number_space) const;

  // Returns TransmissionInfo of the first in flight packet.
  const QuicTransmissionInfo* GetFirstInFlightTransmissionInfo() const;

  // Returns TransmissionInfo of first in flight packet in
  // |packet_number_space|.
  const QuicTransmissionInfo* GetFirstInFlightTransmissionInfoOfSpace(
      PacketNumberSpace packet_number_space) const;

  void SetSessionNotifier(SessionNotifierInterface* session_notifier);

  void EnableMultiplePacketNumberSpacesSupport();

  Perspective perspective() const { return perspective_; }

  bool supports_multiple_packet_number_spaces() const {
    return supports_multiple_packet_number_spaces_;
  }

 private:
  friend class test::QuicUnackedPacketMapPeer;

  // Returns true if packet may be useful for an RTT measurement.
  bool IsPacketUsefulForMeasuringRtt(QuicPacketNumber packet_number,
                                     const QuicTransmissionInfo& info) const;

  // Returns true if packet may be useful for congestion control purposes.
  bool IsPacketUsefulForCongestionControl(
      const QuicTransmissionInfo& info) const;

  // Returns true if packet may be associated with retransmittable data
  // directly or through retransmissions.
  bool IsPacketUsefulForRetransmittableData(
      const QuicTransmissionInfo& info) const;

  // Returns true if the packet no longer has a purpose in the map.
  bool IsPacketUseless(QuicPacketNumber packet_number,
                       const QuicTransmissionInfo& info) const;

  const Perspective perspective_;

  QuicPacketNumber largest_sent_packet_;
  // The largest sent packet we expect to receive an ack for per packet number
  // space.
  QuicPacketNumber
      largest_sent_retransmittable_packets_[NUM_PACKET_NUMBER_SPACES];
  // The largest sent largest_acked in an ACK frame.
  QuicPacketNumber largest_sent_largest_acked_;
  // The largest received largest_acked from an ACK frame.
  QuicPacketNumber largest_acked_;
  // The largest received largest_acked from ACK frame per packet number space.
  QuicPacketNumber largest_acked_packets_[NUM_PACKET_NUMBER_SPACES];

  // Newly serialized retransmittable packets are added to this map, which
  // contains owning pointers to any contained frames.  If a packet is
  // retransmitted, this map will contain entries for both the old and the new
  // packet. The old packet's retransmittable frames entry will be nullptr,
  // while the new packet's entry will contain the frames to retransmit.
  // If the old packet is acked before the new packet, then the old entry will
  // be removed from the map and the new entry's retransmittable frames will be
  // set to nullptr.
  UnackedPacketMap unacked_packets_;
  // The packet at the 0th index of unacked_packets_.
  QuicPacketNumber least_unacked_;

  QuicByteCount bytes_in_flight_;
  QuicPacketCount packets_in_flight_;

  // Time that the last inflight packet was sent.
  QuicTime last_inflight_packet_sent_time_;
  // Time that the last in flight packet was sent per packet number space.
  QuicTime last_inflight_packets_sent_time_[NUM_PACKET_NUMBER_SPACES];

  // Time that the last unacked crypto packet was sent.
  QuicTime last_crypto_packet_sent_time_;

  // Aggregates acked stream data across multiple acked sent packets to save CPU
  // by reducing the number of calls to the session notifier.
  QuicStreamFrame aggregated_stream_frame_;

  // Receives notifications of frames being retransmitted or acknowledged.
  SessionNotifierInterface* session_notifier_;

  // If true, supports multiple packet number spaces.
  bool supports_multiple_packet_number_spaces_;

  // Latched value of the quic_simple_inflight_time flag.
  bool simple_inflight_time_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_UNACKED_PACKET_MAP_H_
