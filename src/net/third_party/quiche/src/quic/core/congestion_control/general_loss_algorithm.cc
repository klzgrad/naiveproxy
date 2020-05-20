// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/general_loss_algorithm.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

GeneralLossAlgorithm::GeneralLossAlgorithm()
    : loss_detection_timeout_(QuicTime::Zero()),
      reordering_shift_(kDefaultLossDelayShift),
      reordering_threshold_(kNumberOfNacksBeforeRetransmission),
      use_adaptive_reordering_threshold_(true),
      use_adaptive_time_threshold_(false),
      use_packet_threshold_for_runt_packets_(true),
      least_in_flight_(1),
      packet_number_space_(NUM_PACKET_NUMBER_SPACES) {}

// Uses nack counts to decide when packets are lost.
void GeneralLossAlgorithm::DetectLosses(
    const QuicUnackedPacketMap& unacked_packets,
    QuicTime time,
    const RttStats& rtt_stats,
    QuicPacketNumber largest_newly_acked,
    const AckedPacketVector& packets_acked,
    LostPacketVector* packets_lost) {
  loss_detection_timeout_ = QuicTime::Zero();
  if (!packets_acked.empty() &&
      packets_acked.front().packet_number == least_in_flight_) {
    if (packets_acked.back().packet_number == largest_newly_acked &&
        least_in_flight_ + packets_acked.size() - 1 == largest_newly_acked) {
      // Optimization for the case when no packet is missing. Please note,
      // packets_acked can include packets of different packet number space, so
      // do not use this optimization if largest_newly_acked is not the largest
      // packet in packets_acked.
      least_in_flight_ = largest_newly_acked + 1;
      return;
    }
    // There is hole in acked_packets, increment least_in_flight_ if possible.
    for (const auto& acked : packets_acked) {
      if (acked.packet_number != least_in_flight_) {
        break;
      }
      ++least_in_flight_;
    }
  }
  QuicTime::Delta max_rtt =
      std::max(rtt_stats.previous_srtt(), rtt_stats.latest_rtt());
  max_rtt = std::max(kAlarmGranularity, max_rtt);
  QuicTime::Delta loss_delay = max_rtt + (max_rtt >> reordering_shift_);
  QuicPacketNumber packet_number = unacked_packets.GetLeastUnacked();
  auto it = unacked_packets.begin();
  if (least_in_flight_.IsInitialized() && least_in_flight_ >= packet_number) {
    if (least_in_flight_ > unacked_packets.largest_sent_packet() + 1) {
      QUIC_BUG << "least_in_flight: " << least_in_flight_
               << " is greater than largest_sent_packet + 1: "
               << unacked_packets.largest_sent_packet() + 1;
    } else {
      it += (least_in_flight_ - packet_number);
      packet_number = least_in_flight_;
    }
  }
  // Clear least_in_flight_.
  least_in_flight_.Clear();
  DCHECK_EQ(packet_number_space_,
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
    // Packet threshold loss detection.
    // Skip packet threshold loss detection if largest_newly_acked is a runt.
    const bool skip_packet_threshold_detection =
        !use_packet_threshold_for_runt_packets_ &&
        it->bytes_sent >
            unacked_packets.GetTransmissionInfo(largest_newly_acked).bytes_sent;
    if (skip_packet_threshold_detection) {
      QUIC_RELOADABLE_FLAG_COUNT_N(
          quic_skip_packet_threshold_loss_detection_with_runt, 2, 2);
    }
    if (!skip_packet_threshold_detection &&
        largest_newly_acked - packet_number >= reordering_threshold_) {
      packets_lost->push_back(LostPacket(packet_number, it->bytes_sent));
      continue;
    }

    // Time threshold loss detection.
    QuicTime when_lost = it->sent_time + loss_delay;
    if (time < when_lost) {
      loss_detection_timeout_ = when_lost;
      if (!least_in_flight_.IsInitialized()) {
        // At this point, packet_number is in flight and not detected as lost.
        least_in_flight_ = packet_number;
      }
      break;
    }
    packets_lost->push_back(LostPacket(packet_number, it->bytes_sent));
  }
  if (!least_in_flight_.IsInitialized()) {
    // There is no in flight packet.
    least_in_flight_ = largest_newly_acked + 1;
  }
}

QuicTime GeneralLossAlgorithm::GetLossTimeout() const {
  return loss_detection_timeout_;
}

void GeneralLossAlgorithm::SpuriousLossDetected(
    const QuicUnackedPacketMap& unacked_packets,
    const RttStats& rtt_stats,
    QuicTime ack_receive_time,
    QuicPacketNumber packet_number,
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
    DCHECK_LT(packet_number, previous_largest_acked);
    // Increase reordering_threshold_ such that packet_number would not have
    // been declared lost.
    reordering_threshold_ = std::max(
        reordering_threshold_, previous_largest_acked - packet_number + 1);
  }
}

void GeneralLossAlgorithm::SetPacketNumberSpace(
    PacketNumberSpace packet_number_space) {
  if (packet_number_space_ < NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG << "Cannot switch packet_number_space";
    return;
  }

  packet_number_space_ = packet_number_space;
}

void GeneralLossAlgorithm::Reset() {
  loss_detection_timeout_ = QuicTime::Zero();
  least_in_flight_.Clear();
}

}  // namespace quic
