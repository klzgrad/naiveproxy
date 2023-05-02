// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The pure virtual class for send side loss detection algorithm.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_LOSS_DETECTION_INTERFACE_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_LOSS_DETECTION_INTERFACE_H_

#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicUnackedPacketMap;
class RttStats;

class QUIC_EXPORT_PRIVATE LossDetectionInterface {
 public:
  virtual ~LossDetectionInterface() {}

  virtual void SetFromConfig(const QuicConfig& config,
                             Perspective perspective) = 0;

  struct QUIC_NO_EXPORT DetectionStats {
    // Maximum sequence reordering observed in newly acked packets.
    QuicPacketCount sent_packets_max_sequence_reordering = 0;
    QuicPacketCount sent_packets_num_borderline_time_reorderings = 0;
    // Total detection response time for lost packets from this detection.
    // See QuicConnectionStats for the definition of detection response time.
    float total_loss_detection_response_time = 0.0;
  };

  // Called when a new ack arrives or the loss alarm fires.
  virtual DetectionStats DetectLosses(
      const QuicUnackedPacketMap& unacked_packets, QuicTime time,
      const RttStats& rtt_stats, QuicPacketNumber largest_newly_acked,
      const AckedPacketVector& packets_acked,
      LostPacketVector* packets_lost) = 0;

  // Get the time the LossDetectionAlgorithm wants to re-evaluate losses.
  // Returns QuicTime::Zero if no alarm needs to be set.
  virtual QuicTime GetLossTimeout() const = 0;

  // Called when |packet_number| was detected lost but gets acked later.
  virtual void SpuriousLossDetected(
      const QuicUnackedPacketMap& unacked_packets, const RttStats& rtt_stats,
      QuicTime ack_receive_time, QuicPacketNumber packet_number,
      QuicPacketNumber previous_largest_acked) = 0;

  virtual void OnConfigNegotiated() = 0;

  virtual void OnMinRttAvailable() = 0;

  virtual void OnUserAgentIdKnown() = 0;

  virtual void OnConnectionClosed() = 0;

  // Called when a reordering is detected by the loss algorithm, but _before_
  // the reordering_shift and reordering_threshold are consulted to see whether
  // it is a loss.
  virtual void OnReorderingDetected() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_LOSS_DETECTION_INTERFACE_H_
