// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_sent_packet_manager.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::IsEmpty;
using testing::Not;
using testing::Pointwise;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace quic {
namespace test {
namespace {
// Default packet length.
const uint32_t kDefaultLength = 1000;

// Stream ID for data sent in CreatePacket().
const QuicStreamId kStreamId = 7;

// Matcher to check that the packet number matches the second argument.
MATCHER(PacketNumberEq, "") {
  return ::testing::get<0>(arg).packet_number ==
         QuicPacketNumber(::testing::get<1>(arg));
}

class MockDebugDelegate : public QuicSentPacketManager::DebugDelegate {
 public:
  MOCK_METHOD2(OnSpuriousPacketRetransmission,
               void(TransmissionType transmission_type,
                    QuicByteCount byte_size));
  MOCK_METHOD4(OnPacketLoss,
               void(QuicPacketNumber lost_packet_number,
                    EncryptionLevel encryption_level,
                    TransmissionType transmission_type,
                    QuicTime detection_time));
};

class QuicSentPacketManagerTest : public QuicTest {
 public:
  void RetransmitCryptoPacket(uint64_t packet_number) {
    EXPECT_CALL(
        *send_algorithm_,
        OnPacketSent(_, BytesInFlight(), QuicPacketNumber(packet_number),
                     kDefaultLength, HAS_RETRANSMITTABLE_DATA));
    SerializedPacket packet(CreatePacket(packet_number, false));
    packet.retransmittable_frames.push_back(
        QuicFrame(QuicStreamFrame(1, false, 0, quiche::QuicheStringPiece())));
    packet.has_crypto_handshake = IS_HANDSHAKE;
    manager_.OnPacketSent(&packet, clock_.Now(), HANDSHAKE_RETRANSMISSION,
                          HAS_RETRANSMITTABLE_DATA);
  }

  void RetransmitDataPacket(uint64_t packet_number,
                            TransmissionType type,
                            EncryptionLevel level) {
    EXPECT_CALL(
        *send_algorithm_,
        OnPacketSent(_, BytesInFlight(), QuicPacketNumber(packet_number),
                     kDefaultLength, HAS_RETRANSMITTABLE_DATA));
    SerializedPacket packet(CreatePacket(packet_number, true));
    packet.encryption_level = level;
    manager_.OnPacketSent(&packet, clock_.Now(), type,
                          HAS_RETRANSMITTABLE_DATA);
  }

  void RetransmitDataPacket(uint64_t packet_number, TransmissionType type) {
    RetransmitDataPacket(packet_number, type, ENCRYPTION_INITIAL);
  }

 protected:
  const CongestionControlType kInitialCongestionControlType = kCubicBytes;
  QuicSentPacketManagerTest()
      : manager_(Perspective::IS_SERVER,
                 &clock_,
                 QuicRandom::GetInstance(),
                 &stats_,
                 kInitialCongestionControlType),
        send_algorithm_(new StrictMock<MockSendAlgorithm>),
        network_change_visitor_(new StrictMock<MockNetworkChangeVisitor>) {
    QuicSentPacketManagerPeer::SetSendAlgorithm(&manager_, send_algorithm_);
    // Disable tail loss probes for most tests.
    QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 0);
    // Advance the time 1s so the send times are never QuicTime::Zero.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1000));
    manager_.SetNetworkChangeVisitor(network_change_visitor_.get());
    manager_.SetSessionNotifier(&notifier_);

    EXPECT_CALL(*send_algorithm_, GetCongestionControlType())
        .WillRepeatedly(Return(kInitialCongestionControlType));
    EXPECT_CALL(*send_algorithm_, HasReliableBandwidthEstimate())
        .Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
        .Times(AnyNumber())
        .WillRepeatedly(Return(QuicBandwidth::Zero()));
    EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, OnPacketNeutered(_)).Times(AnyNumber());
    EXPECT_CALL(*network_change_visitor_, OnPathMtuIncreased(1000))
        .Times(AnyNumber());
    EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(notifier_, HasUnackedCryptoData())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(notifier_, OnStreamFrameRetransmitted(_)).Times(AnyNumber());
    EXPECT_CALL(notifier_, OnFrameAcked(_, _, _)).WillRepeatedly(Return(true));
  }

  ~QuicSentPacketManagerTest() override {}

  QuicByteCount BytesInFlight() { return manager_.GetBytesInFlight(); }
  void VerifyUnackedPackets(uint64_t* packets, size_t num_packets) {
    if (num_packets == 0) {
      EXPECT_TRUE(manager_.unacked_packets().empty());
      EXPECT_EQ(0u, QuicSentPacketManagerPeer::GetNumRetransmittablePackets(
                        &manager_));
      return;
    }

    EXPECT_FALSE(manager_.unacked_packets().empty());
    EXPECT_EQ(QuicPacketNumber(packets[0]), manager_.GetLeastUnacked());
    for (size_t i = 0; i < num_packets; ++i) {
      EXPECT_TRUE(
          manager_.unacked_packets().IsUnacked(QuicPacketNumber(packets[i])))
          << packets[i];
    }
  }

  void VerifyRetransmittablePackets(uint64_t* packets, size_t num_packets) {
    EXPECT_EQ(
        num_packets,
        QuicSentPacketManagerPeer::GetNumRetransmittablePackets(&manager_));
    for (size_t i = 0; i < num_packets; ++i) {
      EXPECT_TRUE(QuicSentPacketManagerPeer::HasRetransmittableFrames(
          &manager_, packets[i]))
          << " packets[" << i << "]:" << packets[i];
    }
  }

  void ExpectAck(uint64_t largest_observed) {
    EXPECT_CALL(
        *send_algorithm_,
        // Ensure the AckedPacketVector argument contains largest_observed.
        OnCongestionEvent(true, _, _,
                          Pointwise(PacketNumberEq(), {largest_observed}),
                          IsEmpty()));
    EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  }

  void ExpectUpdatedRtt(uint64_t /*largest_observed*/) {
    EXPECT_CALL(*send_algorithm_,
                OnCongestionEvent(true, _, _, IsEmpty(), IsEmpty()));
    EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  }

  void ExpectAckAndLoss(bool rtt_updated,
                        uint64_t largest_observed,
                        uint64_t lost_packet) {
    EXPECT_CALL(
        *send_algorithm_,
        OnCongestionEvent(rtt_updated, _, _,
                          Pointwise(PacketNumberEq(), {largest_observed}),
                          Pointwise(PacketNumberEq(), {lost_packet})));
    EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  }

  // |packets_acked| and |packets_lost| should be in packet number order.
  void ExpectAcksAndLosses(bool rtt_updated,
                           uint64_t* packets_acked,
                           size_t num_packets_acked,
                           uint64_t* packets_lost,
                           size_t num_packets_lost) {
    std::vector<QuicPacketNumber> ack_vector;
    for (size_t i = 0; i < num_packets_acked; ++i) {
      ack_vector.push_back(QuicPacketNumber(packets_acked[i]));
    }
    std::vector<QuicPacketNumber> lost_vector;
    for (size_t i = 0; i < num_packets_lost; ++i) {
      lost_vector.push_back(QuicPacketNumber(packets_lost[i]));
    }
    EXPECT_CALL(*send_algorithm_,
                OnCongestionEvent(rtt_updated, _, _,
                                  Pointwise(PacketNumberEq(), ack_vector),
                                  Pointwise(PacketNumberEq(), lost_vector)));
    EXPECT_CALL(*network_change_visitor_, OnCongestionChange())
        .Times(AnyNumber());
  }

  void RetransmitAndSendPacket(uint64_t old_packet_number,
                               uint64_t new_packet_number) {
    RetransmitAndSendPacket(old_packet_number, new_packet_number,
                            TLP_RETRANSMISSION);
  }

  void RetransmitAndSendPacket(uint64_t old_packet_number,
                               uint64_t new_packet_number,
                               TransmissionType transmission_type) {
    bool is_lost = false;
    if (transmission_type == HANDSHAKE_RETRANSMISSION ||
        transmission_type == TLP_RETRANSMISSION ||
        transmission_type == RTO_RETRANSMISSION ||
        transmission_type == PROBING_RETRANSMISSION) {
      EXPECT_CALL(notifier_, RetransmitFrames(_, _))
          .WillOnce(WithArgs<1>(
              Invoke([this, new_packet_number](TransmissionType type) {
                RetransmitDataPacket(new_packet_number, type);
              })));
    } else {
      EXPECT_CALL(notifier_, OnFrameLost(_)).Times(1);
      is_lost = true;
    }
    QuicSentPacketManagerPeer::MarkForRetransmission(
        &manager_, old_packet_number, transmission_type);
    if (!is_lost) {
      return;
    }
    EXPECT_CALL(
        *send_algorithm_,
        OnPacketSent(_, BytesInFlight(), QuicPacketNumber(new_packet_number),
                     kDefaultLength, HAS_RETRANSMITTABLE_DATA));
    SerializedPacket packet(CreatePacket(new_packet_number, true));
    manager_.OnPacketSent(&packet, clock_.Now(), transmission_type,
                          HAS_RETRANSMITTABLE_DATA);
  }

  SerializedPacket CreateDataPacket(uint64_t packet_number) {
    return CreatePacket(packet_number, true);
  }

  SerializedPacket CreatePacket(uint64_t packet_number, bool retransmittable) {
    SerializedPacket packet(QuicPacketNumber(packet_number),
                            PACKET_4BYTE_PACKET_NUMBER, nullptr, kDefaultLength,
                            false, false);
    if (retransmittable) {
      packet.retransmittable_frames.push_back(QuicFrame(
          QuicStreamFrame(kStreamId, false, 0, quiche::QuicheStringPiece())));
    }
    return packet;
  }

  SerializedPacket CreatePingPacket(uint64_t packet_number) {
    SerializedPacket packet(QuicPacketNumber(packet_number),
                            PACKET_4BYTE_PACKET_NUMBER, nullptr, kDefaultLength,
                            false, false);
    packet.retransmittable_frames.push_back(QuicFrame(QuicPingFrame()));
    return packet;
  }

  void SendDataPacket(uint64_t packet_number) {
    SendDataPacket(packet_number, ENCRYPTION_INITIAL);
  }

  void SendDataPacket(uint64_t packet_number,
                      EncryptionLevel encryption_level) {
    EXPECT_CALL(*send_algorithm_,
                OnPacketSent(_, BytesInFlight(),
                             QuicPacketNumber(packet_number), _, _));
    SerializedPacket packet(CreateDataPacket(packet_number));
    packet.encryption_level = encryption_level;
    manager_.OnPacketSent(&packet, clock_.Now(), NOT_RETRANSMISSION,
                          HAS_RETRANSMITTABLE_DATA);
  }

  void SendPingPacket(uint64_t packet_number,
                      EncryptionLevel encryption_level) {
    EXPECT_CALL(*send_algorithm_,
                OnPacketSent(_, BytesInFlight(),
                             QuicPacketNumber(packet_number), _, _));
    SerializedPacket packet(CreatePingPacket(packet_number));
    packet.encryption_level = encryption_level;
    manager_.OnPacketSent(&packet, clock_.Now(), NOT_RETRANSMISSION,
                          HAS_RETRANSMITTABLE_DATA);
  }

  void SendCryptoPacket(uint64_t packet_number) {
    EXPECT_CALL(
        *send_algorithm_,
        OnPacketSent(_, BytesInFlight(), QuicPacketNumber(packet_number),
                     kDefaultLength, HAS_RETRANSMITTABLE_DATA));
    SerializedPacket packet(CreatePacket(packet_number, false));
    packet.retransmittable_frames.push_back(
        QuicFrame(QuicStreamFrame(1, false, 0, quiche::QuicheStringPiece())));
    packet.has_crypto_handshake = IS_HANDSHAKE;
    manager_.OnPacketSent(&packet, clock_.Now(), NOT_RETRANSMISSION,
                          HAS_RETRANSMITTABLE_DATA);
    EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(true));
  }

  void SendAckPacket(uint64_t packet_number, uint64_t largest_acked) {
    SendAckPacket(packet_number, largest_acked, ENCRYPTION_INITIAL);
  }

  void SendAckPacket(uint64_t packet_number,
                     uint64_t largest_acked,
                     EncryptionLevel level) {
    EXPECT_CALL(
        *send_algorithm_,
        OnPacketSent(_, BytesInFlight(), QuicPacketNumber(packet_number),
                     kDefaultLength, NO_RETRANSMITTABLE_DATA));
    SerializedPacket packet(CreatePacket(packet_number, false));
    packet.largest_acked = QuicPacketNumber(largest_acked);
    packet.encryption_level = level;
    manager_.OnPacketSent(&packet, clock_.Now(), NOT_RETRANSMISSION,
                          NO_RETRANSMITTABLE_DATA);
  }

  void EnablePto(QuicTag tag) {
    QuicConfig config;
    QuicTagVector options;
    options.push_back(tag);
    QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
    EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
    EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
    manager_.SetFromConfig(config);
    EXPECT_TRUE(manager_.pto_enabled());
  }

  QuicSentPacketManager manager_;
  MockClock clock_;
  QuicConnectionStats stats_;
  MockSendAlgorithm* send_algorithm_;
  std::unique_ptr<MockNetworkChangeVisitor> network_change_visitor_;
  StrictMock<MockSessionNotifier> notifier_;
};

