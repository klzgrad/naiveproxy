// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_

#include <optional>

#include "quiche/quic/core/congestion_control/general_loss_algorithm.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace test {

class QuicSentPacketManagerPeer;

}  // namespace test

struct QUICHE_EXPORT LossDetectionParameters {
  // See GeneralLossAlgorithm for the meaning of reordering_(shift|threshold).
  std::optional<int> reordering_shift;
  std::optional<QuicPacketCount> reordering_threshold;
};

class QUICHE_EXPORT LossDetectionTunerInterface {
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
class QUICHE_EXPORT UberLossAlgorithm : public LossDetectionInterface {
 public:
  UberLossAlgorithm();
  UberLossAlgorithm(const UberLossAlgorithm&) = delete;
  UberLossAlgorithm& operator=(const UberLossAlgorithm&) = delete;
  ~UberLossAlgorithm() override {}

  void SetFromConfig(const QuicConfig& config,
                     Perspective perspective) override;

  // Detects lost packets.
  DetectionStats DetectLosses(const QuicUnackedPacketMap& unacked_packets,
                              QuicTime time, const RttStats& rtt_stats,
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
  void OnUserAgentIdKnown() override;
  void OnConnectionClosed() override;
  void OnReorderingDetected() override;

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

  // Get the packet reordering threshold from the APPLICATION_DATA PN space.
  // Always 3 when adaptive reordering is not enabled.
  QuicPacketCount GetPacketReorderingThreshold() const;

  // Get the packet reordering shift from the APPLICATION_DATA PN space.
  int GetPacketReorderingShift() const;

  // Disable packet threshold loss detection for *runt* packets.
  void DisablePacketThresholdForRuntPackets();

  // Called to reset loss detection of |space|.
  void ResetLossDetection(PacketNumberSpace space);

  bool use_adaptive_reordering_threshold() const {
    return general_loss_algorithms_[APPLICATION_DATA]
        .use_adaptive_reordering_threshold();
  }

  bool use_adaptive_time_threshold() const {
    return general_loss_algorithms_[APPLICATION_DATA]
        .use_adaptive_time_threshold();
  }

 private:
  friend class test::QuicSentPacketManagerPeer;

  void MaybeStartTuning();

  // One loss algorithm per packet number space.
  GeneralLossAlgorithm general_loss_algorithms_[NUM_PACKET_NUMBER_SPACES];

  // Used to tune reordering_shift and reordering_threshold.
  std::unique_ptr<LossDetectionTunerInterface> tuner_;
  LossDetectionParameters tuned_parameters_;
  bool tuner_started_ = false;
  bool min_rtt_available_ = false;
  // Whether user agent is known to the session.
  bool user_agent_known_ = false;
  // Whether tuning is configured in QuicConfig.
  bool tuning_configured_ = false;
  bool reorder_happened_ = false;  // Whether any reordered packet is observed.
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_UBER_LOSS_ALGORITHM_H_
