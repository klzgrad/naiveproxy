// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_
#define QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {

// QuicCoalescedPacket is used to buffer multiple packets which can be coalesced
// into the same UDP datagram.
class QUIC_EXPORT_PRIVATE QuicCoalescedPacket {
 public:
  QuicCoalescedPacket();
  ~QuicCoalescedPacket();

  // Returns true if |packet| is successfully coalesced with existing packets.
  // Returns false otherwise.
  bool MaybeCoalescePacket(const SerializedPacket& packet,
                           const QuicSocketAddress& self_address,
                           const QuicSocketAddress& peer_address,
                           QuicBufferAllocator* allocator,
                           QuicPacketLength current_max_packet_length);

  // Clears this coalesced packet.
  void Clear();

  // Copies encrypted_buffers_ to |buffer| and sets |length_copied| to the
  // copied amount. Returns false if copy fails (i.e., |buffer_len| is not
  // enough).
  bool CopyEncryptedBuffers(char* buffer,
                            size_t buffer_len,
                            size_t* length_copied) const;

  std::string ToString(size_t serialized_length) const;

  const SerializedPacket* initial_packet() const {
    return initial_packet_.get();
  }

  const QuicSocketAddress& self_address() const { return self_address_; }

  const QuicSocketAddress& peer_address() const { return peer_address_; }

  QuicPacketLength length() const { return length_; }

  QuicPacketLength max_packet_length() const { return max_packet_length_; }

 private:
  // Returns true if this coalesced packet contains packet of |level|.
  bool ContainsPacketOfEncryptionLevel(EncryptionLevel level) const;

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

  // A copy of ENCRYPTION_INITIAL packet if this coalesced packet contains one.
  // Null otherwise. Please note, the encrypted_buffer field is not copied. The
  // frames are copied to allow it be re-serialized when this coalesced packet
  // gets sent.
  std::unique_ptr<SerializedPacket> initial_packet_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_COALESCED_PACKET_H_