TEST_F(QuicSentPacketManagerTest, IsUnacked) {
  VerifyUnackedPackets(nullptr, 0);
  SendDataPacket(1);

  uint64_t unacked[] = {1};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  uint64_t retransmittable[] = {1};
  VerifyRetransmittablePackets(retransmittable,
                               QUICHE_ARRAYSIZE(retransmittable));
}

TEST_F(QuicSentPacketManagerTest, IsUnAckedRetransmit) {
  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);

  EXPECT_TRUE(QuicSentPacketManagerPeer::IsRetransmission(&manager_, 2));
  uint64_t unacked[] = {1, 2};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  std::vector<uint64_t> retransmittable = {1, 2};
  VerifyRetransmittablePackets(&retransmittable[0], retransmittable.size());
}

TEST_F(QuicSentPacketManagerTest, RetransmitThenAck) {
  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);

  // Ack 2 but not 1.
  ExpectAck(2);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  // Packet 1 is unacked, pending, but not retransmittable.
  uint64_t unacked[] = {1};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  EXPECT_TRUE(manager_.HasInFlightPackets());
  VerifyRetransmittablePackets(nullptr, 0);
}

TEST_F(QuicSentPacketManagerTest, RetransmitThenAckBeforeSend) {
  SendDataPacket(1);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(2, type); })));
  QuicSentPacketManagerPeer::MarkForRetransmission(&manager_, 1,
                                                   TLP_RETRANSMISSION);
  // Ack 1.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  uint64_t unacked[] = {2};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  // We do not know packet 2 is a spurious retransmission until it gets acked.
  VerifyRetransmittablePackets(nullptr, 0);
  EXPECT_EQ(0u, stats_.packets_spuriously_retransmitted);
}

TEST_F(QuicSentPacketManagerTest, RetransmitThenStopRetransmittingBeforeSend) {
  SendDataPacket(1);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _));
  QuicSentPacketManagerPeer::MarkForRetransmission(&manager_, 1,
                                                   TLP_RETRANSMISSION);

  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));

  uint64_t unacked[] = {1};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  VerifyRetransmittablePackets(nullptr, 0);
  EXPECT_EQ(0u, stats_.packets_spuriously_retransmitted);
}

TEST_F(QuicSentPacketManagerTest, RetransmitThenAckPrevious) {
  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);
  QuicTime::Delta rtt = QuicTime::Delta::FromMilliseconds(15);
  clock_.AdvanceTime(rtt);

  // Ack 1 but not 2.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  // 2 remains unacked, but no packets have retransmittable data.
  uint64_t unacked[] = {2};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  EXPECT_TRUE(manager_.HasInFlightPackets());
  VerifyRetransmittablePackets(nullptr, 0);
  // Ack 2 causes 2 be considered as spurious retransmission.
  EXPECT_CALL(notifier_, OnFrameAcked(_, _, _)).WillOnce(Return(false));
  ExpectAck(2);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));

  EXPECT_EQ(1u, stats_.packets_spuriously_retransmitted);
}

TEST_F(QuicSentPacketManagerTest, RetransmitThenAckPreviousThenNackRetransmit) {
  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);
  QuicTime::Delta rtt = QuicTime::Delta::FromMilliseconds(15);
  clock_.AdvanceTime(rtt);

  // First, ACK packet 1 which makes packet 2 non-retransmittable.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  SendDataPacket(3);
  SendDataPacket(4);
  SendDataPacket(5);
  clock_.AdvanceTime(rtt);

  // Next, NACK packet 2 three times.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(1);
  ExpectAckAndLoss(true, 3, 2);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(4));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));

  ExpectAck(4);
  manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(5));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(3),
                                   ENCRYPTION_INITIAL));

  ExpectAck(5);
  manager_.OnAckFrameStart(QuicPacketNumber(5), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(6));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(4),
                                   ENCRYPTION_INITIAL));

  uint64_t unacked[] = {2};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  EXPECT_FALSE(manager_.HasInFlightPackets());
  VerifyRetransmittablePackets(nullptr, 0);

  // Verify that the retransmission alarm would not fire,
  // since there is no retransmittable data outstanding.
  EXPECT_EQ(QuicTime::Zero(), manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest,
       DISABLED_RetransmitTwiceThenAckPreviousBeforeSend) {
  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);

  // Fire the RTO, which will mark 2 for retransmission (but will not send it).
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.OnRetransmissionTimeout();

  // Ack 1 but not 2, before 2 is able to be sent.
  // Since 1 has been retransmitted, it has already been lost, and so the
  // send algorithm is not informed that it has been ACK'd.
  ExpectUpdatedRtt(1);
  EXPECT_CALL(*send_algorithm_, RevertRetransmissionTimeout());
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // Since 2 was marked for retransmit, when 1 is acked, 2 is kept for RTT.
  uint64_t unacked[] = {2};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  EXPECT_FALSE(manager_.HasInFlightPackets());
  VerifyRetransmittablePackets(nullptr, 0);

  // Verify that the retransmission alarm would not fire,
  // since there is no retransmittable data outstanding.
  EXPECT_EQ(QuicTime::Zero(), manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, RetransmitTwiceThenAckFirst) {
  StrictMock<MockDebugDelegate> debug_delegate;
  EXPECT_CALL(debug_delegate, OnSpuriousPacketRetransmission(TLP_RETRANSMISSION,
                                                             kDefaultLength))
      .Times(1);
  manager_.SetDebugDelegate(&debug_delegate);

  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);
  RetransmitAndSendPacket(2, 3);
  QuicTime::Delta rtt = QuicTime::Delta::FromMilliseconds(15);
  clock_.AdvanceTime(rtt);

  // Ack 1 but not 2 or 3.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  // Frames in packets 2 and 3 are acked.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_))
      .Times(2)
      .WillRepeatedly(Return(false));

  // 2 and 3 remain unacked, but no packets have retransmittable data.
  uint64_t unacked[] = {2, 3};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  EXPECT_TRUE(manager_.HasInFlightPackets());
  VerifyRetransmittablePackets(nullptr, 0);

  // Ensure packet 2 is lost when 4 is sent and 3 and 4 are acked.
  SendDataPacket(4);
  // No new data gets acked in packet 3.
  EXPECT_CALL(notifier_, OnFrameAcked(_, _, _))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  uint64_t acked[] = {3, 4};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(5));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));

  uint64_t unacked2[] = {2};
  VerifyUnackedPackets(unacked2, QUICHE_ARRAYSIZE(unacked2));
  EXPECT_TRUE(manager_.HasInFlightPackets());

  SendDataPacket(5);
  ExpectAckAndLoss(true, 5, 2);
  EXPECT_CALL(debug_delegate,
              OnPacketLoss(QuicPacketNumber(2), _, LOSS_RETRANSMISSION, _));
  // Frames in all packets are acked.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  // Notify session that stream frame in packet 2 gets lost although it is
  // not outstanding.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(1);
  manager_.OnAckFrameStart(QuicPacketNumber(5), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(6));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(3),
                                   ENCRYPTION_INITIAL));

  uint64_t unacked3[] = {2};
  VerifyUnackedPackets(unacked3, QUICHE_ARRAYSIZE(unacked3));
  EXPECT_FALSE(manager_.HasInFlightPackets());
  // Spurious retransmission is detected when packet 3 gets acked. We cannot
  // know packet 2 is a spurious until it gets acked.
  EXPECT_EQ(1u, stats_.packets_spuriously_retransmitted);
  EXPECT_EQ(1u, stats_.packets_lost);
  EXPECT_LT(QuicTime::Delta::Zero(), stats_.total_loss_detection_time);
}

TEST_F(QuicSentPacketManagerTest, AckOriginalTransmission) {
  auto loss_algorithm = std::make_unique<MockLossAlgorithm>();
  QuicSentPacketManagerPeer::SetLossAlgorithm(&manager_, loss_algorithm.get());

  SendDataPacket(1);
  RetransmitAndSendPacket(1, 2);

  // Ack original transmission, but that wasn't lost via fast retransmit,
  // so no call on OnSpuriousRetransmission is expected.
  {
    ExpectAck(1);
    EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
    manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                             clock_.Now());
    manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
    EXPECT_EQ(PACKETS_NEWLY_ACKED,
              manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                     ENCRYPTION_INITIAL));
  }

  SendDataPacket(3);
  SendDataPacket(4);
  // Ack 4, which causes 3 to be retransmitted.
  {
    ExpectAck(4);
    EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
    manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                             clock_.Now());
    manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(5));
    manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
    EXPECT_EQ(PACKETS_NEWLY_ACKED,
              manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                     ENCRYPTION_INITIAL));
    RetransmitAndSendPacket(3, 5, LOSS_RETRANSMISSION);
  }

  // Ack 3, which causes SpuriousRetransmitDetected to be called.
  {
    uint64_t acked[] = {3};
    ExpectAcksAndLosses(false, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
    EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
    EXPECT_CALL(*loss_algorithm,
                SpuriousLossDetected(_, _, _, QuicPacketNumber(3),
                                     QuicPacketNumber(4)));
    manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                             clock_.Now());
    manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(5));
    manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
    EXPECT_EQ(0u, stats_.packet_spuriously_detected_lost);
    EXPECT_EQ(PACKETS_NEWLY_ACKED,
              manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(3),
                                     ENCRYPTION_INITIAL));
    EXPECT_EQ(1u, stats_.packet_spuriously_detected_lost);
    // Ack 3 will not cause 5 be considered as a spurious retransmission. Ack
    // 5 will cause 5 be considered as a spurious retransmission as no new
    // data gets acked.
    ExpectAck(5);
    EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
    EXPECT_CALL(notifier_, OnFrameAcked(_, _, _)).WillOnce(Return(false));
    manager_.OnAckFrameStart(QuicPacketNumber(5), QuicTime::Delta::Infinite(),
                             clock_.Now());
    manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(6));
    manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
    EXPECT_EQ(PACKETS_NEWLY_ACKED,
              manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(4),
                                     ENCRYPTION_INITIAL));
  }
}

TEST_F(QuicSentPacketManagerTest, GetLeastUnacked) {
  EXPECT_EQ(QuicPacketNumber(1u), manager_.GetLeastUnacked());
}

TEST_F(QuicSentPacketManagerTest, GetLeastUnackedUnacked) {
  SendDataPacket(1);
  EXPECT_EQ(QuicPacketNumber(1u), manager_.GetLeastUnacked());
}

