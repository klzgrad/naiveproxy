// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_

#include <algorithm>
#include <map>

#include "base/macros.h"
#include "net/quic/core/congestion_control/loss_detection_interface.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/quic_unacked_packet_map.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Class which can be configured to implement's TCP's approach of detecting loss
// when 3 nacks have been received for a packet or with a time threshold.
// Also implements TCP's early retransmit(RFC5827).
class QUIC_EXPORT_PRIVATE GeneralLossAlgorithm : public LossDetectionInterface {
 public:
  // TCP retransmits after 3 nacks.
  static const QuicPacketCount kNumberOfNacksBeforeRetransmission = 3;

  GeneralLossAlgorithm();
  explicit GeneralLossAlgorithm(LossDetectionType loss_type);
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
                    LostPacketVector* packets_lost) override;

  // Returns a non-zero value when the early retransmit timer is active.
  QuicTime GetLossTimeout() const override;

  // Increases the loss detection threshold for time loss detection.
  void SpuriousRetransmitDetected(
      const QuicUnackedPacketMap& unacked_packets,
      QuicTime time,
      const RttStats& rtt_stats,
      QuicPacketNumber spurious_retransmission) override;

  int reordering_shift() const { return reordering_shift_; }

 private:
  QuicTime loss_detection_timeout_;
  // Largest sent packet when a spurious retransmit is detected.
  // Prevents increasing the reordering threshold multiple times per epoch.
  // TODO(ianswett): Deprecate when
  // quic_reloadable_flag_quic_fix_adaptive_time_loss is deprecated.
  QuicPacketNumber largest_sent_on_spurious_retransmit_;
  LossDetectionType loss_type_;
  // Fraction of a max(SRTT, latest_rtt) to permit reordering before declaring
  // loss.  Fraction calculated by shifting max(SRTT, latest_rtt) to the right
  // by reordering_shift.
  int reordering_shift_;
  // The largest newly acked from the previous call to DetectLosses.
  QuicPacketNumber largest_previously_acked_;

  DISALLOW_COPY_AND_ASSIGN(GeneralLossAlgorithm);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_GENERAL_LOSS_ALGORITHM_H_
