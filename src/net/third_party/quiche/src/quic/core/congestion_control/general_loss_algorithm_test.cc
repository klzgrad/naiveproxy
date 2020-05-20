// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/general_loss_algorithm.h"

#include <algorithm>
#include <cstdint>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_unacked_packet_map.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {
namespace {

// Default packet length.
const uint32_t kDefaultLength = 1000;

class GeneralLossAlgorithmTest : public QuicTest {
 protected:
  GeneralLossAlgorithmTest() : unacked_packets_(Perspective::IS_CLIENT) {
    rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                         QuicTime::Delta::Zero(), clock_.Now());
    EXPECT_LT(0, rtt_stats_.smoothed_rtt().ToMicroseconds());
    loss_algorithm_.SetPacketNumberSpace(HANDSHAKE_DATA);
  }

  ~GeneralLossAlgorithmTest() override {}

  void SendDataPacket(uint64_t packet_number,
                      QuicPacketLength encrypted_length) {
    QuicStreamFrame frame;
    frame.stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        CurrentSupportedVersions()[0].transport_version,
        Perspective::IS_CLIENT);
    SerializedPacket packet(QuicPacketNumber(packet_number),
                            PACKET_1BYTE_PACKET_NUMBER, nullptr,
                            encrypted_length, false, false);
    packet.retransmittable_frames.push_back(QuicFrame(frame));
    unacked_packets_.AddSentPacket(&packet, NOT_RETRANSMISSION, clock_.Now(),
                                   true);
  }

  void SendDataPacket(uint64_t packet_number) {
    SendDataPacket(packet_number, kDefaultLength);
  }

  void SendAckPacket(uint64_t packet_number) {
    SerializedPacket packet(QuicPacketNumber(packet_number),
                            PACKET_1BYTE_PACKET_NUMBER, nullptr, kDefaultLength,
                            true, false);
    unacked_packets_.AddSentPacket(&packet, NOT_RETRANSMISSION, clock_.Now(),
                                   false);
  }

  void VerifyLosses(uint64_t largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    const std::vector<uint64_t>& losses_expected) {
    unacked_packets_.MaybeUpdateLargestAckedOfPacketNumberSpace(
        APPLICATION_DATA, QuicPacketNumber(largest_newly_acked));
    LostPacketVector lost_packets;
    loss_algorithm_.DetectLosses(unacked_packets_, clock_.Now(), rtt_stats_,
                                 QuicPacketNumber(largest_newly_acked),
                                 packets_acked, &lost_packets);
    ASSERT_EQ(losses_expected.size(), lost_packets.size());
    for (size_t i = 0; i < losses_expected.size(); ++i) {
      EXPECT_EQ(lost_packets[i].packet_number,
                QuicPacketNumber(losses_expected[i]));
    }
  }

  QuicUnackedPacketMap unacked_packets_;
  GeneralLossAlgorithm loss_algorithm_;
  RttStats rtt_stats_;
  MockClock clock_;
};

