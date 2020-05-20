// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_coalesced_packet.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

QuicCoalescedPacket::QuicCoalescedPacket()
    : length_(0), max_packet_length_(0) {}

QuicCoalescedPacket::~QuicCoalescedPacket() {
  Clear();
}

bool QuicCoalescedPacket::MaybeCoalescePacket(
    const SerializedPacket& packet,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    QuicBufferAllocator* allocator,
    QuicPacketLength current_max_packet_length) {
  if (packet.encrypted_length == 0) {
    QUIC_BUG << "Trying to coalesce an empty packet";
    return true;
  }
  if (length_ == 0) {
#ifndef NDEBUG
    for (const auto& buffer : encrypted_buffers_) {
      DCHECK(buffer.empty());
    }
#endif
    DCHECK(initial_packet_ == nullptr);
    // This is the first packet, set max_packet_length and self/peer
    // addresses.
    max_packet_length_ = current_max_packet_length;
    self_address_ = self_address;
    peer_address_ = peer_address;
  } else {
    if (self_address_ != self_address || peer_address_ != peer_address) {
      // Do not coalesce packet with different self/peer addresses.
      QUIC_DLOG(INFO)
          << "Cannot coalesce packet because self/peer address changed";
      return false;
    }
    if (max_packet_length_ != current_max_packet_length) {
      QUIC_BUG << "Max packet length changes in the middle of the write path";
      return false;
    }
    if (ContainsPacketOfEncryptionLevel(packet.encryption_level)) {
      // Do not coalesce packets of the same encryption level.
      return false;
    }
  }

  if (length_ + packet.encrypted_length > max_packet_length_) {
    // Packet does not fit.
    return false;
  }
  QUIC_DVLOG(1) << "Successfully coalesced packet: encryption_level: "
                << EncryptionLevelToString(packet.encryption_level)
                << ", encrypted_length: " << packet.encrypted_length
                << ", current length: " << length_
                << ", max_packet_length: " << max_packet_length_;
  if (length_ > 0) {
    QUIC_CODE_COUNT(QUIC_SUCCESSFULLY_COALESCED_MULTIPLE_PACKETS);
  }
  length_ += packet.encrypted_length;
  if (packet.encryption_level == ENCRYPTION_INITIAL) {
    // Save a copy of ENCRYPTION_INITIAL packet (excluding encrypted buffer, as
    // the packet will be re-serialized later).
    initial_packet_ = QuicWrapUnique<SerializedPacket>(
        CopySerializedPacket(packet, allocator, /*copy_buffer=*/false));
    return true;
  }
  // Copy encrypted buffer of packets with other encryption levels.
  encrypted_buffers_[packet.encryption_level] =
      std::string(packet.encrypted_buffer, packet.encrypted_length);
  return true;
}

void QuicCoalescedPacket::Clear() {
  self_address_ = QuicSocketAddress();
  peer_address_ = QuicSocketAddress();
  length_ = 0;
  max_packet_length_ = 0;
  for (auto& packet : encrypted_buffers_) {
    packet.clear();
  }
  if (initial_packet_ != nullptr) {
    ClearSerializedPacket(initial_packet_.get());
  }
  initial_packet_ = nullptr;
}

bool QuicCoalescedPacket::CopyEncryptedBuffers(char* buffer,
                                               size_t buffer_len,
                                               size_t* length_copied) const {
  *length_copied = 0;
  for (const auto& packet : encrypted_buffers_) {
    if (packet.empty()) {
      continue;
    }
    if (packet.length() > buffer_len) {
      return false;
    }
    memcpy(buffer, packet.data(), packet.length());
    buffer += packet.length();
    buffer_len -= packet.length();
    *length_copied += packet.length();
  }
  return true;
}

bool QuicCoalescedPacket::ContainsPacketOfEncryptionLevel(
    EncryptionLevel level) const {
  return !encrypted_buffers_[level].empty() ||
         (level == ENCRYPTION_INITIAL && initial_packet_ != nullptr);
}

std::string QuicCoalescedPacket::ToString(size_t serialized_length) const {
  // Total length and padding size.
  std::string info = quiche::QuicheStrCat(
      "total_length: ", serialized_length,
      " padding_size: ", serialized_length - length_, " packets: {");
  // Packets' encryption levels.
  bool first_packet = true;
  for (int8_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    if (ContainsPacketOfEncryptionLevel(static_cast<EncryptionLevel>(i))) {
      info = quiche::QuicheStrCat(
          info, first_packet ? "" : ", ",
          EncryptionLevelToString(static_cast<EncryptionLevel>(i)));
      first_packet = false;
    }
  }
  info = quiche::QuicheStrCat(info, "}");
  return info;
}

}  // namespace quic