TEST_F(QuicSentPacketManagerTest, AckAckAndUpdateRtt) {
  EXPECT_FALSE(manager_.largest_packet_peer_knows_is_acked().IsInitialized());
  SendDataPacket(1);
  SendAckPacket(2, 1);

  // Now ack the ack and expect an RTT update.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2),
                           QuicTime::Delta::FromMilliseconds(5), clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(QuicPacketNumber(1), manager_.largest_packet_peer_knows_is_acked());

  SendAckPacket(3, 3);

  // Now ack the ack and expect only an RTT update.
  uint64_t acked2[] = {3};
  ExpectAcksAndLosses(true, acked2, QUICHE_ARRAYSIZE(acked2), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(QuicPacketNumber(3u),
            manager_.largest_packet_peer_knows_is_acked());
}

TEST_F(QuicSentPacketManagerTest, Rtt) {
  QuicTime::Delta expected_rtt = QuicTime::Delta::FromMilliseconds(20);
  SendDataPacket(1);
  clock_.AdvanceTime(expected_rtt);

  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(expected_rtt, manager_.GetRttStats()->latest_rtt());
}

TEST_F(QuicSentPacketManagerTest, RttWithInvalidDelta) {
  // Expect that the RTT is equal to the local time elapsed, since the
  // ack_delay_time is larger than the local time elapsed
  // and is hence invalid.
  QuicTime::Delta expected_rtt = QuicTime::Delta::FromMilliseconds(10);
  SendDataPacket(1);
  clock_.AdvanceTime(expected_rtt);

  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1),
                           QuicTime::Delta::FromMilliseconds(11), clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(expected_rtt, manager_.GetRttStats()->latest_rtt());
}

TEST_F(QuicSentPacketManagerTest, RttWithInfiniteDelta) {
  // Expect that the RTT is equal to the local time elapsed, since the
  // ack_delay_time is infinite, and is hence invalid.
  QuicTime::Delta expected_rtt = QuicTime::Delta::FromMilliseconds(10);
  SendDataPacket(1);
  clock_.AdvanceTime(expected_rtt);

  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(expected_rtt, manager_.GetRttStats()->latest_rtt());
}

TEST_F(QuicSentPacketManagerTest, RttWithDeltaExceedingLimit) {
  // Initialize min and smoothed rtt to 10ms.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());

  QuicTime::Delta send_delta = QuicTime::Delta::FromMilliseconds(100);
  QuicTime::Delta ack_delay =
      QuicTime::Delta::FromMilliseconds(5) + manager_.peer_max_ack_delay();
  ASSERT_GT(send_delta - rtt_stats->min_rtt(), ack_delay);
  SendDataPacket(1);
  clock_.AdvanceTime(send_delta);

  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), ack_delay, clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_FORWARD_SECURE));

  QuicTime::Delta expected_rtt_sample =
      send_delta - manager_.peer_max_ack_delay();
  EXPECT_EQ(expected_rtt_sample, manager_.GetRttStats()->latest_rtt());
}

TEST_F(QuicSentPacketManagerTest, RttZeroDelta) {
  // Expect that the RTT is the time between send and receive since the
  // ack_delay_time is zero.
  QuicTime::Delta expected_rtt = QuicTime::Delta::FromMilliseconds(10);
  SendDataPacket(1);
  clock_.AdvanceTime(expected_rtt);

  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(expected_rtt, manager_.GetRttStats()->latest_rtt());
}

TEST_F(QuicSentPacketManagerTest, TailLossProbeTimeout) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);

  // Send 1 packet.
  SendDataPacket(1);

  // The first tail loss probe retransmits 1 packet.
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(2, type); })));
  manager_.MaybeRetransmitTailLossProbe();

  // The second tail loss probe retransmits 1 packet.
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  manager_.MaybeRetransmitTailLossProbe();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));

  // Ack the third and ensure the first two are still pending.
  ExpectAck(3);

  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  EXPECT_TRUE(manager_.HasInFlightPackets());

  // Acking two more packets will lose both of them due to nacks.
  SendDataPacket(4);
  SendDataPacket(5);
  uint64_t acked[] = {4, 5};
  uint64_t lost[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), lost,
                      QUICHE_ARRAYSIZE(lost));
  // Frames in all packets are acked.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  // Notify session that stream frame in packets 1 and 2 get lost although
  // they are not outstanding.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(2);
  manager_.OnAckFrameStart(QuicPacketNumber(5), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(6));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));

  EXPECT_FALSE(manager_.HasInFlightPackets());
  EXPECT_EQ(2u, stats_.tlp_count);
  EXPECT_EQ(0u, stats_.rto_count);
}

TEST_F(QuicSentPacketManagerTest, TailLossProbeThenRTO) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);

  // Send 100 packets.
  const size_t kNumSentPackets = 100;
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }
  QuicTime rto_packet_time = clock_.Now();
  // Advance the time.
  clock_.AdvanceTime(manager_.GetRetransmissionTime() - clock_.Now());

  // The first tail loss probe retransmits 1 packet.
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(101, type); })));
  manager_.MaybeRetransmitTailLossProbe();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));
  clock_.AdvanceTime(manager_.GetRetransmissionTime() - clock_.Now());

  // The second tail loss probe retransmits 1 packet.
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(102, type); })));
  EXPECT_TRUE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));

  // Ensure the RTO is set based on the correct packet.
  rto_packet_time = clock_.Now();
  EXPECT_EQ(rto_packet_time + QuicTime::Delta::FromMilliseconds(500),
            manager_.GetRetransmissionTime());

  // Advance the time enough to ensure all packets are RTO'd.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1000));

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(103, type); })))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(104, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(2u, stats_.tlp_count);
  EXPECT_EQ(1u, stats_.rto_count);
  // There are 2 RTO retransmissions.
  EXPECT_EQ(104 * kDefaultLength, manager_.GetBytesInFlight());
  QuicPacketNumber largest_acked = QuicPacketNumber(103);
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  EXPECT_CALL(*send_algorithm_,
              OnCongestionEvent(
                  true, _, _, Pointwise(PacketNumberEq(), {largest_acked}), _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  // Although frames in packet 3 gets acked, it would be kept for another
  // RTT.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(true));
  // Packets [1, 102] are lost, although stream frame in packet 3 is not
  // outstanding.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(102);
  manager_.OnAckFrameStart(QuicPacketNumber(103), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(103), QuicPacketNumber(104));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  // All packets before 103 should be lost.
  // Packet 104 is still in flight.
  EXPECT_EQ(1000u, manager_.GetBytesInFlight());
}

TEST_F(QuicSentPacketManagerTest, CryptoHandshakeTimeout) {
  // Send 2 crypto packets and 3 data packets.
  const size_t kNumSentCryptoPackets = 2;
  for (size_t i = 1; i <= kNumSentCryptoPackets; ++i) {
    SendCryptoPacket(i);
  }
  const size_t kNumSentDataPackets = 3;
  for (size_t i = 1; i <= kNumSentDataPackets; ++i) {
    SendDataPacket(kNumSentCryptoPackets + i);
  }
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());
  EXPECT_EQ(5 * kDefaultLength, manager_.GetBytesInFlight());

  // The first retransmits 2 packets.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(6); }))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(7); }));
  manager_.OnRetransmissionTimeout();
  // Expect all 4 handshake packets to be in flight and 3 data packets.
  EXPECT_EQ(7 * kDefaultLength, manager_.GetBytesInFlight());
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // The second retransmits 2 packets.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(8); }))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(9); }));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(9 * kDefaultLength, manager_.GetBytesInFlight());
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Now ack the two crypto packets and the speculatively encrypted request,
  // and ensure the first four crypto packets get abandoned, but not lost.
  // Crypto packets remain in flight, so any that aren't acked will be lost.
  uint64_t acked[] = {3, 4, 5, 8, 9};
  uint64_t lost[] = {1, 2, 6};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), lost,
                      QUICHE_ARRAYSIZE(lost));
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(3);
  EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(false));
  manager_.OnAckFrameStart(QuicPacketNumber(9), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(8), QuicPacketNumber(10));
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(6));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  EXPECT_FALSE(manager_.HasUnackedCryptoPackets());
}

TEST_F(QuicSentPacketManagerTest, CryptoHandshakeTimeoutVersionNegotiation) {
  // Send 2 crypto packets and 3 data packets.
  const size_t kNumSentCryptoPackets = 2;
  for (size_t i = 1; i <= kNumSentCryptoPackets; ++i) {
    SendCryptoPacket(i);
  }
  const size_t kNumSentDataPackets = 3;
  for (size_t i = 1; i <= kNumSentDataPackets; ++i) {
    SendDataPacket(kNumSentCryptoPackets + i);
  }
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(6); }))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(7); }));
  manager_.OnRetransmissionTimeout();
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Now act like a version negotiation packet arrived, which would cause all
  // unacked packets to be retransmitted.
  // Mark packets [1, 7] lost. And the frames in 6 and 7 are same as packets 1
  // and 2, respectively.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(7);
  manager_.RetransmitUnackedPackets(ALL_UNACKED_RETRANSMISSION);

  // Ensure the first two pending packets are the crypto retransmits.
  RetransmitCryptoPacket(8);
  RetransmitCryptoPacket(9);
  RetransmitDataPacket(10, ALL_UNACKED_RETRANSMISSION);
  RetransmitDataPacket(11, ALL_UNACKED_RETRANSMISSION);
  RetransmitDataPacket(12, ALL_UNACKED_RETRANSMISSION);

  EXPECT_EQ(QuicPacketNumber(1u), manager_.GetLeastUnacked());
  // Least unacked isn't raised until an ack is received, so ack the
  // crypto packets.
  uint64_t acked[] = {8, 9};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(9), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(8), QuicPacketNumber(10));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(false));
  EXPECT_EQ(QuicPacketNumber(10u), manager_.GetLeastUnacked());
}

TEST_F(QuicSentPacketManagerTest, CryptoHandshakeSpuriousRetransmission) {
  // Send 1 crypto packet.
  SendCryptoPacket(1);
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Retransmit the crypto packet as 2.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(2); }));
  manager_.OnRetransmissionTimeout();

  // Retransmit the crypto packet as 3.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(3); }));
  manager_.OnRetransmissionTimeout();

  // Now ack the second crypto packet, and ensure the first gets removed, but
  // the third does not.
  uint64_t acked[] = {2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(false));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  EXPECT_FALSE(manager_.HasUnackedCryptoPackets());
  uint64_t unacked[] = {1, 3};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
}

TEST_F(QuicSentPacketManagerTest, CryptoHandshakeTimeoutUnsentDataPacket) {
  // Send 2 crypto packets and 1 data packet.
  const size_t kNumSentCryptoPackets = 2;
  for (size_t i = 1; i <= kNumSentCryptoPackets; ++i) {
    SendCryptoPacket(i);
  }
  SendDataPacket(3);
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Retransmit 2 crypto packets, but not the serialized packet.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(4); }))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(5); }));
  manager_.OnRetransmissionTimeout();
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());
}

TEST_F(QuicSentPacketManagerTest,
       CryptoHandshakeRetransmissionThenRetransmitAll) {
  // Send 1 crypto packet.
  SendCryptoPacket(1);

  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Retransmit the crypto packet as 2.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(2); }));
  manager_.OnRetransmissionTimeout();
  // Now retransmit all the unacked packets, which occurs when there is a
  // version negotiation.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(2);
  manager_.RetransmitUnackedPackets(ALL_UNACKED_RETRANSMISSION);
  // Both packets 1 and 2 are unackable.
  EXPECT_FALSE(manager_.unacked_packets().IsUnacked(QuicPacketNumber(1)));
  EXPECT_FALSE(manager_.unacked_packets().IsUnacked(QuicPacketNumber(2)));
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());
  EXPECT_FALSE(manager_.HasInFlightPackets());
}

