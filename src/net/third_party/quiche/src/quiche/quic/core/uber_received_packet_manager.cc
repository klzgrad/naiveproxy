// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/uber_received_packet_manager.h"

#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

UberReceivedPacketManager::UberReceivedPacketManager(QuicConnectionStats* stats)
    : supports_multiple_packet_number_spaces_(false) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.set_connection_stats(stats);
  }
}

UberReceivedPacketManager::~UberReceivedPacketManager() {}

void UberReceivedPacketManager::SetFromConfig(const QuicConfig& config,
                                              Perspective perspective) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.SetFromConfig(config, perspective);
  }
}

bool UberReceivedPacketManager::IsAwaitingPacket(
    EncryptionLevel decrypted_packet_level,
    QuicPacketNumber packet_number) const {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].IsAwaitingPacket(packet_number);
  }
  return received_packet_managers_[QuicUtils::GetPacketNumberSpace(
                                       decrypted_packet_level)]
      .IsAwaitingPacket(packet_number);
}

const QuicFrame UberReceivedPacketManager::GetUpdatedAckFrame(
    PacketNumberSpace packet_number_space, QuicTime approximate_now) {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].GetUpdatedAckFrame(approximate_now);
  }
  return received_packet_managers_[packet_number_space].GetUpdatedAckFrame(
      approximate_now);
}

void UberReceivedPacketManager::RecordPacketReceived(
    EncryptionLevel decrypted_packet_level, const QuicPacketHeader& header,
    QuicTime receipt_time, QuicEcnCodepoint ecn_codepoint) {
  if (!supports_multiple_packet_number_spaces_) {
    received_packet_managers_[0].RecordPacketReceived(header, receipt_time,
                                                      ecn_codepoint);
    return;
  }
  received_packet_managers_[QuicUtils::GetPacketNumberSpace(
                                decrypted_packet_level)]
      .RecordPacketReceived(header, receipt_time, ecn_codepoint);
}

void UberReceivedPacketManager::DontWaitForPacketsBefore(
    EncryptionLevel decrypted_packet_level, QuicPacketNumber least_unacked) {
  if (!supports_multiple_packet_number_spaces_) {
    received_packet_managers_[0].DontWaitForPacketsBefore(least_unacked);
    return;
  }
  received_packet_managers_[QuicUtils::GetPacketNumberSpace(
                                decrypted_packet_level)]
      .DontWaitForPacketsBefore(least_unacked);
}

void UberReceivedPacketManager::MaybeUpdateAckTimeout(
    bool should_last_packet_instigate_acks,
    EncryptionLevel decrypted_packet_level,
    QuicPacketNumber last_received_packet_number,
    QuicTime last_packet_receipt_time, QuicTime now,
    const RttStats* rtt_stats) {
  if (!supports_multiple_packet_number_spaces_) {
    received_packet_managers_[0].MaybeUpdateAckTimeout(
        should_last_packet_instigate_acks, last_received_packet_number,
        last_packet_receipt_time, now, rtt_stats);
    return;
  }
  received_packet_managers_[QuicUtils::GetPacketNumberSpace(
                                decrypted_packet_level)]
      .MaybeUpdateAckTimeout(should_last_packet_instigate_acks,
                             last_received_packet_number,
                             last_packet_receipt_time, now, rtt_stats);
}

void UberReceivedPacketManager::ResetAckStates(
    EncryptionLevel encryption_level) {
  if (!supports_multiple_packet_number_spaces_) {
    received_packet_managers_[0].ResetAckStates();
    return;
  }
  received_packet_managers_[QuicUtils::GetPacketNumberSpace(encryption_level)]
      .ResetAckStates();
  if (encryption_level == ENCRYPTION_INITIAL) {
    // After one Initial ACK is sent, the others should be sent 'immediately'.
    received_packet_managers_[INITIAL_DATA].set_local_max_ack_delay(
        kAlarmGranularity);
  }
}