TEST_F(GeneralLossAlgorithmTest, NackRetransmit1Packet) {
  const size_t kNumSentPackets = 5;
  // Transmit 5 packets.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  AckedPacketVector packets_acked;
  // No loss on one ack.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  // No loss on two acks.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(3));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(3), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(3, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  // Loss on three acks.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(4));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(4), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(4, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

// A stretch ack is an ack that covers more than 1 packet of previously
// unacknowledged data.
TEST_F(GeneralLossAlgorithmTest, NackRetransmit1PacketWith1StretchAck) {
  const size_t kNumSentPackets = 10;
  // Transmit 10 packets.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  AckedPacketVector packets_acked;
  // Nack the first packet 3 times in a single StretchAck.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(3));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(3), kMaxOutgoingPacketSize, QuicTime::Zero()));
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(4));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(4), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(4, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

// Ack a packet 3 packets ahead, causing a retransmit.
TEST_F(GeneralLossAlgorithmTest, NackRetransmit1PacketSingleAck) {
  const size_t kNumSentPackets = 10;
  // Transmit 10 packets.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  AckedPacketVector packets_acked;
  // Nack the first packet 3 times in an AckFrame with three missing packets.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(4));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(4), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(4, packets_acked, {1});
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, EarlyRetransmit1Packet) {
  const size_t kNumSentPackets = 2;
  // Transmit 2 packets.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  AckedPacketVector packets_acked;
  // Early retransmit when the final packet gets acked and the first is nacked.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());

  clock_.AdvanceTime(1.25 * rtt_stats_.latest_rtt());
  VerifyLosses(2, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, EarlyRetransmitAllPackets) {
  const size_t kNumSentPackets = 5;
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
    // Advance the time 1/4 RTT between 3 and 4.
    if (i == 3) {
      clock_.AdvanceTime(0.25 * rtt_stats_.smoothed_rtt());
    }
  }
  AckedPacketVector packets_acked;
  // Early retransmit when the final packet gets acked and 1.25 RTTs have
  // elapsed since the packets were sent.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(kNumSentPackets));
  packets_acked.push_back(AckedPacket(QuicPacketNumber(kNumSentPackets),
                                      kMaxOutgoingPacketSize,
                                      QuicTime::Zero()));
  // This simulates a single ack following multiple missing packets with FACK.
  VerifyLosses(kNumSentPackets, packets_acked, {1, 2});
  packets_acked.clear();
  // The time has already advanced 1/4 an RTT, so ensure the timeout is set
  // 1.25 RTTs after the earliest pending packet(3), not the last(4).
  EXPECT_EQ(clock_.Now() + rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());

  clock_.AdvanceTime(rtt_stats_.smoothed_rtt());
  VerifyLosses(kNumSentPackets, packets_acked, {3});
  EXPECT_EQ(clock_.Now() + 0.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
  clock_.AdvanceTime(0.25 * rtt_stats_.smoothed_rtt());
  VerifyLosses(kNumSentPackets, packets_acked, {4});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, DontEarlyRetransmitNeuteredPacket) {
  const size_t kNumSentPackets = 2;
  // Transmit 2 packets.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  AckedPacketVector packets_acked;
  // Neuter packet 1.
  unacked_packets_.RemoveRetransmittability(QuicPacketNumber(1));
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt());

  // Early retransmit when the final packet gets acked and the first is nacked.
  unacked_packets_.MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(2));
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  EXPECT_EQ(clock_.Now() + 0.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, EarlyRetransmitWithLargerUnackablePackets) {
  // Transmit 2 data packets and one ack.
  SendDataPacket(1);
  SendDataPacket(2);
  SendAckPacket(3);
  AckedPacketVector packets_acked;
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt());

  // Early retransmit when the final packet gets acked and the first is nacked.
  unacked_packets_.MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(2));
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  EXPECT_EQ(clock_.Now() + 0.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());

  // The packet should be lost once the loss timeout is reached.
  clock_.AdvanceTime(0.25 * rtt_stats_.latest_rtt());
  VerifyLosses(2, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, AlwaysLosePacketSent1RTTEarlier) {
  // Transmit 1 packet and then wait an rtt plus 1ms.
  SendDataPacket(1);
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() +
                     QuicTime::Delta::FromMilliseconds(1));

  // Transmit 2 packets.
  SendDataPacket(2);
  SendDataPacket(3);
  AckedPacketVector packets_acked;
  // Wait another RTT and ack 2.
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt());
  unacked_packets_.MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(2));
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, {1});
}

TEST_F(GeneralLossAlgorithmTest, IncreaseTimeThresholdUponSpuriousLoss) {
  loss_algorithm_.enable_adaptive_time_threshold();
  loss_algorithm_.set_reordering_shift(kDefaultLossDelayShift);
  EXPECT_EQ(kDefaultLossDelayShift, loss_algorithm_.reordering_shift());
  EXPECT_TRUE(loss_algorithm_.use_adaptive_time_threshold());
  const size_t kNumSentPackets = 10;
  // Transmit 2 packets at 1/10th an RTT interval.
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
    clock_.AdvanceTime(0.1 * rtt_stats_.smoothed_rtt());
  }
  EXPECT_EQ(QuicTime::Zero() + rtt_stats_.smoothed_rtt(), clock_.Now());
  AckedPacketVector packets_acked;
  // Expect the timer to not be set.
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
  // Packet 1 should not be lost until 1/4 RTTs pass.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  // Expect the timer to be set to 1/4 RTT's in the future.
  EXPECT_EQ(rtt_stats_.smoothed_rtt() * (1.0f / 4),
            loss_algorithm_.GetLossTimeout() - clock_.Now());
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() * (1.0f / 4));
  VerifyLosses(2, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
  // Retransmit packet 1 as 11 and 2 as 12.
  SendDataPacket(11);
  SendDataPacket(12);

  // Advance the time 1/4 RTT and indicate the loss was spurious.
  // The new threshold should be 1/2 RTT.
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() * (1.0f / 4));
  loss_algorithm_.SpuriousLossDetected(unacked_packets_, rtt_stats_,
                                       clock_.Now(), QuicPacketNumber(1),
                                       QuicPacketNumber(2));
  EXPECT_EQ(1, loss_algorithm_.reordering_shift());
}

