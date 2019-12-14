// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/loss_detection_interface.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_sent_packet_manager.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_unacked_packet_map_peer.h"

namespace quic {
namespace test {

// static
size_t QuicSentPacketManagerPeer::GetMaxTailLossProbes(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->max_tail_loss_probes_;
}

// static
void QuicSentPacketManagerPeer::SetMaxTailLossProbes(
    QuicSentPacketManager* sent_packet_manager,
    size_t max_tail_loss_probes) {
  sent_packet_manager->max_tail_loss_probes_ = max_tail_loss_probes;
}

// static
bool QuicSentPacketManagerPeer::GetEnableHalfRttTailLossProbe(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->enable_half_rtt_tail_loss_probe_;
}

// static
bool QuicSentPacketManagerPeer::GetUseNewRto(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->use_new_rto_;
}

// static
void QuicSentPacketManagerPeer::SetPerspective(
    QuicSentPacketManager* sent_packet_manager,
    Perspective perspective) {
  QuicUnackedPacketMapPeer::SetPerspective(
      &sent_packet_manager->unacked_packets_, perspective);
}

// static
SendAlgorithmInterface* QuicSentPacketManagerPeer::GetSendAlgorithm(
    const QuicSentPacketManager& sent_packet_manager) {
  return sent_packet_manager.send_algorithm_.get();
}

// static
void QuicSentPacketManagerPeer::SetSendAlgorithm(
    QuicSentPacketManager* sent_packet_manager,
    SendAlgorithmInterface* send_algorithm) {
  sent_packet_manager->SetSendAlgorithm(send_algorithm);
}

// static
const LossDetectionInterface* QuicSentPacketManagerPeer::GetLossAlgorithm(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->loss_algorithm_;
}

// static
void QuicSentPacketManagerPeer::SetLossAlgorithm(
    QuicSentPacketManager* sent_packet_manager,
    LossDetectionInterface* loss_detector) {
  sent_packet_manager->loss_algorithm_ = loss_detector;
}

// static
RttStats* QuicSentPacketManagerPeer::GetRttStats(
    QuicSentPacketManager* sent_packet_manager) {
  return &sent_packet_manager->rtt_stats_;
}

// static
bool QuicSentPacketManagerPeer::IsRetransmission(
    QuicSentPacketManager* sent_packet_manager,
    uint64_t packet_number) {
  DCHECK(HasRetransmittableFrames(sent_packet_manager, packet_number));
  if (!HasRetransmittableFrames(sent_packet_manager, packet_number)) {
    return false;
  }
  if (sent_packet_manager->session_decides_what_to_write()) {
    return sent_packet_manager->unacked_packets_
               .GetTransmissionInfo(QuicPacketNumber(packet_number))
               .transmission_type != NOT_RETRANSMISSION;
  }
  for (auto transmission_info : sent_packet_manager->unacked_packets_) {
    if (transmission_info.retransmission.IsInitialized() &&
        transmission_info.retransmission == QuicPacketNumber(packet_number)) {
      return true;
    }
  }
  return false;
}

// static
void QuicSentPacketManagerPeer::MarkForRetransmission(
    QuicSentPacketManager* sent_packet_manager,
    uint64_t packet_number,
    TransmissionType transmission_type) {
  sent_packet_manager->MarkForRetransmission(QuicPacketNumber(packet_number),
                                             transmission_type);
}

// static
QuicTime::Delta QuicSentPacketManagerPeer::GetRetransmissionDelay(
    const QuicSentPacketManager* sent_packet_manager,
    size_t consecutive_rto_count) {
  return sent_packet_manager->GetRetransmissionDelay(consecutive_rto_count);
}

// static
QuicTime::Delta QuicSentPacketManagerPeer::GetRetransmissionDelay(
    const QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->GetRetransmissionDelay();
}

// static
QuicTime::Delta QuicSentPacketManagerPeer::GetTailLossProbeDelay(
    const QuicSentPacketManager* sent_packet_manager,
    size_t consecutive_tlp_count) {
  return sent_packet_manager->GetTailLossProbeDelay(consecutive_tlp_count);
}

// static
QuicTime::Delta QuicSentPacketManagerPeer::GetTailLossProbeDelay(
    const QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->GetTailLossProbeDelay();
}

// static
size_t QuicSentPacketManagerPeer::GetNumRetransmittablePackets(
    const QuicSentPacketManager* sent_packet_manager) {
  size_t num_unacked_packets = 0;
  for (auto it = sent_packet_manager->unacked_packets_.begin();
       it != sent_packet_manager->unacked_packets_.end(); ++it) {
    if (sent_packet_manager->unacked_packets_.HasRetransmittableFrames(*it)) {
      ++num_unacked_packets;
    }
  }
  return num_unacked_packets;
}

// static
void QuicSentPacketManagerPeer::SetConsecutiveRtoCount(
    QuicSentPacketManager* sent_packet_manager,
    size_t count) {
  sent_packet_manager->consecutive_rto_count_ = count;
}

// static
void QuicSentPacketManagerPeer::SetConsecutiveTlpCount(
    QuicSentPacketManager* sent_packet_manager,
    size_t count) {
  sent_packet_manager->consecutive_tlp_count_ = count;
}

// static
QuicSustainedBandwidthRecorder& QuicSentPacketManagerPeer::GetBandwidthRecorder(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->sustained_bandwidth_recorder_;
}

// static
bool QuicSentPacketManagerPeer::UsingPacing(
    const QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->using_pacing_;
}

// static
void QuicSentPacketManagerPeer::SetUsingPacing(
    QuicSentPacketManager* sent_packet_manager,
    bool using_pacing) {
  sent_packet_manager->using_pacing_ = using_pacing;
}

// static
bool QuicSentPacketManagerPeer::HasRetransmittableFrames(
    QuicSentPacketManager* sent_packet_manager,
    uint64_t packet_number) {
  return sent_packet_manager->unacked_packets_.HasRetransmittableFrames(
      QuicPacketNumber(packet_number));
}

// static
QuicUnackedPacketMap* QuicSentPacketManagerPeer::GetUnackedPacketMap(
    QuicSentPacketManager* sent_packet_manager) {
  return &sent_packet_manager->unacked_packets_;
}

// static
void QuicSentPacketManagerPeer::DisablePacerBursts(
    QuicSentPacketManager* sent_packet_manager) {
  sent_packet_manager->pacing_sender_.burst_tokens_ = 0;
  sent_packet_manager->pacing_sender_.initial_burst_size_ = 0;
}

// static
void QuicSentPacketManagerPeer::SetNextPacedPacketTime(
    QuicSentPacketManager* sent_packet_manager,
    QuicTime time) {
  sent_packet_manager->pacing_sender_.ideal_next_packet_send_time_ = time;
}

// static
int QuicSentPacketManagerPeer::GetReorderingShift(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->uber_loss_algorithm_.general_loss_algorithms_[0]
      .reordering_shift();
}

// static
bool QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(
    QuicSentPacketManager* sent_packet_manager) {
  return sent_packet_manager->uber_loss_algorithm_.general_loss_algorithms_[0]
      .use_adaptive_reordering_threshold();
}

}  // namespace test
}  // namespace quic
