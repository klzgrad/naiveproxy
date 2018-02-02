// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_sent_packet_manager.h"

namespace net {

class SendAlgorithmInterface;

namespace test {

class QuicSentPacketManagerPeer {
 public:
  static size_t GetMaxTailLossProbes(
      QuicSentPacketManager* sent_packet_manager);

  static void SetMaxTailLossProbes(QuicSentPacketManager* sent_packet_manager,
                                   size_t max_tail_loss_probes);

  static bool GetEnableHalfRttTailLossProbe(
      QuicSentPacketManager* sent_packet_manager);

  static bool GetUseNewRto(QuicSentPacketManager* sent_packet_manager);

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

  static bool HasPendingPackets(
      const QuicSentPacketManager* sent_packet_manager);

  // Returns true if |packet_number| is a retransmission of a packet.
  static bool IsRetransmission(QuicSentPacketManager* sent_packet_manager,
                               QuicPacketNumber packet_number);

  static void MarkForRetransmission(QuicSentPacketManager* sent_packet_manager,
                                    QuicPacketNumber packet_number,
                                    TransmissionType transmission_type);

  static QuicTime::Delta GetRetransmissionDelay(
      const QuicSentPacketManager* sent_packet_manager);

  static bool HasUnackedCryptoPackets(
      const QuicSentPacketManager* sent_packet_manager);

  static size_t GetNumRetransmittablePackets(
      const QuicSentPacketManager* sent_packet_manager);

  static QuicByteCount GetBytesInFlight(
      const QuicSentPacketManager* sent_packet_manager);

  static void SetConsecutiveRtoCount(QuicSentPacketManager* sent_packet_manager,
                                     size_t count);

  static void SetConsecutiveTlpCount(QuicSentPacketManager* sent_packet_manager,
                                     size_t count);

  static QuicSustainedBandwidthRecorder& GetBandwidthRecorder(
      QuicSentPacketManager* sent_packet_manager);

  static void SetUsingPacing(QuicSentPacketManager* sent_packet_manager,
                             bool using_pacing);

  static bool UsingPacing(const QuicSentPacketManager* sent_packet_manager);

  static bool IsUnacked(QuicSentPacketManager* sent_packet_manager,
                        QuicPacketNumber packet_number);

  static bool HasRetransmittableFrames(
      QuicSentPacketManager* sent_packet_manager,
      QuicPacketNumber packet_number);

  static QuicUnackedPacketMap* GetUnackedPacketMap(
      QuicSentPacketManager* sent_packet_manager);

  static void DisablePacerBursts(QuicSentPacketManager* sent_packet_manager);

  static void SetNextPacedPacketTime(QuicSentPacketManager* sent_packet_manager,
                                     QuicTime time);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicSentPacketManagerPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_SENT_PACKET_MANAGER_PEER_H_
