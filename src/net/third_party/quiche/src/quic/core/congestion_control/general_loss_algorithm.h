// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_

#include <algorithm>
#include <map>

#include "net/third_party/quiche/src/quic/core/congestion_control/loss_detection_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_unacked_packet_map.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Class which can be configured to implement's TCP's approach of detecting loss
// when 3 nacks have been received for a packet or with a time threshold.
// Also implements TCP's early retransmit(RFC5827).
class QUIC_EXPORT_PRIVATE GeneralLossAlgorithm : public LossDetectionInterface {
 public:
  // TCP retransmits after 3 nacks.
  static const QuicPacketCount kNumberOfNacksBeforeRetransmission = 3;

  GeneralLossAlgorithm();
  explicit GeneralLossAlgorithm(LossDetectionType loss_type);
  GeneralLossAlgorithm(const GeneralLossAlgorithm&) = delete;
  GeneralLossAlgorithm& operator=(const GeneralLossAlgorithm&) = delete;
  ~GeneralLossAlgorithm() override {}

  LossDetectionType GetLossDetectionType() const override;

  // Switches the loss detection type to |loss_type| and resets the loss
  // algorithm.
  void SetLossDetectionType(LossDetectionType loss_type);

  // Uses |largest_acked| and time to decide when packets are lost.
  void DetectLosses(const QuicUnackedPacketMap& unacked_packets,
                    QuicTime time,
                    const RttStats& rtt_stats,
                    QuicPacketNumber largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    LostPacketVector* packets_lost) override;

  // Returns a non-zero value when the early retransmit timer is active.
  QuicTime GetLossTimeout() const override;

  // Increases the loss detection threshold for time loss detection.
  void SpuriousRetransmitDetected(
      const QuicUnackedPacketMap& unacked_packets,
      QuicTime time,
      const RttStats& rtt_stats,
      QuicPacketNumber spurious_retransmission) override;

  // Called to increases time and/or packet threshold.
  void SpuriousLossDetected(const QuicUnackedPacketMap& unacked_packets,
                            const RttStats& rtt_stats,
                            QuicTime ack_receive_time,
                            QuicPacketNumber packet_number,
                            QuicPacketNumber previous_largest_acked) override;

  void SetPacketNumberSpace(PacketNumberSpace packet_number_space);

  int reordering_shift() const { return reordering_shift_; }

  void set_reordering_shift(int reordering_shift) {
    reordering_shift_ = reordering_shift;
  }

  bool use_adaptive_reordering_threshold() const {
    return use_adaptive_reordering_threshold_;
  }

  void enable_adaptive_reordering_threshold() {
    use_adaptive_reordering_threshold_ = true;
  }

 private:
  QuicTime loss_detection_timeout_;
  // Largest sent packet when a spurious retransmit is detected.
  // Prevents increasing the reordering threshold multiple times per epoch.
  QuicPacketNumber largest_sent_on_spurious_retransmit_;
  LossDetectionType loss_type_;
  // Fraction of a max(SRTT, latest_rtt) to permit reordering before declaring
  // loss.  Fraction calculated by shifting max(SRTT, latest_rtt) to the right
  // by reordering_shift.
  int reordering_shift_;
  // Reordering threshold for loss detection.
  QuicPacketCount reordering_threshold_;
  // If true, uses adaptive reordering threshold for loss detection.
  bool use_adaptive_reordering_threshold_;
  // The largest newly acked from the previous call to DetectLosses.
  QuicPacketNumber largest_previously_acked_;
  // The least in flight packet. Loss detection should start from this. Please
  // note, least_in_flight_ could be largest packet ever sent + 1.
  QuicPacketNumber least_in_flight_;
  PacketNumberSpace packet_number_space_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
