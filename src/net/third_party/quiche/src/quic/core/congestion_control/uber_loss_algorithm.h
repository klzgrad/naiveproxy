// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_

#include "net/third_party/quiche/src/quic/core/congestion_control/general_loss_algorithm.h"

namespace quic {

namespace test {

class QuicSentPacketManagerPeer;

}  // namespace test

// This class comprises multiple loss algorithms, each per packet number space.
class QUIC_EXPORT_PRIVATE UberLossAlgorithm : public LossDetectionInterface {
 public:
  UberLossAlgorithm();
  explicit UberLossAlgorithm(LossDetectionType loss_type);
  UberLossAlgorithm(const UberLossAlgorithm&) = delete;
  UberLossAlgorithm& operator=(const UberLossAlgorithm&) = delete;
  ~UberLossAlgorithm() override {}

  LossDetectionType GetLossDetectionType() const override;

  // Switches the loss detection type to |loss_type| and resets loss algorithm
  // for all packet number spaces.
  void SetLossDetectionType(LossDetectionType loss_type);

  // Detects lost packets.
  void DetectLosses(const QuicUnackedPacketMap& unacked_packets,
                    QuicTime time,
                    const RttStats& rtt_stats,
                    QuicPacketNumber largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    LostPacketVector* packets_lost) override;

  // Returns the earliest time the early retransmit timer should be active.
  QuicTime GetLossTimeout() const override;

  // Increases the loss detection threshold for time loss detection.
  void SpuriousRetransmitDetected(
      const QuicUnackedPacketMap& unacked_packets,
      QuicTime time,
      const RttStats& rtt_stats,
      QuicPacketNumber spurious_retransmission) override;

  // Called to increases time or packet threshold.
  void SpuriousLossDetected(const QuicUnackedPacketMap& unacked_packets,
                            const RttStats& rtt_stats,
                            QuicTime ack_receive_time,
                            QuicPacketNumber packet_number,
                            QuicPacketNumber previous_largest_acked) override;

  // Sets reordering_shift for all packet number spaces.
  void SetReorderingShift(int reordering_shift);

  // Enable adaptive reordering threshold of all packet number spaces.
  void EnableAdaptiveReorderingThreshold();

 private:
  friend class test::QuicSentPacketManagerPeer;

  LossDetectionType loss_type_;
  // One loss algorithm per packet number space.
  GeneralLossAlgorithm general_loss_algorithms_[NUM_PACKET_NUMBER_SPACES];
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
