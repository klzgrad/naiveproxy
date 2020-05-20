// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/uber_loss_algorithm.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"

namespace quic {

UberLossAlgorithm::UberLossAlgorithm() {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].SetPacketNumberSpace(
        static_cast<PacketNumberSpace>(i));
  }
}

void UberLossAlgorithm::DetectLosses(
    const QuicUnackedPacketMap& unacked_packets,
    QuicTime time,
    const RttStats& rtt_stats,
    QuicPacketNumber /*largest_newly_acked*/,
    const AckedPacketVector& packets_acked,
    LostPacketVector* packets_lost) {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    const QuicPacketNumber largest_acked =
        unacked_packets.GetLargestAckedOfPacketNumberSpace(
            static_cast<PacketNumberSpace>(i));
    if (!largest_acked.IsInitialized() ||
        unacked_packets.GetLeastUnacked() > largest_acked) {
      // Skip detecting losses if no packet has been received for this packet
      // number space or the least_unacked is greater than largest_acked.
      continue;
    }

    general_loss_algorithms_[i].DetectLosses(unacked_packets, time, rtt_stats,
                                             largest_acked, packets_acked,
                                             packets_lost);
  }
}

QuicTime UberLossAlgorithm::GetLossTimeout() const {
  QuicTime loss_timeout = QuicTime::Zero();
  // Returns the earliest non-zero loss timeout.
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    const QuicTime timeout = general_loss_algorithms_[i].GetLossTimeout();
    if (!loss_timeout.IsInitialized()) {
      loss_timeout = timeout;
      continue;
    }
    if (timeout.IsInitialized()) {
      loss_timeout = std::min(loss_timeout, timeout);
    }
  }
  return loss_timeout;
}

void UberLossAlgorithm::SpuriousLossDetected(
    const QuicUnackedPacketMap& unacked_packets,
    const RttStats& rtt_stats,
    QuicTime ack_receive_time,
    QuicPacketNumber packet_number,
    QuicPacketNumber previous_largest_acked) {
  general_loss_algorithms_[unacked_packets.GetPacketNumberSpace(packet_number)]
      .SpuriousLossDetected(unacked_packets, rtt_stats, ack_receive_time,
                            packet_number, previous_largest_acked);
}

void UberLossAlgorithm::SetLossDetectionTuner(
    std::unique_ptr<LossDetectionTunerInterface> tuner) {
  if (tuner_ != nullptr) {
    QUIC_BUG << "LossDetectionTuner can only be set once when session begins.";
    return;
  }
  tuner_ = std::move(tuner);
}

void UberLossAlgorithm::MaybeStartTuning() {
  if (tuner_ == nullptr || tuner_started_) {
    return;
  }

  tuner_started_ = tuner_->Start(&tuned_parameters_);
}

void UberLossAlgorithm::OnConfigNegotiated() {}

void UberLossAlgorithm::OnMinRttAvailable() {
  MaybeStartTuning();
}

void UberLossAlgorithm::OnConnectionClosed() {
  if (tuner_ != nullptr && tuner_started_) {
    tuner_->Finish(tuned_parameters_);
  }
}

void UberLossAlgorithm::SetReorderingShift(int reordering_shift) {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].set_reordering_shift(reordering_shift);
  }
}

void UberLossAlgorithm::SetReorderingThreshold(
    QuicPacketCount reordering_threshold) {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].set_reordering_threshold(reordering_threshold);
  }
}

void UberLossAlgorithm::EnableAdaptiveReorderingThreshold() {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].set_use_adaptive_reordering_threshold(true);
  }
}

void UberLossAlgorithm::DisableAdaptiveReorderingThreshold() {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].set_use_adaptive_reordering_threshold(false);
  }
}

void UberLossAlgorithm::EnableAdaptiveTimeThreshold() {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].enable_adaptive_time_threshold();
  }
}

void UberLossAlgorithm::DisablePacketThresholdForRuntPackets() {
  for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    general_loss_algorithms_[i].disable_packet_threshold_for_runt_packets();
  }
}

void UberLossAlgorithm::ResetLossDetection(PacketNumberSpace space) {
  if (space >= NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG << "Invalid packet number space: " << space;
    return;
  }
  general_loss_algorithms_[space].Reset();
}

}  // namespace quic
