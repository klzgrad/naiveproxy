// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_

#include "quiche/quic/core/congestion_control/pacing_sender.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_sent_packet_manager.h"

namespace quic {

class SendAlgorithmInterface;

namespace test {

class QuicSentPacketManagerPeer {
 public:
  QuicSentPacketManagerPeer() = delete;


  static void SetPerspective(QuicSentPacketManager* sent_packet_manager,
                             Perspective perspective);

  static SendAlgorithmInterface* GetSendAlgorithm(
      const QuicSentPacketManager& sent_packet_manager);

  static void SetSendAlgorithm(QuicSentPacketManager* sent_packet_manager,
                               SendAlgorithmInterface* send_algorithm);

  static const LossDetectionInterface* GetLossAlgorithm(
      QuicSentPacketManager* sent_packet_manager);

  static void SetLossAlgorithm(QuicSentPacketManager* sent_packet_manager,
                               LossDetectionInterface* loss_detector);

  static RttStats* GetRttStats(QuicSentPacketManager* sent_packet_manager);

  // Returns true if |packet_number| is a retransmission of a packet.
  static bool IsRetransmission(QuicSentPacketManager* sent_packet_manager,
                               uint64_t packet_number);

  static void MarkForRetransmission(QuicSentPacketManager* sent_packet_manager,
                                    uint64_t packet_number,
                                    TransmissionType transmission_type);

  static size_t GetNumRetransmittablePackets(
      const QuicSentPacketManager* sent_packet_manager);

  static void SetConsecutivePtoCount(QuicSentPacketManager* sent_packet_manager,
                                     size_t count);

  static QuicSustainedBandwidthRecorder& GetBandwidthRecorder(
      QuicSentPacketManager* sent_packet_manager);

  static void SetUsingPacing(QuicSentPacketManager* sent_packet_manager,
                             bool using_pacing);

  static bool UsingPacing(const QuicSentPacketManager* sent_packet_manager);

  static PacingSender* GetPacingSender(
      QuicSentPacketManager* sent_packet_manager);

  static bool HasRetransmittableFrames(
      QuicSentPacketManager* sent_packet_manager, uint64_t packet_number);

  static QuicUnackedPacketMap* GetUnackedPacketMap(
      QuicSentPacketManager* sent_packet_manager);

  static void DisablePacerBursts(QuicSentPacketManager* sent_packet_manager);

  static int GetPacerInitialBurstSize(
      QuicSentPacketManager* sent_packet_manager);

  static void SetNextPacedPacketTime(QuicSentPacketManager* sent_packet_manager,
                                     QuicTime time);

  static int GetReorderingShift(QuicSentPacketManager* sent_packet_manager);

  static bool AdaptiveReorderingThresholdEnabled(
      QuicSentPacketManager* sent_packet_manager);

  static bool AdaptiveTimeThresholdEnabled(
      QuicSentPacketManager* sent_packet_manager);

  static bool UsePacketThresholdForRuntPackets(
      QuicSentPacketManager* sent_packet_manager);

  static int GetNumPtosForPathDegrading(
      QuicSentPacketManager* sent_packet_manager);

  static QuicEcnCounts* GetPeerEcnCounts(
      QuicSentPacketManager* sent_packet_manager, PacketNumberSpace space);

  static QuicPacketCount GetEct0Sent(QuicSentPacketManager* sent_packet_manager,
                                     PacketNumberSpace space);

  static QuicPacketCount GetEct1Sent(QuicSentPacketManager* sent_packet_manager,
                                     PacketNumberSpace space);

  static void SetEcnQueried(QuicSentPacketManager* sent_packet_manager,
                            bool ecn_queried);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_
