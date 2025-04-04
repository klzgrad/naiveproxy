// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_

#include <algorithm>
#include <map>

#include "quiche/quic/core/congestion_control/loss_detection_interface.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_unacked_packet_map.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Class which can be configured to implement's TCP's approach of detecting loss
// when 3 nacks have been received for a packet or with a time threshold.
// Also implements TCP's early retransmit(RFC5827).
class QUICHE_EXPORT GeneralLossAlgorithm : public LossDetectionInterface {
 public:
  GeneralLossAlgorithm() = default;
  GeneralLossAlgorithm(const GeneralLossAlgorithm&) = delete;
  GeneralLossAlgorithm& operator=(const GeneralLossAlgorithm&) = delete;
  ~GeneralLossAlgorithm() override {}

  void SetFromConfig(const QuicConfig& /*config*/,
                     Perspective /*perspective*/) override {}

  // Uses |largest_acked| and time to decide when packets are lost.
  DetectionStats DetectLosses(const QuicUnackedPacketMap& unacked_packets,
                              QuicTime time, const RttStats& rtt_stats,
                              QuicPacketNumber largest_newly_acked,
                              const AckedPacketVector& packets_acked,
                              LostPacketVector* packets_lost) override;

  // Returns a non-zero value when the early retransmit timer is active.
  QuicTime GetLossTimeout() const override;

  // Called to increases time and/or packet threshold.
  void SpuriousLossDetected(const QuicUnackedPacketMap& unacked_packets,
                            const RttStats& rtt_stats,
                            QuicTime ack_receive_time,
                            QuicPacketNumber packet_number,
                            QuicPacketNumber previous_largest_acked) override;

  void OnConfigNegotiated() override {
    QUICHE_DCHECK(false)
        << "Unexpected call to GeneralLossAlgorithm::OnConfigNegotiated";
  }

  void OnMinRttAvailable() override {
    QUICHE_DCHECK(false)
        << "Unexpected call to GeneralLossAlgorithm::OnMinRttAvailable";
  }

  void OnUserAgentIdKnown() override {
    QUICHE_DCHECK(false)
        << "Unexpected call to GeneralLossAlgorithm::OnUserAgentIdKnown";
  }

  void OnConnectionClosed() override {
    QUICHE_DCHECK(false)
        << "Unexpected call to GeneralLossAlgorithm::OnConnectionClosed";
  }

  void OnReorderingDetected() override {
    QUICHE_DCHECK(false)
        << "Unexpected call to GeneralLossAlgorithm::OnReorderingDetected";
  }

  void Initialize(PacketNumberSpace packet_number_space,
                  LossDetectionInterface* parent);

  void Reset();

  QuicPacketCount reordering_threshold() const { return reordering_threshold_; }

  int reordering_shift() const { return reordering_shift_; }

  void set_reordering_shift(int reordering_shift) {
    reordering_shift_ = reordering_shift;
  }

  void set_reordering_threshold(QuicPacketCount reordering_threshold) {
    reordering_threshold_ = reordering_threshold;
  }

  bool use_adaptive_reordering_threshold() const {
    return use_adaptive_reordering_threshold_;
  }

  void set_use_adaptive_reordering_threshold(bool value) {
    use_adaptive_reordering_threshold_ = value;
  }

  bool use_adaptive_time_threshold() const {
    return use_adaptive_time_threshold_;
  }

  void enable_adaptive_time_threshold() { use_adaptive_time_threshold_ = true; }

  bool use_packet_threshold_for_runt_packets() const {
    return use_packet_threshold_for_runt_packets_;
  }

  void disable_packet_threshold_for_runt_packets() {
    use_packet_threshold_for_runt_packets_ = false;
  }

 private:
  LossDetectionInterface* parent_ = nullptr;
  QuicTime loss_detection_timeout_ = QuicTime::Zero();
  // Fraction of a max(SRTT, latest_rtt) to permit reordering before declaring
  // loss.  Fraction calculated by shifting max(SRTT, latest_rtt) to the right
  // by reordering_shift.
  int reordering_shift_ = kDefaultLossDelayShift;
  // Reordering threshold for loss detection.
  QuicPacketCount reordering_threshold_ = kDefaultPacketReorderingThreshold;
  // If true, uses adaptive reordering threshold for loss detection.
  bool use_adaptive_reordering_threshold_ = true;
  // If true, uses adaptive time threshold for time based loss detection.
  bool use_adaptive_time_threshold_ = false;
  // If true, uses packet threshold when largest acked is a runt packet.
  bool use_packet_threshold_for_runt_packets_ = true;
  // The least in flight packet. Loss detection should start from this. Please
  // note, least_in_flight_ could be largest packet ever sent + 1.
  QuicPacketNumber least_in_flight_{1};
  PacketNumberSpace packet_number_space_ = NUM_PACKET_NUMBER_SPACES;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
