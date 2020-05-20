// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_

#include "net/third_party/quiche/src/quic/core/congestion_control/general_loss_algorithm.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"

namespace quic {

namespace test {

class QuicSentPacketManagerPeer;

}  // namespace test

struct QUIC_EXPORT_PRIVATE LossDetectionParameters {
  // See GeneralLossAlgorithm for the meaning of reordering_(shift|threshold).
  quiche::QuicheOptional<int> reordering_shift;
  quiche::QuicheOptional<QuicPacketCount> reordering_threshold;
};

class QUIC_EXPORT_PRIVATE LossDetectionTunerInterface {
 public:
  virtual ~LossDetectionTunerInterface() {}

  // Start the tuning by choosing parameters and saving them into |*params|.
  // Called near the start of a QUIC session, see the .cc file for exactly
  // where.
  virtual bool Start(LossDetectionParameters* params) = 0;

  // Finish tuning. The tuner is expected to use the actual loss detection
  // performance(for its definition of performance) to improve the parameter
  // selection for future QUIC sessions.
  // Called when a QUIC session closes.
  virtual void Finish(const LossDetectionParameters& params) = 0;
};

// This class comprises multiple loss algorithms, each per packet number space.
class QUIC_EXPORT_PRIVATE UberLossAlgorithm : public LossDetectionInterface {
 public:
  UberLossAlgorithm();
  UberLossAlgorithm(const UberLossAlgorithm&) = delete;
  UberLossAlgorithm& operator=(const UberLossAlgorithm&) = delete;
  ~UberLossAlgorithm() override {}

  // Detects lost packets.
  void DetectLosses(const QuicUnackedPacketMap& unacked_packets,
                    QuicTime time,
                    const RttStats& rtt_stats,
                    QuicPacketNumber largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    LostPacketVector* packets_lost) override;

  // Returns the earliest time the early retransmit timer should be active.
  QuicTime GetLossTimeout() const override;

  // Called to increases time or packet threshold.
  void SpuriousLossDetected(const QuicUnackedPacketMap& unacked_packets,
                            const RttStats& rtt_stats,
                            QuicTime ack_receive_time,
                            QuicPacketNumber packet_number,
                            QuicPacketNumber previous_largest_acked) override;

  void SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface> tuner);
  void OnConfigNegotiated() override;
  void OnMinRttAvailable() override;
  void OnConnectionClosed() override;

  // Sets reordering_shift for all packet number spaces.
  void SetReorderingShift(int reordering_shift);

  // Sets reordering_threshold for all packet number spaces.
  void SetReorderingThreshold(QuicPacketCount reordering_threshold);

  // Enable adaptive reordering threshold of all packet number spaces.
  void EnableAdaptiveReorderingThreshold();

  // Disable adaptive reordering threshold of all packet number spaces.
  void DisableAdaptiveReorderingThreshold();

  // Enable adaptive time threshold of all packet number spaces.
  void EnableAdaptiveTimeThreshold();

  // Disable packet threshold loss detection for *runt* packets.
  void DisablePacketThresholdForRuntPackets();

  // Called to reset loss detection of |space|.
  void ResetLossDetection(PacketNumberSpace space);

 private:
  friend class test::QuicSentPacketManagerPeer;

  void MaybeStartTuning();

  // One loss algorithm per packet number space.
  GeneralLossAlgorithm general_loss_algorithms_[NUM_PACKET_NUMBER_SPACES];

  // Used to tune reordering_shift and reordering_threshold.
  std::unique_ptr<LossDetectionTunerInterface> tuner_;
  LossDetectionParameters tuned_parameters_;
  bool tuner_started_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
