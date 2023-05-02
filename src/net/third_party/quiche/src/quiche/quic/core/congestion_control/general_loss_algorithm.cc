// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/congestion_control/general_loss_algorithm.h"

#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace {
float DetectionResponseTime(QuicTime::Delta rtt, QuicTime send_time,
                            QuicTime detection_time) {
  if (detection_time <= send_time || rtt.IsZero()) {
    // Time skewed, assume a very fast detection where |detection_time| is
    // |send_time| + |rtt|.
    return 1.0;
  }
  float send_to_detection_us = (detection_time - send_time).ToMicroseconds();
  return send_to_detection_us / rtt.ToMicroseconds();
}

QuicTime::Delta GetMaxRtt(const RttStats& rtt_stats) {
  return std::max(kAlarmGranularity,
                  std::max(rtt_stats.previous_srtt(), rtt_stats.latest_rtt()));
}

}  // namespace

// Uses nack counts to decide when packets are lost.
LossDetectionInterface::DetectionStats GeneralLossAlgorithm::DetectLosses(
    const QuicUnackedPacketMap& unacked_packets, QuicTime time,
    const RttStats& rtt_stats, QuicPacketNumber largest_newly_acked,
    const AckedPacketVector& packets_acked, LostPacketVector* packets_lost) {
  DetectionStats detection_stats;

  loss_detection_timeout_ = QuicTime::Zero();
  if (!packets_acked.empty() && least_in_flight_.IsInitialized() &&
      packets_acked.front().packet_number == least_in_flight_) {
    if (packets_acked.back().packet_number == largest_newly_acked &&
        least_in_flight_ + packets_acked.size() - 1 == largest_newly_acked) {
      // Optimization for the case when no packet is missing. Please note,
      // packets_acked can include packets of different packet number space, so
      // do not use this optimization if largest_newly_acked is not the largest
      // packet in packets_acked.
      least_in_flight_ = largest_newly_acked + 1;
      return detection_stats;
    }
    // There is hole in acked_packets, increment least_in_flight_ if possible.
    for (const auto& acked : packets_acked) {
      if (acked.packet_number != least_in_flight_) {
        break;
      }
      ++least_in_flight_;
    }
  }

  const QuicTime::Delta max_rtt = GetMaxRtt(rtt_stats);

  QuicPacketNumber packet_number = unacked_packets.GetLeastUnacked();
  auto it = unacked_packets.begin();
  if (least_in_flight_.IsInitialized() && least_in_flight_ >= packet_number) {
    if (least_in_flight_ > unacked_packets.largest_sent_packet() + 1) {
      QUIC_BUG(quic_bug_10430_1) << "least_in_flight: " << least_in_flight_
                                 << " is greater than largest_sent_packet + 1: "
                                 << unacked_packets.largest_sent_packet() + 1;
    } else {
      it += (least_in_flight_ - packet_number);
      packet_number = least_in_flight_;
    }
  }
  // Clear least_in_flight_.
  least_in_flight_.Clear();
  QUICHE_DCHECK_EQ(packet_number_space_,
                   unacked_packets.GetPacketNumberSpace(largest_newly_acked));
  for (; it != unacked_packets.end() && packet_number <= largest_newly_acked;
       ++it, ++packet_number) {
    if (unacked_packets.GetPacketNumberSpace(it->encryption_level) !=
        packet_number_space_) {
      // Skip packets of different packet number space.
      continue;
    }

    if (!it->in_flight) {
      continue;
    }

    if (parent_ != nullptr && largest_newly_acked != packet_number) {
      parent_->OnReorderingDetected();
    }

    if (largest_newly_acked - packet_number >
        detection_stats.sent_packets_max_sequence_reordering) {
      detection_stats.sent_packets_max_sequence_reordering =
          largest_newly_acked - packet_number;
    }

    // Packet threshold loss detection.
    // Skip packet threshold loss detection if largest_newly_acked is a runt.
    const bool skip_packet_threshold_detection =
        !use_packet_threshold_for_runt_packets_ &&
        it->bytes_sent >
            unacked_packets.GetTransmissionInfo(largest_newly_acked).bytes_sent;
    if (!skip_packet_threshold_detection &&
        largest_newly_acked - packet_number >= reordering_threshold_) {
      packets_lost->push_back(LostPacket(packet_number, it->bytes_sent));
      detection_stats.total_loss_detection_response_time +=
          DetectionResponseTime(max_rtt, it->sent_time, time);
      continue;
    }

    // Time threshold loss detection.
    const QuicTime::Delta loss_delay = max_rtt + (max_rtt >> reordering_shift_);
    QuicTime when_lost = it->sent_time + loss_delay;
    if (time < when_lost) {
      if (time >=
          it->sent_time + max_rtt + (max_rtt >> (reordering_shift_ + 1))) {
        ++detection_stats.sent_packets_num_borderline_time_reorderings;
      }
      loss_detection_timeout_ = when_lost;
      if (!least_in_flight_.IsInitialized()) {
        // At this point, packet_number is in flight and not detected as lost.
        least_in_flight_ = packet_number;
      }
      break;
    }
    packets_lost->push_back(LostPacket(packet_number, it->bytes_sent));
    detection_stats.total_loss_detection_response_time +=
        DetectionResponseTime(max_rtt, it->sent_time, time);
  }
  if (!least_in_flight_.IsInitialized()) {
    // There is no in flight packet.
    least_in_flight_ = largest_newly_acked + 1;
  }

  return detection_stats;
}

QuicTime GeneralLossAlgorithm::GetLossTimeout() const {
  return loss_detection_timeout_;
}

void GeneralLossAlgorithm::SpuriousLossDetected(
    const QuicUnackedPacketMap& unacked_packets, const RttStats& rtt_stats,
    QuicTime ack_receive_time, QuicPacketNumber packet_number,
    QuicPacketNumber previous_largest_acked) {
  if (use_adaptive_time_threshold_ && reordering_shift_ > 0) {
    // Increase reordering fraction such that the packet would not have been
    // declared lost.
    QuicTime::Delta time_needed =
        ack_receive_time -
        unacked_packets.GetTransmissionInfo(packet_number).sent_time;
    QuicTime::Delta max_rtt =
        std::max(rtt_stats.previous_srtt(), rtt_stats.latest_rtt());
    while (max_rtt + (max_rtt >> reordering_shift_) < time_needed &&
           reordering_shift_ > 0) {
      --reordering_shift_;
    }
  }

  if (use_adaptive_reordering_threshold_) {
    QUICHE_DCHECK_LT(packet_number, previous_largest_acked);
    // Increase reordering_threshold_ such that packet_number would not have
    // been declared lost.
    reordering_threshold_ = std::max(
        reordering_threshold_, previous_largest_acked - packet_number + 1);
  }
}

void GeneralLossAlgorithm::Initialize(PacketNumberSpace packet_number_space,
                                      LossDetectionInterface* parent) {
  parent_ = parent;
  if (packet_number_space_ < NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG(quic_bug_10430_2) << "Cannot switch packet_number_space";
    return;
  }

  packet_number_space_ = packet_number_space;
}

void GeneralLossAlgorithm::Reset() {
  loss_detection_timeout_ = QuicTime::Zero();
  least_in_flight_.Clear();
}

}  // namespace quic