TEST_F(GeneralLossAlgorithmTest, IncreaseReorderingThresholdUponSpuriousLoss) {
  loss_algorithm_.set_use_adaptive_reordering_threshold(true);
  for (size_t i = 1; i <= 4; ++i) {
    SendDataPacket(i);
  }
  // Acking 4 causes 1 detected lost.
  AckedPacketVector packets_acked;
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(4));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(4), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(4, packets_acked, std::vector<uint64_t>{1});
  packets_acked.clear();

  // Retransmit 1 as 5.
  SendDataPacket(5);

  // Acking 1 such that it was detected lost spuriously.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(1));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(1), kMaxOutgoingPacketSize, QuicTime::Zero()));
  loss_algorithm_.SpuriousLossDetected(unacked_packets_, rtt_stats_,
                                       clock_.Now(), QuicPacketNumber(1),
                                       QuicPacketNumber(4));
  VerifyLosses(4, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();

  // Verify acking 5 does not cause 2 detected lost.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(5));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(5), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(5, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();

  SendDataPacket(6);

  // Acking 6 will causes 2 detected lost.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(6));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(6), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(6, packets_acked, std::vector<uint64_t>{2});
  packets_acked.clear();

  // Retransmit 2 as 7.
  SendDataPacket(7);

  // Acking 2 such that it was detected lost spuriously.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  loss_algorithm_.SpuriousLossDetected(unacked_packets_, rtt_stats_,
                                       clock_.Now(), QuicPacketNumber(2),
                                       QuicPacketNumber(6));
  VerifyLosses(6, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();

  // Acking 7 will not cause 3 as detected lost.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(7));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(7), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(7, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
}

TEST_F(GeneralLossAlgorithmTest, DefaultIetfLossDetection) {
  loss_algorithm_.set_reordering_shift(kDefaultIetfLossDelayShift);
  for (size_t i = 1; i <= 6; ++i) {
    SendDataPacket(i);
  }
  // Packet threshold loss detection.
  AckedPacketVector packets_acked;
  // No loss on one ack.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  // No loss on two acks.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(3));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(3), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(3, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  // Loss on three acks.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(4));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(4), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(4, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
  packets_acked.clear();

  SendDataPacket(7);

  // Time threshold loss detection.
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(6));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(6), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(6, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  EXPECT_EQ(clock_.Now() + rtt_stats_.smoothed_rtt() +
                (rtt_stats_.smoothed_rtt() >> 3),
            loss_algorithm_.GetLossTimeout());
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() +
                     (rtt_stats_.smoothed_rtt() >> 3));
  VerifyLosses(6, packets_acked, {5});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, IetfLossDetectionWithOneFourthRttDelay) {
  loss_algorithm_.set_reordering_shift(2);
  SendDataPacket(1);
  SendDataPacket(2);

  AckedPacketVector packets_acked;
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(2));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(2), kMaxOutgoingPacketSize, QuicTime::Zero()));
  VerifyLosses(2, packets_acked, std::vector<uint64_t>{});
  packets_acked.clear();
  EXPECT_EQ(clock_.Now() + rtt_stats_.smoothed_rtt() +
                (rtt_stats_.smoothed_rtt() >> 2),
            loss_algorithm_.GetLossTimeout());
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() +
                     (rtt_stats_.smoothed_rtt() >> 2));
  VerifyLosses(2, packets_acked, {1});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

TEST_F(GeneralLossAlgorithmTest, NoPacketThresholdForRuntPackets) {
  loss_algorithm_.disable_packet_threshold_for_runt_packets();
  for (size_t i = 1; i <= 6; ++i) {
    SendDataPacket(i);
  }
  // Send a small packet.
  SendDataPacket(7, /*encrypted_length=*/kDefaultLength / 2);
  // No packet threshold for runt packet.
  AckedPacketVector packets_acked;
  unacked_packets_.RemoveFromInFlight(QuicPacketNumber(7));
  packets_acked.push_back(AckedPacket(
      QuicPacketNumber(7), kMaxOutgoingPacketSize, QuicTime::Zero()));
  // Verify no packet is detected lost because packet 7 is a runt.
  VerifyLosses(7, packets_acked, std::vector<uint64_t>{});
  EXPECT_EQ(clock_.Now() + rtt_stats_.smoothed_rtt() +
                (rtt_stats_.smoothed_rtt() >> 2),
            loss_algorithm_.GetLossTimeout());
  clock_.AdvanceTime(rtt_stats_.smoothed_rtt() +
                     (rtt_stats_.smoothed_rtt() >> 2));
  // Verify packets are declared lost because time threshold has passed.
  VerifyLosses(7, packets_acked, {1, 2, 3, 4, 5, 6});
  EXPECT_EQ(QuicTime::Zero(), loss_algorithm_.GetLossTimeout());
}

}  // namespace
}  // namespace test
}  // namespace quic