void UberReceivedPacketManager::EnableMultiplePacketNumberSpacesSupport(
    Perspective perspective) {
  if (supports_multiple_packet_number_spaces_) {
    QUIC_BUG(quic_bug_10495_1)
        << "Multiple packet number spaces has already been enabled";
    return;
  }
  if (received_packet_managers_[0].GetLargestObserved().IsInitialized()) {
    QUIC_BUG(quic_bug_10495_2)
        << "Try to enable multiple packet number spaces support after any "
           "packet has been received.";
    return;
  }
  // In IETF QUIC, the peer is expected to acknowledge packets in Initial and
  // Handshake packets with minimal delay.
  if (perspective == Perspective::IS_CLIENT) {
    // Delay the first server ACK, because server ACKs are padded to
    // full size and count towards the amplification limit.
    received_packet_managers_[INITIAL_DATA].set_local_max_ack_delay(
        kAlarmGranularity);
  }
  received_packet_managers_[HANDSHAKE_DATA].set_local_max_ack_delay(
      kAlarmGranularity);

  supports_multiple_packet_number_spaces_ = true;
}

bool UberReceivedPacketManager::IsAckFrameUpdated() const {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].ack_frame_updated();
  }
  for (const auto& received_packet_manager : received_packet_managers_) {
    if (received_packet_manager.ack_frame_updated()) {
      return true;
    }
  }
  return false;
}

QuicPacketNumber UberReceivedPacketManager::GetLargestObserved(
    EncryptionLevel decrypted_packet_level) const {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].GetLargestObserved();
  }
  return received_packet_managers_[QuicUtils::GetPacketNumberSpace(
                                       decrypted_packet_level)]
      .GetLargestObserved();
}

QuicTime UberReceivedPacketManager::GetAckTimeout(
    PacketNumberSpace packet_number_space) const {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].ack_timeout();
  }
  return received_packet_managers_[packet_number_space].ack_timeout();
}

QuicTime UberReceivedPacketManager::GetEarliestAckTimeout() const {
  QuicTime ack_timeout = QuicTime::Zero();
  // Returns the earliest non-zero ack timeout.
  for (const auto& received_packet_manager : received_packet_managers_) {
    const QuicTime timeout = received_packet_manager.ack_timeout();
    if (!ack_timeout.IsInitialized()) {
      ack_timeout = timeout;
      continue;
    }
    if (timeout.IsInitialized()) {
      ack_timeout = std::min(ack_timeout, timeout);
    }
  }
  return ack_timeout;
}

bool UberReceivedPacketManager::IsAckFrameEmpty(
    PacketNumberSpace packet_number_space) const {
  if (!supports_multiple_packet_number_spaces_) {
    return received_packet_managers_[0].IsAckFrameEmpty();
  }
  return received_packet_managers_[packet_number_space].IsAckFrameEmpty();
}

size_t UberReceivedPacketManager::min_received_before_ack_decimation() const {
  return received_packet_managers_[0].min_received_before_ack_decimation();
}

void UberReceivedPacketManager::set_min_received_before_ack_decimation(
    size_t new_value) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.set_min_received_before_ack_decimation(new_value);
  }
}

void UberReceivedPacketManager::set_ack_frequency(size_t new_value) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.set_ack_frequency(new_value);
  }
}

const QuicAckFrame& UberReceivedPacketManager::ack_frame() const {
  QUICHE_DCHECK(!supports_multiple_packet_number_spaces_);
  return received_packet_managers_[0].ack_frame();
}

const QuicAckFrame& UberReceivedPacketManager::GetAckFrame(
    PacketNumberSpace packet_number_space) const {
  QUICHE_DCHECK(supports_multiple_packet_number_spaces_);
  return received_packet_managers_[packet_number_space].ack_frame();
}

void UberReceivedPacketManager::set_max_ack_ranges(size_t max_ack_ranges) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.set_max_ack_ranges(max_ack_ranges);
  }
}

void UberReceivedPacketManager::set_save_timestamps(bool save_timestamps) {
  for (auto& received_packet_manager : received_packet_managers_) {
    received_packet_manager.set_save_timestamps(
        save_timestamps, supports_multiple_packet_number_spaces_);
  }
}

void UberReceivedPacketManager::OnAckFrequencyFrame(
    const QuicAckFrequencyFrame& frame) {
  if (!supports_multiple_packet_number_spaces_) {
    QUIC_BUG(quic_bug_10495_3)
        << "Received AckFrequencyFrame when multiple packet number spaces "
           "is not supported";
    return;
  }
  received_packet_managers_[APPLICATION_DATA].OnAckFrequencyFrame(frame);
}

}  // namespace quic