TEST_F(QuicSentPacketManagerTest,
       CryptoHandshakeRetransmissionThenNeuterAndAck) {
  // Send 1 crypto packet.
  SendCryptoPacket(1);

  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Retransmit the crypto packet as 2.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(2); }));
  manager_.OnRetransmissionTimeout();
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Retransmit the crypto packet as 3.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(3); }));
  manager_.OnRetransmissionTimeout();
  EXPECT_TRUE(manager_.HasUnackedCryptoPackets());

  // Now neuter all unacked unencrypted packets, which occurs when the
  // connection goes forward secure.
  EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(false));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.NeuterUnencryptedPackets();
  EXPECT_FALSE(manager_.HasUnackedCryptoPackets());
  uint64_t unacked[] = {1, 2, 3};
  VerifyUnackedPackets(unacked, QUICHE_ARRAYSIZE(unacked));
  VerifyRetransmittablePackets(nullptr, 0);
  EXPECT_FALSE(manager_.HasUnackedCryptoPackets());
  EXPECT_FALSE(manager_.HasInFlightPackets());

  // Ensure both packets get discarded when packet 2 is acked.
  uint64_t acked[] = {3};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  VerifyUnackedPackets(nullptr, 0);
  VerifyRetransmittablePackets(nullptr, 0);
}

TEST_F(QuicSentPacketManagerTest, RetransmissionTimeout) {
  StrictMock<MockDebugDelegate> debug_delegate;
  manager_.SetDebugDelegate(&debug_delegate);

  // Send 100 packets.
  const size_t kNumSentPackets = 100;
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }

  EXPECT_FALSE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(101, type); })))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(102, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(102 * kDefaultLength, manager_.GetBytesInFlight());

  // Ack a retransmission.
  // Ensure no packets are lost.
  QuicPacketNumber largest_acked = QuicPacketNumber(102);
  EXPECT_CALL(*send_algorithm_,
              OnCongestionEvent(true, _, _,
                                Pointwise(PacketNumberEq(), {largest_acked}),
                                /*lost_packets=*/IsEmpty()));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  // RTO's use loss detection instead of immediately declaring retransmitted
  // packets lost.
  for (int i = 1; i <= 99; ++i) {
    EXPECT_CALL(debug_delegate,
                OnPacketLoss(QuicPacketNumber(i), _, LOSS_RETRANSMISSION, _));
  }
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(true));
  // Packets [1, 99] are considered as lost, although stream frame in packet
  // 2 is not outstanding.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(99);
  manager_.OnAckFrameStart(QuicPacketNumber(102), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(102), QuicPacketNumber(103));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, RetransmissionTimeoutOnePacket) {
  // Set the 1RTO connection option.
  QuicConfig client_config;
  QuicTagVector options;
  options.push_back(k1RTO);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(client_config);
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));

  StrictMock<MockDebugDelegate> debug_delegate;
  manager_.SetDebugDelegate(&debug_delegate);

  // Send 100 packets.
  const size_t kNumSentPackets = 100;
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }

  EXPECT_FALSE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(1)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(101, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(101 * kDefaultLength, manager_.GetBytesInFlight());
}

TEST_F(QuicSentPacketManagerTest, NewRetransmissionTimeout) {
  QuicConfig client_config;
  QuicTagVector options;
  options.push_back(kNRTO);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(client_config);
  EXPECT_TRUE(QuicSentPacketManagerPeer::GetUseNewRto(&manager_));
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));

  // Send 100 packets.
  const size_t kNumSentPackets = 100;
  for (size_t i = 1; i <= kNumSentPackets; ++i) {
    SendDataPacket(i);
  }

  EXPECT_FALSE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(101, type); })))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(102, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(102 * kDefaultLength, manager_.GetBytesInFlight());

  // Ack a retransmission and expect no call to OnRetransmissionTimeout.
  // This will include packets in the lost packet map.
  QuicPacketNumber largest_acked = QuicPacketNumber(102);
  EXPECT_CALL(*send_algorithm_,
              OnCongestionEvent(true, _, _,
                                Pointwise(PacketNumberEq(), {largest_acked}),
                                /*lost_packets=*/Not(IsEmpty())));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(true));
  // Packets [1, 99] are considered as lost, although stream frame in packet
  // 2 is not outstanding.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(99);
  manager_.OnAckFrameStart(QuicPacketNumber(102), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(102), QuicPacketNumber(103));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, TwoRetransmissionTimeoutsAckSecond) {
  // Send 1 packet.
  SendDataPacket(1);

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(2, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(2 * kDefaultLength, manager_.GetBytesInFlight());

  // Rto a second time.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(3 * kDefaultLength, manager_.GetBytesInFlight());

  // Ack a retransmission and ensure OnRetransmissionTimeout is called.
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  ExpectAck(2);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // The original packet and newest should be outstanding.
  EXPECT_EQ(2 * kDefaultLength, manager_.GetBytesInFlight());
}

TEST_F(QuicSentPacketManagerTest, TwoRetransmissionTimeoutsAckFirst) {
  // Send 1 packet.
  SendDataPacket(1);

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(2, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(2 * kDefaultLength, manager_.GetBytesInFlight());

  // Rto a second time.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(3 * kDefaultLength, manager_.GetBytesInFlight());

  // Ack a retransmission and ensure OnRetransmissionTimeout is called.
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  ExpectAck(3);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // The first two packets should still be outstanding.
  EXPECT_EQ(2 * kDefaultLength, manager_.GetBytesInFlight());
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionTime) {
  EXPECT_EQ(QuicTime::Zero(), manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionTimeCryptoHandshake) {
  QuicTime crypto_packet_send_time = clock_.Now();
  SendCryptoPacket(1);

  // Check the min.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(1));
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromMilliseconds(10),
            manager_.GetRetransmissionTime());

  // Test with a standard smoothed RTT.
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(100));

  QuicTime::Delta srtt = rtt_stats->initial_rtt();
  QuicTime expected_time = clock_.Now() + 1.5 * srtt;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(1.5 * srtt);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(2); }));
  // When session decides what to write, crypto_packet_send_time gets updated.
  crypto_packet_send_time = clock_.Now();
  manager_.OnRetransmissionTimeout();

  // The retransmission time should now be twice as far in the future.
  expected_time = crypto_packet_send_time + srtt * 2 * 1.5;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Retransmit the packet for the 2nd time.
  clock_.AdvanceTime(2 * 1.5 * srtt);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(3); }));
  // When session decides what to write, crypto_packet_send_time gets updated.
  crypto_packet_send_time = clock_.Now();
  manager_.OnRetransmissionTimeout();

  // Verify exponential backoff of the retransmission timeout.
  expected_time = crypto_packet_send_time + srtt * 4 * 1.5;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest,
       GetConservativeTransmissionTimeCryptoHandshake) {
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kCONH);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  // Calling SetFromConfig requires mocking out some send algorithm methods.
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));

  QuicTime crypto_packet_send_time = clock_.Now();
  SendCryptoPacket(1);

  // Check the min.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(1));
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromMilliseconds(25),
            manager_.GetRetransmissionTime());

  // Test with a standard smoothed RTT.
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(100));

  QuicTime::Delta srtt = rtt_stats->initial_rtt();
  QuicTime expected_time = clock_.Now() + 2 * srtt;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(2 * srtt);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(2); }));
  crypto_packet_send_time = clock_.Now();
  manager_.OnRetransmissionTimeout();

  // The retransmission time should now be twice as far in the future.
  expected_time = crypto_packet_send_time + srtt * 2 * 2;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionTimeTailLossProbe) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);
  SendDataPacket(1);
  SendDataPacket(2);

  // Check the min.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(1));
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromMilliseconds(10),
            manager_.GetRetransmissionTime());

  // Test with a standard smoothed RTT.
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(100));
  QuicTime::Delta srtt = rtt_stats->initial_rtt();
  QuicTime::Delta expected_tlp_delay = 2 * srtt;
  QuicTime expected_time = clock_.Now() + expected_tlp_delay;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(expected_tlp_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  EXPECT_TRUE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));

  expected_time = clock_.Now() + expected_tlp_delay;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, TLPRWithPendingStreamData) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kTLPR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillOnce(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(config);
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::GetEnableHalfRttTailLossProbe(&manager_));

  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);

  SendDataPacket(1);
  SendDataPacket(2);

  // Test with a standard smoothed RTT.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(100));
  QuicTime::Delta srtt = rtt_stats->initial_rtt();
  // With pending stream data, TLPR is used.
  QuicTime::Delta expected_tlp_delay = 0.5 * srtt;
  EXPECT_CALL(notifier_, HasUnackedStreamData()).WillRepeatedly(Return(true));

  EXPECT_EQ(expected_tlp_delay,
            manager_.GetRetransmissionTime() - clock_.Now());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(expected_tlp_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  EXPECT_TRUE(manager_.MaybeRetransmitTailLossProbe());

  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));

  // 2nd TLP.
  expected_tlp_delay = 2 * srtt;
  EXPECT_EQ(expected_tlp_delay,
            manager_.GetRetransmissionTime() - clock_.Now());
}

TEST_F(QuicSentPacketManagerTest, TLPRWithoutPendingStreamData) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kTLPR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillOnce(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(config);
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::GetEnableHalfRttTailLossProbe(&manager_));
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);

  SendPingPacket(1, ENCRYPTION_INITIAL);
  SendPingPacket(2, ENCRYPTION_INITIAL);

  // Test with a standard smoothed RTT.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->set_initial_rtt(QuicTime::Delta::FromMilliseconds(100));
  QuicTime::Delta srtt = rtt_stats->initial_rtt();
  QuicTime::Delta expected_tlp_delay = 0.5 * srtt;
  // With no pending stream data, TLPR is ignored.
  expected_tlp_delay = 2 * srtt;
  EXPECT_CALL(notifier_, HasUnackedStreamData()).WillRepeatedly(Return(false));
  EXPECT_EQ(expected_tlp_delay,
            manager_.GetRetransmissionTime() - clock_.Now());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(expected_tlp_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(3, type); })));
  EXPECT_TRUE(manager_.MaybeRetransmitTailLossProbe());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(QuicTime::Delta::Infinite(), manager_.TimeUntilSend(clock_.Now()));

  // 2nd TLP.
  expected_tlp_delay = 2 * srtt;
  EXPECT_EQ(expected_tlp_delay,
            manager_.GetRetransmissionTime() - clock_.Now());
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionTimeSpuriousRTO) {
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());

  SendDataPacket(1);
  SendDataPacket(2);
  SendDataPacket(3);
  SendDataPacket(4);

  QuicTime::Delta expected_rto_delay =
      rtt_stats->smoothed_rtt() + 4 * rtt_stats->mean_deviation();
  QuicTime expected_time = clock_.Now() + expected_rto_delay;
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Retransmit the packet by invoking the retransmission timeout.
  clock_.AdvanceTime(expected_rto_delay);
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(5, type); })))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(6, type); })));
  manager_.OnRetransmissionTimeout();
  // All previous packets are inflight, plus two rto retransmissions.
  EXPECT_EQ(6 * kDefaultLength, manager_.GetBytesInFlight());

  // The delay should double the second time.
  expected_time = clock_.Now() + expected_rto_delay + expected_rto_delay;
  // Once we always base the timer on the right edge, leaving the older packets
  // in flight doesn't change the timeout.
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());

  // Ack a packet before the first RTO and ensure the RTO timeout returns to the
  // original value and OnRetransmissionTimeout is not called or reverted.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Zero(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(4 * kDefaultLength, manager_.GetBytesInFlight());

  // Wait 2RTTs from now for the RTO, since it's the max of the RTO time
  // and the TLP time.  In production, there would always be two TLP's first.
  // Since retransmission was spurious, smoothed_rtt_ is expired, and replaced
  // by the latest RTT sample of 500ms.
  expected_time = clock_.Now() + QuicTime::Delta::FromMilliseconds(1000);
  // Once we always base the timer on the right edge, leaving the older packets
  // in flight doesn't change the timeout.
  EXPECT_EQ(expected_time, manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionDelayMin) {
  SendDataPacket(1);
  // Provide a 1ms RTT sample.
  const_cast<RttStats*>(manager_.GetRttStats())
      ->UpdateRtt(QuicTime::Delta::FromMilliseconds(1), QuicTime::Delta::Zero(),
                  QuicTime::Zero());
  QuicTime::Delta delay = QuicTime::Delta::FromMilliseconds(200);

  // If the delay is smaller than the min, ensure it exponentially backs off
  // from the min.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(delay,
              QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));
    delay = delay + delay;
    EXPECT_CALL(notifier_, RetransmitFrames(_, _))
        .WillOnce(WithArgs<1>(Invoke([this, i](TransmissionType type) {
          RetransmitDataPacket(i + 2, type);
        })));
    manager_.OnRetransmissionTimeout();
  }
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionDelayMax) {
  SendDataPacket(1);
  // Provide a 60s RTT sample.
  const_cast<RttStats*>(manager_.GetRttStats())
      ->UpdateRtt(QuicTime::Delta::FromSeconds(60), QuicTime::Delta::Zero(),
                  QuicTime::Zero());

  EXPECT_EQ(QuicTime::Delta::FromSeconds(60),
            QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));
}

