// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/uber_loss_algorithm.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_unacked_packet_map_peer.h"

namespace quic {
namespace test {
namespace {

// Default packet length.
const uint32_t kDefaultLength = 1000;

class UberLossAlgorithmTest : public QuicTest {
 protected:
  UberLossAlgorithmTest() {
    unacked_packets_ =
        std::make_unique<QuicUnackedPacketMap>(Perspective::IS_CLIENT);
    rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                         QuicTime::Delta::Zero(), clock_.Now());
    EXPECT_LT(0, rtt_stats_.smoothed_rtt().ToMicroseconds());
  }

  void SendPacket(uint64_t packet_number, EncryptionLevel encryption_level) {
    QuicStreamFrame frame;
    QuicTransportVersion version =
        CurrentSupportedVersions()[0].transport_version;
    frame.stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        version, Perspective::IS_CLIENT);
    if (encryption_level == ENCRYPTION_INITIAL) {
      if (QuicVersionUsesCryptoFrames(version)) {
        frame.stream_id = QuicUtils::GetFirstBidirectionalStreamId(
            version, Perspective::IS_CLIENT);
      } else {
        frame.stream_id = QuicUtils::GetCryptoStreamId(version);
      }
    }
    SerializedPacket packet(QuicPacketNumber(packet_number),
                            PACKET_1BYTE_PACKET_NUMBER, nullptr, kDefaultLength,
                            false, false);
    packet.encryption_level = encryption_level;
    packet.retransmittable_frames.push_back(QuicFrame(frame));
    unacked_packets_->AddSentPacket(&packet, NOT_RETRANSMISSION, clock_.Now(),
                                    true);
  }

  void AckPackets(const std::vector<uint64_t>& packets_acked) {
    packets_acked_.clear();
    for (uint64_t acked : packets_acked) {
      unacked_packets_->RemoveFromInFlight(QuicPacketNumber(acked));
      packets_acked_.push_back(AckedPacket(
          QuicPacketNumber(acked), kMaxOutgoingPacketSize, QuicTime::Zero()));
    }
  }

  void VerifyLosses(uint64_t largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    const std::vector<uint64_t>& losses_expected) {
    LostPacketVector lost_packets;
    loss_algorithm_.DetectLosses(*unacked_packets_, clock_.Now(), rtt_stats_,
                                 QuicPacketNumber(largest_newly_acked),
                                 packets_acked, &lost_packets);
    ASSERT_EQ(losses_expected.size(), lost_packets.size());
    for (size_t i = 0; i < losses_expected.size(); ++i) {
      EXPECT_EQ(lost_packets[i].packet_number,
                QuicPacketNumber(losses_expected[i]));
    }
  }

  MockClock clock_;
  std::unique_ptr<QuicUnackedPacketMap> unacked_packets_;
  RttStats rtt_stats_;
  UberLossAlgorithm loss_algorithm_;
  AckedPacketVector packets_acked_;
};

TEST_F(UberLossAlgorithmTest, ScenarioA) {
  // This test mimics a scenario: client sends 1-CHLO, 2-0RTT, 3-0RTT,
  // timeout and retransmits 4-CHLO. Server acks packet 1 (ack gets lost).
  // Server receives and buffers packets 2 and 3. Server receives packet 4 and
  // processes handshake asynchronously, so server acks 4 and cannot process
  // packets 2 and 3.
  SendPacket(1, ENCRYPTION_INITIAL);
  SendPacket(2, ENCRYPTION_ZERO_RTT);
  SendPacket(3, ENCRYPTION_ZERO_RTT);
  unacked_packets_->RemoveFromInFlight(QuicPacketNumber(1));
  SendPacket(4, ENCRYPTION_INITIAL);

  AckPackets({1, 4});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      HANDSHAKE_DATA, QuicPacketNumber(4));
  // Verify no packet is detected lost.
  VerifyLosses(4, packets_acked_, std::vector<uint64_t>{});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(UberLossAlgorithmTest, ScenarioB) {
  // This test mimics a scenario: client sends 3-0RTT, 4-0RTT, receives SHLO,
  // sends 5-1RTT, 6-1RTT.
  SendPacket(3, ENCRYPTION_ZERO_RTT);
  SendPacket(4, ENCRYPTION_ZERO_RTT);
  SendPacket(5, ENCRYPTION_FORWARD_SECURE);
  SendPacket(6, ENCRYPTION_FORWARD_SECURE);

  AckPackets({4});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(4));
  // No packet loss by acking 4.
  VerifyLosses(4, packets_acked_, std::vector<uint64_t>{});
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());

  // Acking 6 causes 3 to be detected loss.
  AckPackets({6});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(6));
  VerifyLosses(6, packets_acked_, std::vector<uint64_t>{3});
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
  packets_acked_.clear();

  clock_.AdvanceTime(1.25 * rtt_stats_.latest_rtt());
  // Verify 5 will be early retransmitted.
  VerifyLosses(6, packets_acked_, {5});
}

TEST_F(UberLossAlgorithmTest, ScenarioC) {
  // This test mimics a scenario: server sends 1-SHLO, 2-1RTT, 3-1RTT, 4-1RTT
  // and retransmit 4-SHLO. Client receives and buffers packet 4. Client
  // receives packet 5 and processes 4.
  QuicUnackedPacketMapPeer::SetPerspective(unacked_packets_.get(),
                                           Perspective::IS_SERVER);
  SendPacket(1, ENCRYPTION_ZERO_RTT);
  SendPacket(2, ENCRYPTION_FORWARD_SECURE);
  SendPacket(3, ENCRYPTION_FORWARD_SECURE);
  SendPacket(4, ENCRYPTION_FORWARD_SECURE);
  unacked_packets_->RemoveFromInFlight(QuicPacketNumber(1));
  SendPacket(5, ENCRYPTION_ZERO_RTT);

  AckPackets({4, 5});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(4));
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      HANDSHAKE_DATA, QuicPacketNumber(5));
  // No packet loss by acking 5.
  VerifyLosses(5, packets_acked_, std::vector<uint64_t>{});
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
  packets_acked_.clear();

  clock_.AdvanceTime(1.25 * rtt_stats_.latest_rtt());
  // Verify 2 and 3 will be early retransmitted.
  VerifyLosses(5, packets_acked_, std::vector<uint64_t>{2, 3});
}

// Regression test for b/133771183.
TEST_F(UberLossAlgorithmTest, PacketInLimbo) {
  // This test mimics a scenario: server sends 1-SHLO, 2-1RTT, 3-1RTT,
  // 4-retransmit SHLO. Client receives and ACKs packets 1, 3 and 4.
  QuicUnackedPacketMapPeer::SetPerspective(unacked_packets_.get(),
                                           Perspective::IS_SERVER);

  SendPacket(1, ENCRYPTION_ZERO_RTT);
  SendPacket(2, ENCRYPTION_FORWARD_SECURE);
  SendPacket(3, ENCRYPTION_FORWARD_SECURE);
  SendPacket(4, ENCRYPTION_ZERO_RTT);

  SendPacket(5, ENCRYPTION_FORWARD_SECURE);
  AckPackets({1, 3, 4});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(3));
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      HANDSHAKE_DATA, QuicPacketNumber(4));
  // No packet loss detected.
  VerifyLosses(4, packets_acked_, std::vector<uint64_t>{});

  SendPacket(6, ENCRYPTION_FORWARD_SECURE);
  AckPackets({5, 6});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(6));
  // Verify packet 2 is detected lost.
  VerifyLosses(6, packets_acked_, std::vector<uint64_t>{2});
}

}  // namespace
}  // namespace test
}  // namespace quic
