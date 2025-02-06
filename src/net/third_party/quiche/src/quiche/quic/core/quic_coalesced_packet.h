// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_
#define QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_

#include "quiche/quic/core/quic_packets.h"

namespace quic {

namespace test {
class QuicCoalescedPacketPeer;
}

// QuicCoalescedPacket is used to buffer multiple packets which can be coalesced
// into the same UDP datagram.
class QUICHE_EXPORT QuicCoalescedPacket {
 public:
  QuicCoalescedPacket();
  ~QuicCoalescedPacket();

  // Returns true if |packet| is successfully coalesced with existing packets.
  // Returns false otherwise.
  bool MaybeCoalescePacket(const SerializedPacket& packet,
                           const QuicSocketAddress& self_address,
                           const QuicSocketAddress& peer_address,
                           quiche::QuicheBufferAllocator* allocator,
                           QuicPacketLength current_max_packet_length,
                           QuicEcnCodepoint ecn_codepoint, uint32_t flow_label);

  // Clears this coalesced packet.
  void Clear();

  // Clears all state associated with initial_packet_.
  void NeuterInitialPacket();

  // Copies encrypted_buffers_ to |buffer| and sets |length_copied| to the
  // copied amount. Returns false if copy fails (i.e., |buffer_len| is not
  // enough).
  bool CopyEncryptedBuffers(char* buffer, size_t buffer_len,
                            size_t* length_copied) const;

  std::string ToString(size_t serialized_length) const;

  // Returns true if this coalesced packet contains packet of |level|.
  bool ContainsPacketOfEncryptionLevel(EncryptionLevel level) const;

  // Returns transmission type of packet of |level|. This should only be called
  // when this coalesced packet contains packet of |level|.
  TransmissionType TransmissionTypeOfPacket(EncryptionLevel level) const;

  // Returns number of packets contained in this coalesced packet.
  size_t NumberOfPackets() const;

  const SerializedPacket* initial_packet() const {
    return initial_packet_.get();
  }

  const QuicSocketAddress& self_address() const { return self_address_; }

  const QuicSocketAddress& peer_address() const { return peer_address_; }

  QuicPacketLength length() const { return length_; }

  QuicPacketLength max_packet_length() const { return max_packet_length_; }

  std::vector<size_t> packet_lengths() const;

  QuicEcnCodepoint ecn_codepoint() const { return ecn_codepoint_; }

  uint32_t flow_label() const { return flow_label_; }

 private:
  friend class test::QuicCoalescedPacketPeer;

  // self/peer addresses are set when trying to coalesce the first packet.
  // Packets with different self/peer addresses cannot be coalesced.
  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;
  // Length of this coalesced packet.
  QuicPacketLength length_;
  // Max packet length. Do not try to coalesce packet when max packet length
  // changes (e.g., with MTU discovery).
  QuicPacketLength max_packet_length_;
  // Copies of packets' encrypted buffers according to different encryption
  // levels.
  std::string encrypted_buffers_[NUM_ENCRYPTION_LEVELS];
  // Recorded transmission type according to different encryption levels.
  TransmissionType transmission_types_[NUM_ENCRYPTION_LEVELS];

  // A copy of ENCRYPTION_INITIAL packet if this coalesced packet contains one.
  // Null otherwise. Please note, the encrypted_buffer field is not copied. The
  // frames are copied to allow it be re-serialized when this coalesced packet
  // gets sent.
  std::unique_ptr<SerializedPacket> initial_packet_;

  // A coalesced packet shares an ECN codepoint.
  QuicEcnCodepoint ecn_codepoint_;

  // A coalesced packet shares an single flow label.
  uint32_t flow_label_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_