TEST_F(QuicSentPacketManagerTest, GetTransmissionDelayExponentialBackoff) {
  SendDataPacket(1);
  QuicTime::Delta delay = QuicTime::Delta::FromMilliseconds(500);

  // Delay should back off exponentially.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(delay,
              QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));
    delay = delay + delay;
    EXPECT_CALL(notifier_, RetransmitFrames(_, _))
        .WillOnce(WithArgs<1>(Invoke([this, i](TransmissionType type) {
          RetransmitDataPacket(i + 2, type);
        })));
    manager_.OnRetransmissionTimeout();
  }
}

TEST_F(QuicSentPacketManagerTest, RetransmissionDelay) {
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  const int64_t kRttMs = 250;
  const int64_t kDeviationMs = 5;

  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kRttMs),
                       QuicTime::Delta::Zero(), clock_.Now());

  // Initial value is to set the median deviation to half of the initial rtt,
  // the median in then multiplied by a factor of 4 and finally the smoothed rtt
  // is added which is the initial rtt.
  QuicTime::Delta expected_delay =
      QuicTime::Delta::FromMilliseconds(kRttMs + kRttMs / 2 * 4);
  EXPECT_EQ(expected_delay,
            QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));

  for (int i = 0; i < 100; ++i) {
    // Run to make sure that we converge.
    rtt_stats->UpdateRtt(
        QuicTime::Delta::FromMilliseconds(kRttMs + kDeviationMs),
        QuicTime::Delta::Zero(), clock_.Now());
    rtt_stats->UpdateRtt(
        QuicTime::Delta::FromMilliseconds(kRttMs - kDeviationMs),
        QuicTime::Delta::Zero(), clock_.Now());
  }
  expected_delay = QuicTime::Delta::FromMilliseconds(kRttMs + kDeviationMs * 4);

  EXPECT_NEAR(kRttMs, rtt_stats->smoothed_rtt().ToMilliseconds(), 1);
  EXPECT_NEAR(expected_delay.ToMilliseconds(),
              QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_)
                  .ToMilliseconds(),
              1);
}

TEST_F(QuicSentPacketManagerTest, GetLossDelay) {
  auto loss_algorithm = std::make_unique<MockLossAlgorithm>();
  QuicSentPacketManagerPeer::SetLossAlgorithm(&manager_, loss_algorithm.get());

  EXPECT_CALL(*loss_algorithm, GetLossTimeout())
      .WillRepeatedly(Return(QuicTime::Zero()));
  SendDataPacket(1);
  SendDataPacket(2);

  // Handle an ack which causes the loss algorithm to be evaluated and
  // set the loss timeout.
  ExpectAck(2);
  EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  QuicTime timeout(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_CALL(*loss_algorithm, GetLossTimeout())
      .WillRepeatedly(Return(timeout));
  EXPECT_EQ(timeout, manager_.GetRetransmissionTime());

  // Fire the retransmission timeout and ensure the loss detection algorithm
  // is invoked.
  EXPECT_CALL(*loss_algorithm, DetectLosses(_, _, _, _, _, _));
  manager_.OnRetransmissionTimeout();
}

TEST_F(QuicSentPacketManagerTest, NegotiateIetfLossDetectionFromOptions) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));

  QuicConfig config;
  QuicTagVector options;
  options.push_back(kILD0);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(3, QuicSentPacketManagerPeer::GetReorderingShift(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
}

TEST_F(QuicSentPacketManagerTest,
       NegotiateIetfLossDetectionOneFourthRttFromOptions) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));

  QuicConfig config;
  QuicTagVector options;
  options.push_back(kILD1);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
}

TEST_F(QuicSentPacketManagerTest,
       NegotiateIetfLossDetectionAdaptiveReorderingThreshold) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));

  QuicConfig config;
  QuicTagVector options;
  options.push_back(kILD2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(3, QuicSentPacketManagerPeer::GetReorderingShift(&manager_));
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
}

TEST_F(QuicSentPacketManagerTest,
       NegotiateIetfLossDetectionAdaptiveReorderingThreshold2) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));

  QuicConfig config;
  QuicTagVector options;
  options.push_back(kILD3);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
}

TEST_F(QuicSentPacketManagerTest,
       NegotiateIetfLossDetectionAdaptiveReorderingAndTimeThreshold) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_FALSE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));

  QuicConfig config;
  QuicTagVector options;
  options.push_back(kILD4);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(kDefaultLossDelayShift,
            QuicSentPacketManagerPeer::GetReorderingShift(&manager_));
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveReorderingThresholdEnabled(&manager_));
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::AdaptiveTimeThresholdEnabled(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateCongestionControlFromOptions) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kRENO);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kRenoBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                            ->GetCongestionControlType());

  options.clear();
  options.push_back(kTBBR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kBBR, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                      ->GetCongestionControlType());

  options.clear();
  options.push_back(kBYTE);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kCubicBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                             ->GetCongestionControlType());
  options.clear();
  options.push_back(kRENO);
  options.push_back(kBYTE);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kRenoBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                            ->GetCongestionControlType());
}

TEST_F(QuicSentPacketManagerTest, NegotiateClientCongestionControlFromOptions) {
  QuicConfig config;
  QuicTagVector options;

  // No change if the server receives client options.
  const SendAlgorithmInterface* mock_sender =
      QuicSentPacketManagerPeer::GetSendAlgorithm(manager_);
  options.push_back(kRENO);
  config.SetClientConnectionOptions(options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(mock_sender, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_));

  // Change the congestion control on the client with client options.
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kRenoBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                            ->GetCongestionControlType());

  options.clear();
  options.push_back(kTBBR);
  config.SetClientConnectionOptions(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kBBR, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                      ->GetCongestionControlType());

  options.clear();
  options.push_back(kBYTE);
  config.SetClientConnectionOptions(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kCubicBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                             ->GetCongestionControlType());

  options.clear();
  options.push_back(kRENO);
  options.push_back(kBYTE);
  config.SetClientConnectionOptions(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_EQ(kRenoBytes, QuicSentPacketManagerPeer::GetSendAlgorithm(manager_)
                            ->GetCongestionControlType());
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoMinTLPFromOptionsAtServer) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kMAD2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillOnce(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(config);
  // Set the initial RTT to 1us.
  QuicSentPacketManagerPeer::GetRttStats(&manager_)->set_initial_rtt(
      QuicTime::Delta::FromMicroseconds(1));
  // The TLP with fewer than 2 packets outstanding includes 1/2 min RTO(200ms).
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(100002),
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));

  // Send two packets, and the TLP should be 1ms.
  QuicTime::Delta expected_tlp_delay = QuicTime::Delta::FromMilliseconds(1);
  SendDataPacket(1);
  SendDataPacket(2);
  EXPECT_EQ(expected_tlp_delay,
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoMinTLPFromOptionsAtClient) {
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(kMAD2);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillOnce(Return(10 * kDefaultTCPMSS));
  manager_.SetFromConfig(client_config);
  // Set the initial RTT to 1us.
  QuicSentPacketManagerPeer::GetRttStats(&manager_)->set_initial_rtt(
      QuicTime::Delta::FromMicroseconds(1));
  // The TLP with fewer than 2 packets outstanding includes 1/2 min RTO(200ms).
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(100002),
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));
  // Send two packets, and the TLP should be 1ms.
  QuicTime::Delta expected_tlp_delay = QuicTime::Delta::FromMilliseconds(1);
  SendDataPacket(1);
  SendDataPacket(2);
  EXPECT_EQ(expected_tlp_delay,
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoMinRTOFromOptionsAtServer) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kMAD3);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(config);
  // Provide one RTT measurement, because otherwise we use the default of 500ms.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMicroseconds(1),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta expected_rto_delay = QuicTime::Delta::FromMilliseconds(1);
  EXPECT_EQ(expected_rto_delay,
            QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));
  // The TLP with fewer than 2 packets outstanding includes 1/2 min RTO(0ms).
  QuicTime::Delta expected_tlp_delay = QuicTime::Delta::FromMicroseconds(502);
  EXPECT_EQ(expected_tlp_delay,
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoMinRTOFromOptionsAtClient) {
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(kMAD3);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(client_config);
  // Provide one RTT measurement, because otherwise we use the default of 500ms.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMicroseconds(1),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta expected_rto_delay = QuicTime::Delta::FromMilliseconds(1);
  EXPECT_EQ(expected_rto_delay,
            QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_));
  // The TLP with fewer than 2 packets outstanding includes 1/2 min RTO(0ms).
  QuicTime::Delta expected_tlp_delay = QuicTime::Delta::FromMicroseconds(502);
  EXPECT_EQ(expected_tlp_delay,
            QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoTLPFromOptionsAtServer) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kNTLP);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(config);
  EXPECT_EQ(0u, QuicSentPacketManagerPeer::GetMaxTailLossProbes(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNoTLPFromOptionsAtClient) {
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(kNTLP);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(client_config);
  EXPECT_EQ(0u, QuicSentPacketManagerPeer::GetMaxTailLossProbes(&manager_));
}

TEST_F(QuicSentPacketManagerTest, Negotiate1TLPFromOptionsAtServer) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(k1TLP);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(config);
  EXPECT_EQ(1u, QuicSentPacketManagerPeer::GetMaxTailLossProbes(&manager_));
}

TEST_F(QuicSentPacketManagerTest, Negotiate1TLPFromOptionsAtClient) {
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(k1TLP);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(client_config);
  EXPECT_EQ(1u, QuicSentPacketManagerPeer::GetMaxTailLossProbes(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateTLPRttFromOptionsAtServer) {
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kTLPR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(config);
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::GetEnableHalfRttTailLossProbe(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateTLPRttFromOptionsAtClient) {
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(kTLPR);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(client_config);
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::GetEnableHalfRttTailLossProbe(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNewRTOFromOptionsAtServer) {
  EXPECT_FALSE(QuicSentPacketManagerPeer::GetUseNewRto(&manager_));
  QuicConfig config;
  QuicTagVector options;

  options.push_back(kNRTO);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(config);
  EXPECT_TRUE(QuicSentPacketManagerPeer::GetUseNewRto(&manager_));
}

TEST_F(QuicSentPacketManagerTest, NegotiateNewRTOFromOptionsAtClient) {
  EXPECT_FALSE(QuicSentPacketManagerPeer::GetUseNewRto(&manager_));
  QuicConfig client_config;
  QuicTagVector options;

  options.push_back(kNRTO);
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  client_config.SetConnectionOptionsToSend(options);
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  manager_.SetFromConfig(client_config);
  EXPECT_TRUE(QuicSentPacketManagerPeer::GetUseNewRto(&manager_));
}

TEST_F(QuicSentPacketManagerTest, UseInitialRoundTripTimeToSend) {
  QuicTime::Delta initial_rtt = QuicTime::Delta::FromMilliseconds(325);
  EXPECT_NE(initial_rtt, manager_.GetRttStats()->smoothed_rtt());

  QuicConfig config;
  config.SetInitialRoundTripTimeUsToSend(initial_rtt.ToMicroseconds());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.GetRttStats()->smoothed_rtt());
  EXPECT_EQ(initial_rtt, manager_.GetRttStats()->initial_rtt());
}

TEST_F(QuicSentPacketManagerTest, ResumeConnectionState) {
  // The sent packet manager should use the RTT from CachedNetworkParameters if
  // it is provided.
  const QuicTime::Delta kRtt = QuicTime::Delta::FromMilliseconds(1234);
  CachedNetworkParameters cached_network_params;
  cached_network_params.set_min_rtt_ms(kRtt.ToMilliseconds());

  SendAlgorithmInterface::NetworkParams params;
  params.bandwidth = QuicBandwidth::Zero();
  params.allow_cwnd_to_decrease = false;
  params.rtt = kRtt;

  EXPECT_CALL(*send_algorithm_, AdjustNetworkParameters(params));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .Times(testing::AnyNumber());
  manager_.ResumeConnectionState(cached_network_params, false);
  EXPECT_EQ(kRtt, manager_.GetRttStats()->initial_rtt());
}

TEST_F(QuicSentPacketManagerTest, ConnectionMigrationUnspecifiedChange) {
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  QuicTime::Delta default_init_rtt = rtt_stats->initial_rtt();
  rtt_stats->set_initial_rtt(default_init_rtt * 2);
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());

  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, 1);
  EXPECT_EQ(1u, manager_.GetConsecutiveRtoCount());
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, 2);
  EXPECT_EQ(2u, manager_.GetConsecutiveTlpCount());

  EXPECT_CALL(*send_algorithm_, OnConnectionMigration());
  manager_.OnConnectionMigration(IPV4_TO_IPV4_CHANGE);

  EXPECT_EQ(default_init_rtt, rtt_stats->initial_rtt());
  EXPECT_EQ(0u, manager_.GetConsecutiveRtoCount());
  EXPECT_EQ(0u, manager_.GetConsecutiveTlpCount());
}

