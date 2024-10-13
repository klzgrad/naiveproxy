// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_coalesced_packet.h"

#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

QuicCoalescedPacket::QuicCoalescedPacket()
    : length_(0), max_packet_length_(0), ecn_codepoint_(ECN_NOT_ECT) {}

QuicCoalescedPacket::~QuicCoalescedPacket() { Clear(); }

bool QuicCoalescedPacket::MaybeCoalescePacket(
    const SerializedPacket& packet, const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    quiche::QuicheBufferAllocator* allocator,
    QuicPacketLength current_max_packet_length,
    QuicEcnCodepoint ecn_codepoint) {
  if (packet.encrypted_length == 0) {
    QUIC_BUG(quic_bug_10611_1) << "Trying to coalesce an empty packet";
    return true;
  }
  if (length_ == 0) {
#ifndef NDEBUG
    for (const auto& buffer : encrypted_buffers_) {
      QUICHE_DCHECK(buffer.empty());
    }
#endif
    QUICHE_DCHECK(initial_packet_ == nullptr);
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
      QUIC_BUG(quic_bug_10611_2)
          << "Max packet length changes in the middle of the write path";
      return false;
    }
    if (ContainsPacketOfEncryptionLevel(packet.encryption_level)) {
      // Do not coalesce packets of the same encryption level.
      return false;
    }
    if (ecn_codepoint != ecn_codepoint_) {
      // Do not coalesce packets with different ECN codepoints.
      return false;
    }
  }

  if (length_ + packet.encrypted_length > max_packet_length_) {
    // Packet does not fit.
    return false;
  }
  QUIC_DVLOG(1) << "Successfully coalesced packet: encryption_level: "
                << packet.encryption_level
                << ", encrypted_length: " << packet.encrypted_length
                << ", current length: " << length_
                << ", max_packet_length: " << max_packet_length_;
  if (length_ > 0) {
    QUIC_CODE_COUNT(QUIC_SUCCESSFULLY_COALESCED_MULTIPLE_PACKETS);
  }
  ecn_codepoint_ = ecn_codepoint;
  length_ += packet.encrypted_length;
  transmission_types_[packet.encryption_level] = packet.transmission_type;
  if (packet.encryption_level == ENCRYPTION_INITIAL) {
    // Save a copy of ENCRYPTION_INITIAL packet (excluding encrypted buffer, as
    // the packet will be re-serialized later).
    initial_packet_ = absl::WrapUnique<SerializedPacket>(
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
  for (size_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    transmission_types_[i] = NOT_RETRANSMISSION;
  }
  initial_packet_ = nullptr;
}

void QuicCoalescedPacket::NeuterInitialPacket() {
  if (initial_packet_ == nullptr) {
    return;
  }
  if (length_ < initial_packet_->encrypted_length) {
    QUIC_BUG(quic_bug_10611_3)
        << "length_: " << length_ << ", is less than initial packet length: "
        << initial_packet_->encrypted_length;
    Clear();
    return;
  }
  length_ -= initial_packet_->encrypted_length;
  if (length_ == 0) {
    Clear();
    return;
  }
  transmission_types_[ENCRYPTION_INITIAL] = NOT_RETRANSMISSION;
  initial_packet_ = nullptr;
}

bool QuicCoalescedPacket::CopyEncryptedBuffers(char* buffer, size_t buffer_len,
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

TransmissionType QuicCoalescedPacket::TransmissionTypeOfPacket(
    EncryptionLevel level) const {
  if (!ContainsPacketOfEncryptionLevel(level)) {
    QUIC_BUG(quic_bug_10611_4)
        << "Coalesced packet does not contain packet of encryption level: "
        << EncryptionLevelToString(level);
    return NOT_RETRANSMISSION;
  }
  return transmission_types_[level];
}

size_t QuicCoalescedPacket::NumberOfPackets() const {
  size_t num_of_packets = 0;
  for (int8_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    if (ContainsPacketOfEncryptionLevel(static_cast<EncryptionLevel>(i))) {
      ++num_of_packets;
    }
  }
  return num_of_packets;
}

std::string QuicCoalescedPacket::ToString(size_t serialized_length) const {
  // Total length and padding size.
  std::string info = absl::StrCat(
      "total_length: ", serialized_length,
      " padding_size: ", serialized_length - length_, " packets: {");
  // Packets' encryption levels.
  bool first_packet = true;
  for (int8_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    if (ContainsPacketOfEncryptionLevel(static_cast<EncryptionLevel>(i))) {
      absl::StrAppend(&info, first_packet ? "" : ", ",
                      EncryptionLevelToString(static_cast<EncryptionLevel>(i)));
      first_packet = false;
    }
  }
  absl::StrAppend(&info, "}");
  return info;
}

std::vector<size_t> QuicCoalescedPacket::packet_lengths() const {
  std::vector<size_t> lengths;
  for (const auto& packet : encrypted_buffers_) {
    if (lengths.empty()) {
      lengths.push_back(
          initial_packet_ == nullptr ? 0 : initial_packet_->encrypted_length);
    } else {
      lengths.push_back(packet.length());
    }
  }
  return lengths;
}

}  // namespace quic
