// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_UBER_RECEIVED_PACKET_MANAGER_H_
#define QUICHE_QUIC_CORE_UBER_RECEIVED_PACKET_MANAGER_H_

#include "net/third_party/quiche/src/quic/core/quic_received_packet_manager.h"

namespace quic {

// This class comprises multiple received packet managers, one per packet number
// space. Please note, if multiple packet number spaces is not supported, only
// one received packet manager will be used.
class QUIC_EXPORT_PRIVATE UberReceivedPacketManager {
 public:
  explicit UberReceivedPacketManager(QuicConnectionStats* stats);
  UberReceivedPacketManager(const UberReceivedPacketManager&) = delete;
  UberReceivedPacketManager& operator=(const UberReceivedPacketManager&) =
      delete;
  virtual ~UberReceivedPacketManager();

  void SetFromConfig(const QuicConfig& config, Perspective perspective);

  // Checks if we are still waiting for the packet with |packet_number|.
  bool IsAwaitingPacket(EncryptionLevel decrypted_packet_level,
                        QuicPacketNumber packet_number) const;

  // Called after a packet has been successfully decrypted and its header has
  // been parsed.
  void RecordPacketReceived(EncryptionLevel decrypted_packet_level,
                            const QuicPacketHeader& header,
                            QuicTime receipt_time);

  // Retrieves a frame containing a QuicAckFrame. The ack frame must be
  // serialized before another packet is received, or it will change.
  const QuicFrame GetUpdatedAckFrame(PacketNumberSpace packet_number_space,
                                     QuicTime approximate_now);

  // Stop ACKing packets before |least_unacked|.
  void DontWaitForPacketsBefore(EncryptionLevel decrypted_packet_level,
                                QuicPacketNumber least_unacked);

  // Called after header of last received packet has been successfully processed
  // to update ACK timeout.
  void MaybeUpdateAckTimeout(bool should_last_packet_instigate_acks,
                             EncryptionLevel decrypted_packet_level,
                             QuicPacketNumber last_received_packet_number,
                             QuicTime time_of_last_received_packet,
                             QuicTime now,
                             const RttStats* rtt_stats);

  // Resets ACK related states, called after an ACK is successfully sent.
  void ResetAckStates(EncryptionLevel encryption_level);

  // Called to enable multiple packet number support.
  void EnableMultiplePacketNumberSpacesSupport();

  // Returns true if ACK frame has been updated since GetUpdatedAckFrame was
  // last called.
  bool IsAckFrameUpdated() const;

  // Returns the largest received packet number.
  QuicPacketNumber GetLargestObserved(
      EncryptionLevel decrypted_packet_level) const;

  // Returns ACK timeout of |packet_number_space|.
  QuicTime GetAckTimeout(PacketNumberSpace packet_number_space) const;

  // Get the earliest ack_timeout of all packet number spaces.
  QuicTime GetEarliestAckTimeout() const;

  // Return true if ack frame of |packet_number_space| is empty.
  bool IsAckFrameEmpty(PacketNumberSpace packet_number_space) const;

  QuicPacketNumber peer_least_packet_awaiting_ack() const;

  size_t min_received_before_ack_decimation() const;
  void set_min_received_before_ack_decimation(size_t new_value);

  size_t ack_frequency_before_ack_decimation() const;
  void set_ack_frequency_before_ack_decimation(size_t new_value);

  bool supports_multiple_packet_number_spaces() const {
    return supports_multiple_packet_number_spaces_;
  }

  // For logging purposes.
  const QuicAckFrame& ack_frame() const;
  const QuicAckFrame& GetAckFrame(PacketNumberSpace packet_number_space) const;

  void set_max_ack_ranges(size_t max_ack_ranges);

  // Get and set the max ack delay to use for application data.
  QuicTime::Delta max_ack_delay();
  void set_max_ack_delay(QuicTime::Delta max_ack_delay);

  void set_save_timestamps(bool save_timestamps);

 private:
  friend class test::QuicConnectionPeer;
  friend class test::UberReceivedPacketManagerPeer;

  // One received packet manager per packet number space. If
  // supports_multiple_packet_number_spaces_ is false, only the first (0 index)
  // received_packet_manager is used.
  QuicReceivedPacketManager received_packet_managers_[NUM_PACKET_NUMBER_SPACES];

  bool supports_multiple_packet_number_spaces_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_UBER_RECEIVED_PACKET_MANAGER_H_