TEST_F(QuicSentPacketManagerTest, ConnectionMigrationIPSubnetChange) {
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  QuicTime::Delta default_init_rtt = rtt_stats->initial_rtt();
  rtt_stats->set_initial_rtt(default_init_rtt * 2);
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());

  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, 1);
  EXPECT_EQ(1u, manager_.GetConsecutiveRtoCount());
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, 2);
  EXPECT_EQ(2u, manager_.GetConsecutiveTlpCount());

  manager_.OnConnectionMigration(IPV4_SUBNET_CHANGE);

  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());
  EXPECT_EQ(1u, manager_.GetConsecutiveRtoCount());
  EXPECT_EQ(2u, manager_.GetConsecutiveTlpCount());
}

TEST_F(QuicSentPacketManagerTest, ConnectionMigrationPortChange) {
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  QuicTime::Delta default_init_rtt = rtt_stats->initial_rtt();
  rtt_stats->set_initial_rtt(default_init_rtt * 2);
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());

  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, 1);
  EXPECT_EQ(1u, manager_.GetConsecutiveRtoCount());
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, 2);
  EXPECT_EQ(2u, manager_.GetConsecutiveTlpCount());

  manager_.OnConnectionMigration(PORT_CHANGE);

  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());
  EXPECT_EQ(1u, manager_.GetConsecutiveRtoCount());
  EXPECT_EQ(2u, manager_.GetConsecutiveTlpCount());
}

TEST_F(QuicSentPacketManagerTest, PathMtuIncreased) {
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, BytesInFlight(), QuicPacketNumber(1), _, _));
  SerializedPacket packet(QuicPacketNumber(1), PACKET_4BYTE_PACKET_NUMBER,
                          nullptr, kDefaultLength + 100, false, false);
  manager_.OnPacketSent(&packet, clock_.Now(), NOT_RETRANSMISSION,
                        HAS_RETRANSMITTABLE_DATA);

  // Ack the large packet and expect the path MTU to increase.
  ExpectAck(1);
  EXPECT_CALL(*network_change_visitor_,
              OnPathMtuIncreased(kDefaultLength + 100));
  QuicAckFrame ack_frame = InitAckFrame(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, OnAckRangeSlowPath) {
  // Send packets 1 - 20.
  for (size_t i = 1; i <= 20; ++i) {
    SendDataPacket(i);
  }
  // Ack [5, 7), [10, 12), [15, 17).
  uint64_t acked1[] = {5, 6, 10, 11, 15, 16};
  uint64_t lost1[] = {1, 2, 3, 4, 7, 8, 9, 12, 13};
  ExpectAcksAndLosses(true, acked1, QUICHE_ARRAYSIZE(acked1), lost1,
                      QUICHE_ARRAYSIZE(lost1));
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(AnyNumber());
  manager_.OnAckFrameStart(QuicPacketNumber(16), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(15), QuicPacketNumber(17));
  manager_.OnAckRange(QuicPacketNumber(10), QuicPacketNumber(12));
  manager_.OnAckRange(QuicPacketNumber(5), QuicPacketNumber(7));
  // Make sure empty range does not harm.
  manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // Ack [4, 8), [9, 13), [14, 21).
  uint64_t acked2[] = {4, 7, 9, 12, 14, 17, 18, 19, 20};
  ExpectAcksAndLosses(true, acked2, QUICHE_ARRAYSIZE(acked2), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(20), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(14), QuicPacketNumber(21));
  manager_.OnAckRange(QuicPacketNumber(9), QuicPacketNumber(13));
  manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(8));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, TolerateReneging) {
  // Send packets 1 - 20.
  for (size_t i = 1; i <= 20; ++i) {
    SendDataPacket(i);
  }
  // Ack [5, 7), [10, 12), [15, 17).
  uint64_t acked1[] = {5, 6, 10, 11, 15, 16};
  uint64_t lost1[] = {1, 2, 3, 4, 7, 8, 9, 12, 13};
  ExpectAcksAndLosses(true, acked1, QUICHE_ARRAYSIZE(acked1), lost1,
                      QUICHE_ARRAYSIZE(lost1));
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(AnyNumber());
  manager_.OnAckFrameStart(QuicPacketNumber(16), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(15), QuicPacketNumber(17));
  manager_.OnAckRange(QuicPacketNumber(10), QuicPacketNumber(12));
  manager_.OnAckRange(QuicPacketNumber(5), QuicPacketNumber(7));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // Making sure reneged ACK does not harm. Ack [4, 8), [9, 13).
  uint64_t acked2[] = {4, 7, 9, 12};
  ExpectAcksAndLosses(true, acked2, QUICHE_ARRAYSIZE(acked2), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(12), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(9), QuicPacketNumber(13));
  manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(8));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(QuicPacketNumber(16), manager_.GetLargestObserved());
}

TEST_F(QuicSentPacketManagerTest, MultiplePacketNumberSpaces) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  const QuicUnackedPacketMap* unacked_packets =
      QuicSentPacketManagerPeer::GetUnackedPacketMap(&manager_);
  EXPECT_FALSE(
      unacked_packets
          ->GetLargestSentRetransmittableOfPacketNumberSpace(INITIAL_DATA)
          .IsInitialized());
  EXPECT_FALSE(
      manager_.GetLargestAckedPacket(ENCRYPTION_INITIAL).IsInitialized());
  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  EXPECT_EQ(QuicPacketNumber(1),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                INITIAL_DATA));
  EXPECT_FALSE(
      unacked_packets
          ->GetLargestSentRetransmittableOfPacketNumberSpace(HANDSHAKE_DATA)
          .IsInitialized());
  // Ack packet 1.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(QuicPacketNumber(1),
            manager_.GetLargestAckedPacket(ENCRYPTION_INITIAL));
  EXPECT_FALSE(
      manager_.GetLargestAckedPacket(ENCRYPTION_HANDSHAKE).IsInitialized());
  // Send packets 2 and 3.
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  SendDataPacket(3, ENCRYPTION_HANDSHAKE);
  EXPECT_EQ(QuicPacketNumber(1),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                INITIAL_DATA));
  EXPECT_EQ(QuicPacketNumber(3),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                HANDSHAKE_DATA));
  EXPECT_FALSE(
      unacked_packets
          ->GetLargestSentRetransmittableOfPacketNumberSpace(APPLICATION_DATA)
          .IsInitialized());
  // Ack packet 2.
  ExpectAck(2);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_HANDSHAKE));
  EXPECT_EQ(QuicPacketNumber(2),
            manager_.GetLargestAckedPacket(ENCRYPTION_HANDSHAKE));
  EXPECT_FALSE(
      manager_.GetLargestAckedPacket(ENCRYPTION_ZERO_RTT).IsInitialized());
  // Ack packet 3.
  ExpectAck(3);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(2), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(3),
                                   ENCRYPTION_HANDSHAKE));
  EXPECT_EQ(QuicPacketNumber(3),
            manager_.GetLargestAckedPacket(ENCRYPTION_HANDSHAKE));
  EXPECT_FALSE(
      manager_.GetLargestAckedPacket(ENCRYPTION_ZERO_RTT).IsInitialized());
  // Send packets 4 and 5.
  SendDataPacket(4, ENCRYPTION_ZERO_RTT);
  SendDataPacket(5, ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(QuicPacketNumber(1),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                INITIAL_DATA));
  EXPECT_EQ(QuicPacketNumber(3),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                HANDSHAKE_DATA));
  EXPECT_EQ(QuicPacketNumber(5),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                APPLICATION_DATA));
  // Ack packet 5.
  ExpectAck(5);
  manager_.OnAckFrameStart(QuicPacketNumber(5), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(5), QuicPacketNumber(6));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(4),
                                   ENCRYPTION_FORWARD_SECURE));
  EXPECT_EQ(QuicPacketNumber(3),
            manager_.GetLargestAckedPacket(ENCRYPTION_HANDSHAKE));
  EXPECT_EQ(QuicPacketNumber(5),
            manager_.GetLargestAckedPacket(ENCRYPTION_ZERO_RTT));
  EXPECT_EQ(QuicPacketNumber(5),
            manager_.GetLargestAckedPacket(ENCRYPTION_FORWARD_SECURE));

  // Send packets 6 - 8.
  SendDataPacket(6, ENCRYPTION_FORWARD_SECURE);
  SendDataPacket(7, ENCRYPTION_FORWARD_SECURE);
  SendDataPacket(8, ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(QuicPacketNumber(1),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                INITIAL_DATA));
  EXPECT_EQ(QuicPacketNumber(3),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                HANDSHAKE_DATA));
  EXPECT_EQ(QuicPacketNumber(8),
            unacked_packets->GetLargestSentRetransmittableOfPacketNumberSpace(
                APPLICATION_DATA));
  // Ack all packets.
  uint64_t acked[] = {4, 6, 7, 8};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(8), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(9));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(5),
                                   ENCRYPTION_FORWARD_SECURE));
  EXPECT_EQ(QuicPacketNumber(3),
            manager_.GetLargestAckedPacket(ENCRYPTION_HANDSHAKE));
  EXPECT_EQ(QuicPacketNumber(8),
            manager_.GetLargestAckedPacket(ENCRYPTION_ZERO_RTT));
  EXPECT_EQ(QuicPacketNumber(8),
            manager_.GetLargestAckedPacket(ENCRYPTION_FORWARD_SECURE));
}

TEST_F(QuicSentPacketManagerTest, PacketsGetAckedInWrongPacketNumberSpace) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  // Send packets 2 and 3.
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  SendDataPacket(3, ENCRYPTION_HANDSHAKE);

  // ACK packets 2 and 3 in the wrong packet number space.
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, PacketsGetAckedInWrongPacketNumberSpace2) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  // Send packets 2 and 3.
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  SendDataPacket(3, ENCRYPTION_HANDSHAKE);

  // ACK packet 1 in the wrong packet number space.
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_HANDSHAKE));
}

TEST_F(QuicSentPacketManagerTest,
       ToleratePacketsGetAckedInWrongPacketNumberSpace) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  // Ack packet 1.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  // Send packets 2 and 3.
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  SendDataPacket(3, ENCRYPTION_HANDSHAKE);

  // Packet 1 gets acked in the wrong packet number space. Since packet 1 has
  // been acked in the correct packet number space, tolerate it.
  uint64_t acked[] = {2, 3};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(3), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(4));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_HANDSHAKE));
}

// Regression test for b/133771183.
TEST_F(QuicSentPacketManagerTest, PacketInLimbo) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);
  // Send SHLO.
  SendCryptoPacket(1);
  // Send data packet.
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Send Ack Packet.
  SendAckPacket(3, 1, ENCRYPTION_FORWARD_SECURE);
  // Retransmit SHLO.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(InvokeWithoutArgs([this]() { RetransmitCryptoPacket(4); }));
  manager_.OnRetransmissionTimeout();

  // Successfully decrypt a forward secure packet.
  manager_.SetHandshakeConfirmed();
  EXPECT_CALL(notifier_, HasUnackedCryptoData()).WillRepeatedly(Return(false));
  // Send Ack packet.
  SendAckPacket(5, 2, ENCRYPTION_FORWARD_SECURE);

  // Retransmission alarm fires.
  manager_.OnRetransmissionTimeout();
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(6, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeRetransmitTailLossProbe();

  // Received Ack of packets 1, 3 and 4.
  uint64_t acked[] = {1, 3, 4};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(5));
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));

  uint64_t acked2[] = {5, 6};
  uint64_t loss[] = {2};
  // Verify packet 2 is detected lost.
  EXPECT_CALL(notifier_, OnFrameLost(_)).Times(1);
  ExpectAcksAndLosses(true, acked2, QUICHE_ARRAYSIZE(acked2), loss,
                      QUICHE_ARRAYSIZE(loss));
  manager_.OnAckFrameStart(QuicPacketNumber(6), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(3), QuicPacketNumber(7));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(2),
                                   ENCRYPTION_INITIAL));
}

TEST_F(QuicSentPacketManagerTest, RtoFiresNoPacketToRetransmit) {
  // Send 10 packets.
  for (size_t i = 1; i <= 10; ++i) {
    SendDataPacket(i);
  }
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(11, type); })))
      .WillOnce(WithArgs<1>(Invoke(
          [this](TransmissionType type) { RetransmitDataPacket(12, type); })));
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(1u, stats_.rto_count);
  EXPECT_EQ(0u, manager_.pending_timer_transmission_count());

  // RTO fires again, but there is no packet to be RTO retransmitted.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(notifier_, RetransmitFrames(_, _)).Times(0);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(2u, stats_.rto_count);
  // Verify a credit is raised up.
  EXPECT_EQ(1u, manager_.pending_timer_transmission_count());
}

TEST_F(QuicSentPacketManagerTest, ComputingProbeTimeout) {
  EnablePto(k2PTO);
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set based on sent time of packet 2.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
  EXPECT_EQ(0u, stats_.pto_count);

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify two probe packets get sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(4, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify PTO period gets set to twice the current value.
  QuicTime sent_time = clock_.Now();
  EXPECT_EQ(sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Received ACK for packets 1 and 2.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_FORWARD_SECURE));
  expected_pto_delay =
      rtt_stats->SmoothedOrInitialRtt() +
      std::max(4 * rtt_stats->mean_deviation(),
               QuicTime::Delta::FromMilliseconds(1)) +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

  // Verify PTO is correctly re-armed based on sent time of packet 4.
  EXPECT_EQ(sent_time + expected_pto_delay, manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, SendOneProbePacket) {
  EnablePto(k1PTO);
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);

  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();
  // Verify PTO period is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));

  // Verify one probe packet gets sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
}

TEST_F(QuicSentPacketManagerTest, DisableHandshakeModeClient) {
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  manager_.EnableIetfPtoAndLossDetection();
  // Send CHLO.
  SendCryptoPacket(1);
  EXPECT_NE(QuicTime::Zero(), manager_.GetRetransmissionTime());
  // Ack packet 1.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(0u, manager_.GetBytesInFlight());
  // Verify retransmission timeout is not zero because handshake is not
  // confirmed although there is no in flight packet.
  EXPECT_NE(QuicTime::Zero(), manager_.GetRetransmissionTime());
  // Fire PTO.
  EXPECT_EQ(QuicSentPacketManager::PTO_MODE,
            manager_.OnRetransmissionTimeout());
}

TEST_F(QuicSentPacketManagerTest, DisableHandshakeModeServer) {
  manager_.EnableIetfPtoAndLossDetection();
  // Send SHLO.
  SendCryptoPacket(1);
  EXPECT_NE(QuicTime::Zero(), manager_.GetRetransmissionTime());
  // Ack packet 1.
  ExpectAck(1);
  manager_.OnAckFrameStart(QuicPacketNumber(1), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_INITIAL));
  EXPECT_EQ(0u, manager_.GetBytesInFlight());
  // Verify retransmission timeout is not set on server side because there is
  // nothing in flight.
  EXPECT_EQ(QuicTime::Zero(), manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, PtoTimeoutIncludesMaxAckDelay) {
  EnablePto(k1PTO);
  // Use PTOS and PTOA.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPTOA);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set and ack delay is included.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set based on sent time of packet 2 but ack delay is
  // not included as an immediate ACK is expected.
  expected_pto_delay = expected_pto_delay - QuicTime::Delta::FromMilliseconds(
                                                kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
  EXPECT_EQ(0u, stats_.pto_count);

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify 1 probe packets get sent and packet number gets skipped.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(4, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify PTO period gets set to twice the current value. Also, ack delay is
  // not included.
  QuicTime sent_time = clock_.Now();
  EXPECT_EQ(sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Received ACK for packets 1 and 2.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_FORWARD_SECURE));
  expected_pto_delay =
      rtt_stats->SmoothedOrInitialRtt() +
      std::max(4 * rtt_stats->mean_deviation(),
               QuicTime::Delta::FromMilliseconds(1)) +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

  // Verify PTO is correctly re-armed based on sent time of packet 4. Because of
  // PTOS turns out to be spurious, ACK delay is included.
  EXPECT_EQ(sent_time + expected_pto_delay, manager_.GetRetransmissionTime());

  // Received ACK for packets 4.
  ExpectAck(4);
  manager_.OnAckFrameStart(QuicPacketNumber(4), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(4), QuicPacketNumber(5));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(4),
                                   ENCRYPTION_FORWARD_SECURE));
  EXPECT_EQ(QuicTime::Zero(), manager_.GetRetransmissionTime());
  // Send more packets, such that peer will do ack decimation.
  std::vector<uint64_t> acked2;
  for (size_t i = 5; i <= 100; ++i) {
    SendDataPacket(i, ENCRYPTION_FORWARD_SECURE);
    acked2.push_back(i);
  }
  // Received ACK for all sent packets.
  ExpectAcksAndLosses(true, &acked2[0], acked2.size(), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(100), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(5), QuicPacketNumber(101));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(100),
                                   ENCRYPTION_FORWARD_SECURE));

  expected_pto_delay =
      rtt_stats->SmoothedOrInitialRtt() +
      std::max(4 * rtt_stats->mean_deviation(),
               QuicTime::Delta::FromMilliseconds(1)) +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  for (size_t i = 101; i < 110; i++) {
    SendDataPacket(i, ENCRYPTION_FORWARD_SECURE);
    // Verify PTO timeout includes ACK delay as there are less than 10 packets
    // outstanding.
    EXPECT_EQ(clock_.Now() + expected_pto_delay,
              manager_.GetRetransmissionTime());
  }
  expected_pto_delay = expected_pto_delay - QuicTime::Delta::FromMilliseconds(
                                                kDefaultDelayedAckTimeMs);
  SendDataPacket(110, ENCRYPTION_FORWARD_SECURE);
  // Verify ACK delay is excluded.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, StartExponentialBackoffSince2ndPto) {
  EnablePto(k2PTO);
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPEB2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set based on sent time of packet 2.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
  EXPECT_EQ(0u, stats_.pto_count);

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify two probe packets get sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(4, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify no exponential backoff.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke 2nd PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(2u, stats_.pto_count);

  // Verify two probe packets get sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(5, type, ENCRYPTION_FORWARD_SECURE);
      })))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(6, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify still no exponential backoff.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke 3rd PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(3u, stats_.pto_count);

  // Verify two probe packets get sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(7, type, ENCRYPTION_FORWARD_SECURE);
      })))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(8, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify exponential backoff starts.
  EXPECT_EQ(clock_.Now() + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Invoke 4th PTO.
  clock_.AdvanceTime(expected_pto_delay * 2);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(4u, stats_.pto_count);

  // Verify two probe packets get sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(9, type, ENCRYPTION_FORWARD_SECURE);
      })))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(10, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify exponential backoff continues.
  EXPECT_EQ(clock_.Now() + expected_pto_delay * 4,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, PtoTimeoutRttVarMultiple) {
  EnablePto(k1PTO);
  // Use 2 * rttvar
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPVS1);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set based on 2 times rtt var.
  QuicTime::Delta expected_pto_delay =
      srtt + 2 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

// Regression test for b/143962153
TEST_F(QuicSentPacketManagerTest, RtoNotInFlightPacket) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);
  // Send SHLO.
  QuicStreamFrame crypto_frame(1, false, 0, quiche::QuicheStringPiece());
  SendCryptoPacket(1);
  // Send data packet.
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);

  // Successfully decrypt a forward secure packet.
  EXPECT_CALL(notifier_, OnFrameAcked(_, _, _)).Times(1);
  manager_.SetHandshakeConfirmed();

  // 1st TLP.
  manager_.OnRetransmissionTimeout();
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeRetransmitTailLossProbe();

  // 2nd TLP.
  manager_.OnRetransmissionTimeout();
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(4, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeRetransmitTailLossProbe();

  // RTO retransmits SHLO although it is not in flight.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<0>(Invoke([&crypto_frame](const QuicFrames& frames) {
        EXPECT_EQ(1u, frames.size());
        EXPECT_NE(crypto_frame, frames[0].stream_frame);
      })));
  manager_.OnRetransmissionTimeout();
}

TEST_F(QuicSentPacketManagerTest, Aggressive1Pto) {
  EnablePto(k1PTO);
  // Let the first PTO be aggressive.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPAG1);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();
  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay = 2 * srtt;
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify 1 probe packets get sent and packet number gets skipped.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();

  // Verify PTO period gets set correctly.
  QuicTime sent_time = clock_.Now();
  expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, Aggressive2Ptos) {
  EnablePto(k1PTO);
  // Let the first PTO be aggressive.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPAG2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();
  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay = 2 * srtt;
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify 1 probe packets get sent and packet number gets skipped.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();

  // Verify PTO period gets set correctly.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Invoke 2nd PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(2u, stats_.pto_count);

  // Verify 1 probe packets get sent and packet number gets skipped.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(5, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

  // Verify PTO period gets set correctly.
  EXPECT_EQ(clock_.Now() + expected_pto_delay * 4,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, IW10ForUpAndDown) {
  SetQuicReloadableFlag(quic_bbr_mitigate_overly_large_bandwidth_sample, true);
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kBWS5);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*send_algorithm_, SetInitialCongestionWindowInPackets(10));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_EQ(10u, manager_.initial_congestion_window());
}

TEST_F(QuicSentPacketManagerTest, ClientMultiplePacketNumberSpacePtoTimeout) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  EnablePto(k1PTO);
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);

  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard initial key and send packet 2 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.NeuterUnencryptedPackets();

  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(true));
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  // Verify PTO is correctly set based on sent time of packet 2.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  // Verify probe packet gets sent.
  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_HANDSHAKE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify PTO period gets set to twice the current value.
  const QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet3_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Send packet 4 in application data with 0-RTT.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(4, ENCRYPTION_ZERO_RTT);
  const QuicTime packet4_sent_time = clock_.Now();
  // Verify PTO timeout is still based on packet 3.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Send packet 5 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(5, ENCRYPTION_HANDSHAKE);
  const QuicTime packet5_sent_time = clock_.Now();
  // Verify PTO timeout is now based on packet 5 because packet 4 should be
  // ignored.
  EXPECT_EQ(clock_.Now() + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Send packet 6 in 1-RTT.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(6, ENCRYPTION_FORWARD_SECURE);
  const QuicTime packet6_sent_time = clock_.Now();
  // Verify PTO timeout is now based on packet 5.
  EXPECT_EQ(packet5_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Send packet 7 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(7, ENCRYPTION_HANDSHAKE);
  // Verify PTO timeout is now based on packet 6.
  EXPECT_EQ(packet6_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Neuter handshake key.
  manager_.SetHandshakeConfirmed();
  // Forward progress has been made, verify PTO counter gets reset. PTO timeout
  // is armed by left edge.
  EXPECT_EQ(packet4_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, ServerMultiplePacketNumberSpacePtoTimeout) {
  manager_.EnableMultiplePacketNumberSpacesSupport();
  EnablePto(k1PTO);
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  const QuicTime packet1_sent_time = clock_.Now();
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 2 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  const QuicTime packet2_sent_time = clock_.Now();
  // Verify PTO timeout is still based on packet 1.
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard initial keys.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.NeuterUnencryptedPackets();

  // Send packet 3 in 1-RTT.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(3, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO timeout is based on packet 2.
  const QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet2_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 4 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(4, ENCRYPTION_HANDSHAKE);
  // Verify PTO timeout is based on packet 4 as application data is ignored.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard handshake keys.
  manager_.SetHandshakeConfirmed();
  // Verify PTO timeout is now based on packet 3 as handshake is
  // complete/confirmed.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, ComputingProbeTimeoutByLeftEdge) {
  SetQuicReloadableFlag(quic_arm_pto_with_earliest_sent_time, true);
  EnablePto(k1PTO);
  // Use PTOS and PLE1.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPLE1);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  const QuicTime packet1_sent_time = clock_.Now();
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is still based on packet 1.
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
  EXPECT_EQ(0u, stats_.pto_count);

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify PTO period gets set to twice the current value and based on packet3.
  QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet3_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Received ACK for packets 1 and 2.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_FORWARD_SECURE));
  expected_pto_delay =
      rtt_stats->SmoothedOrInitialRtt() +
      std::max(4 * rtt_stats->mean_deviation(),
               QuicTime::Delta::FromMilliseconds(1)) +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

  // Verify PTO is correctly re-armed based on sent time of packet 4.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, ComputingProbeTimeoutByLeftEdge2) {
  SetQuicReloadableFlag(quic_arm_pto_with_earliest_sent_time, true);
  EnablePto(k1PTO);
  // Use PTOS and PLE2.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPLE2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  const QuicTime packet1_sent_time = clock_.Now();
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Sent a packet 10ms before PTO expiring.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(
      expected_pto_delay.ToMilliseconds() - 10));
  SendDataPacket(2, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO expands to packet 2 sent time + 1.5 * srtt.
  expected_pto_delay = 1.5 * rtt_stats->smoothed_rtt();
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
  EXPECT_EQ(0u, stats_.pto_count);

  // Invoke PTO.
  clock_.AdvanceTime(expected_pto_delay);
  manager_.OnRetransmissionTimeout();
  EXPECT_EQ(QuicTime::Delta::Zero(), manager_.TimeUntilSend(clock_.Now()));
  EXPECT_EQ(1u, stats_.pto_count);

  EXPECT_CALL(notifier_, RetransmitFrames(_, _))
      .WillOnce(WithArgs<1>(Invoke([this](TransmissionType type) {
        RetransmitDataPacket(3, type, ENCRYPTION_FORWARD_SECURE);
      })));
  manager_.MaybeSendProbePackets();
  // Verify PTO period gets set to twice the expected value and based on
  // packet3 (right edge).
  expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet3_sent_time + expected_pto_delay * 2,
            manager_.GetRetransmissionTime());

  // Received ACK for packets 1 and 2.
  uint64_t acked[] = {1, 2};
  ExpectAcksAndLosses(true, acked, QUICHE_ARRAYSIZE(acked), nullptr, 0);
  manager_.OnAckFrameStart(QuicPacketNumber(2), QuicTime::Delta::Infinite(),
                           clock_.Now());
  manager_.OnAckRange(QuicPacketNumber(1), QuicPacketNumber(3));
  EXPECT_EQ(PACKETS_NEWLY_ACKED,
            manager_.OnAckFrameEnd(clock_.Now(), QuicPacketNumber(1),
                                   ENCRYPTION_FORWARD_SECURE));
  expected_pto_delay =
      rtt_stats->SmoothedOrInitialRtt() +
      std::max(4 * rtt_stats->mean_deviation(),
               QuicTime::Delta::FromMilliseconds(1)) +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

  // Verify PTO is correctly re-armed based on sent time of packet 3 (left
  // edge).
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, ComputingProbeTimeoutUsingStandardDeviation) {
  SetQuicReloadableFlag(quic_use_standard_deviation_for_pto, true);
  EnablePto(k1PTO);
  // Use PTOS and PSDA.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPSDA);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(50),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(50),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(75),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  SendDataPacket(1, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO is correctly set using standard deviation.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->GetStandardOrMeanDeviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest,
       ComputingProbeTimeoutByLeftEdgeMultiplePacketNumberSpaces) {
  SetQuicReloadableFlag(quic_arm_pto_with_earliest_sent_time, true);
  manager_.EnableMultiplePacketNumberSpacesSupport();
  EnablePto(k1PTO);
  // Use PTOS and PLE1.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPLE1);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  const QuicTime packet1_sent_time = clock_.Now();
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 2 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  const QuicTime packet2_sent_time = clock_.Now();
  // Verify PTO timeout is still based on packet 1.
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard initial keys.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.NeuterUnencryptedPackets();

  // Send packet 3 in 1-RTT.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(3, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO timeout is based on packet 2.
  const QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet2_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 4 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(4, ENCRYPTION_HANDSHAKE);
  // Verify PTO timeout is based on packet 4 as application data is ignored.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard handshake keys.
  manager_.SetHandshakeConfirmed();
  // Verify PTO timeout is now based on packet 3 as handshake is
  // complete/confirmed.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(5, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO timeout is still based on packet 3.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest,
       ComputingProbeTimeoutByLeftEdge2MultiplePacketNumberSpaces) {
  SetQuicReloadableFlag(quic_arm_pto_with_earliest_sent_time, true);
  manager_.EnableMultiplePacketNumberSpacesSupport();
  EnablePto(k1PTO);
  // Use PTOS and PLE2.
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kPTOS);
  options.push_back(kPLE2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);
  EXPECT_TRUE(manager_.skip_packet_number_for_pto());
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, PacingRate(_))
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(10 * kDefaultTCPMSS));
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  QuicTime::Delta srtt = rtt_stats->smoothed_rtt();

  // Send packet 1.
  SendDataPacket(1, ENCRYPTION_INITIAL);
  const QuicTime packet1_sent_time = clock_.Now();
  // Verify PTO is correctly set.
  QuicTime::Delta expected_pto_delay =
      srtt + 4 * rtt_stats->mean_deviation() +
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 2 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(2, ENCRYPTION_HANDSHAKE);
  const QuicTime packet2_sent_time = clock_.Now();
  // Verify PTO timeout is still based on packet 1.
  EXPECT_EQ(packet1_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard initial keys.
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  manager_.NeuterUnencryptedPackets();

  // Send packet 3 in 1-RTT.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(3, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO timeout is based on packet 2.
  const QuicTime packet3_sent_time = clock_.Now();
  EXPECT_EQ(packet2_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 4 in handshake.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  SendDataPacket(4, ENCRYPTION_HANDSHAKE);
  // Verify PTO timeout is based on packet 4 as application data is ignored.
  EXPECT_EQ(clock_.Now() + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Discard handshake keys.
  manager_.SetHandshakeConfirmed();
  // Verify PTO timeout is now based on packet 3 as handshake is
  // complete/confirmed.
  EXPECT_EQ(packet3_sent_time + expected_pto_delay,
            manager_.GetRetransmissionTime());

  // Send packet 5 10ms before PTO expiring.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(
      expected_pto_delay.ToMilliseconds() - 10));
  SendDataPacket(5, ENCRYPTION_FORWARD_SECURE);
  // Verify PTO timeout expands to packet 5 sent time + 1.5 * srtt.
  EXPECT_EQ(clock_.Now() + 1.5 * rtt_stats->smoothed_rtt(),
            manager_.GetRetransmissionTime());
}

TEST_F(QuicSentPacketManagerTest, SetHandshakeConfirmed) {
  QuicSentPacketManagerPeer::SetPerspective(&manager_, Perspective::IS_CLIENT);
  manager_.EnableMultiplePacketNumberSpacesSupport();

  SendDataPacket(1, ENCRYPTION_INITIAL);

  SendDataPacket(2, ENCRYPTION_HANDSHAKE);

  EXPECT_CALL(notifier_, OnFrameAcked(_, _, _))
      .WillOnce(
          Invoke([](const QuicFrame& /*frame*/, QuicTime::Delta ack_delay_time,
                    QuicTime receive_timestamp) {
            EXPECT_TRUE(ack_delay_time.IsZero());
            EXPECT_EQ(receive_timestamp, QuicTime::Zero());
            return true;
          }));

  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_CALL(*send_algorithm_, OnPacketNeutered(QuicPacketNumber(2)))
        .Times(1);
  }
  manager_.SetHandshakeConfirmed();
}

// Regresstion test for b/148841700.
TEST_F(QuicSentPacketManagerTest, NeuterUnencryptedPackets) {
  SendCryptoPacket(1);
  SendPingPacket(2, ENCRYPTION_INITIAL);
  // Crypto data has been discarded but ping does not.
  EXPECT_CALL(notifier_, OnFrameAcked(_, _, _))
      .Times(2)
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(notifier_, IsFrameOutstanding(_)).WillRepeatedly(Return(false));
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_CALL(*send_algorithm_, OnPacketNeutered(QuicPacketNumber(1)))
        .Times(1);
  }
  manager_.NeuterUnencryptedPackets();
}

TEST_F(QuicSentPacketManagerTest, NoPacketThresholdDetectionForRuntPackets) {
  EXPECT_TRUE(
      QuicSentPacketManagerPeer::UsePacketThresholdForRuntPackets(&manager_));

  SetQuicReloadableFlag(quic_skip_packet_threshold_loss_detection_with_runt,
                        true);
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kRUNT);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(*network_change_visitor_, OnCongestionChange());
  manager_.SetFromConfig(config);

  EXPECT_FALSE(
      QuicSentPacketManagerPeer::UsePacketThresholdForRuntPackets(&manager_));
}

TEST_F(QuicSentPacketManagerTest, GetPathDegradingDelay) {
  QuicSentPacketManagerPeer::SetMaxTailLossProbes(&manager_, 2);
  // Before RTT sample is available.
  // 2 TLPs + 2 RTOs.
  QuicTime::Delta expected_delay = QuicTime::Delta::Zero();
  for (size_t i = 0; i < 2; ++i) {
    QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, i);
    expected_delay =
        expected_delay +
        QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_);
  }
  for (size_t i = 0; i < 2; ++i) {
    QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, i);
    expected_delay =
        expected_delay +
        QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_);
  }
  EXPECT_EQ(expected_delay, manager_.GetPathDegradingDelay());

  expected_delay = QuicTime::Delta::Zero();
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, 0);
  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, 0);

  // After RTT sample is available.
  RttStats* rtt_stats = const_cast<RttStats*>(manager_.GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  // 2 TLPs + 2 RTOs.
  for (size_t i = 0; i < 2; ++i) {
    QuicSentPacketManagerPeer::SetConsecutiveTlpCount(&manager_, i);
    expected_delay =
        expected_delay +
        QuicSentPacketManagerPeer::GetTailLossProbeDelay(&manager_);
  }
  for (size_t i = 0; i < 2; ++i) {
    QuicSentPacketManagerPeer::SetConsecutiveRtoCount(&manager_, i);
    expected_delay =
        expected_delay +
        QuicSentPacketManagerPeer::GetRetransmissionDelay(&manager_);
  }
  EXPECT_EQ(expected_delay, manager_.GetPathDegradingDelay());
}

}  // namespace
}  // namespace test
}  // namespace quic
