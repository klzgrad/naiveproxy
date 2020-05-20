// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/uber_received_packet_manager.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {

class UberReceivedPacketManagerPeer {
 public:
  static void SetAckMode(UberReceivedPacketManager* manager, AckMode ack_mode) {
    for (auto& received_packet_manager : manager->received_packet_managers_) {
      received_packet_manager.ack_mode_ = ack_mode;
    }
  }

  static void SetFastAckAfterQuiescence(UberReceivedPacketManager* manager,
                                        bool fast_ack_after_quiescence) {
    for (auto& received_packet_manager : manager->received_packet_managers_) {
      received_packet_manager.fast_ack_after_quiescence_ =
          fast_ack_after_quiescence;
    }
  }

  static void SetAckDecimationDelay(UberReceivedPacketManager* manager,
                                    float ack_decimation_delay) {
    for (auto& received_packet_manager : manager->received_packet_managers_) {
      received_packet_manager.ack_decimation_delay_ = ack_decimation_delay;
    }
  }
};

namespace {

const bool kInstigateAck = true;
const QuicTime::Delta kMinRttMs = QuicTime::Delta::FromMilliseconds(40);
const QuicTime::Delta kDelayedAckTime =
    QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);

class UberReceivedPacketManagerTest : public QuicTest {
 protected:
  UberReceivedPacketManagerTest() {
    manager_ = std::make_unique<UberReceivedPacketManager>(&stats_);
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
    rtt_stats_.UpdateRtt(kMinRttMs, QuicTime::Delta::Zero(), QuicTime::Zero());
    manager_->set_save_timestamps(true);
  }

  void RecordPacketReceipt(uint64_t packet_number) {
    RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, packet_number);
  }

  void RecordPacketReceipt(uint64_t packet_number, QuicTime receipt_time) {
    RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, packet_number, receipt_time);
  }

  void RecordPacketReceipt(EncryptionLevel decrypted_packet_level,
                           uint64_t packet_number) {
    RecordPacketReceipt(decrypted_packet_level, packet_number,
                        QuicTime::Zero());
  }

  void RecordPacketReceipt(EncryptionLevel decrypted_packet_level,
                           uint64_t packet_number,
                           QuicTime receipt_time) {
    QuicPacketHeader header;
    header.packet_number = QuicPacketNumber(packet_number);
    manager_->RecordPacketReceived(decrypted_packet_level, header,
                                   receipt_time);
  }

  bool HasPendingAck() {
    if (!manager_->supports_multiple_packet_number_spaces()) {
      return manager_->GetAckTimeout(APPLICATION_DATA).IsInitialized();
    }
    return manager_->GetEarliestAckTimeout().IsInitialized();
  }

  void MaybeUpdateAckTimeout(bool should_last_packet_instigate_acks,
                             uint64_t last_received_packet_number) {
    MaybeUpdateAckTimeout(should_last_packet_instigate_acks,
                          ENCRYPTION_FORWARD_SECURE,
                          last_received_packet_number);
  }

  void MaybeUpdateAckTimeout(bool should_last_packet_instigate_acks,
                             EncryptionLevel decrypted_packet_level,
                             uint64_t last_received_packet_number) {
    manager_->MaybeUpdateAckTimeout(
        should_last_packet_instigate_acks, decrypted_packet_level,
        QuicPacketNumber(last_received_packet_number), clock_.ApproximateNow(),
        clock_.ApproximateNow(), &rtt_stats_);
  }

  void CheckAckTimeout(QuicTime time) {
    DCHECK(HasPendingAck());
    if (!manager_->supports_multiple_packet_number_spaces()) {
      DCHECK(manager_->GetAckTimeout(APPLICATION_DATA) == time);
      if (time <= clock_.ApproximateNow()) {
        // ACK timeout expires, send an ACK.
        manager_->ResetAckStates(ENCRYPTION_FORWARD_SECURE);
        DCHECK(!HasPendingAck());
      }
      return;
    }
    DCHECK(manager_->GetEarliestAckTimeout() == time);
    // Send all expired ACKs.
    for (int8_t i = INITIAL_DATA; i < NUM_PACKET_NUMBER_SPACES; ++i) {
      const QuicTime ack_timeout =
          manager_->GetAckTimeout(static_cast<PacketNumberSpace>(i));
      if (!ack_timeout.IsInitialized() ||
          ack_timeout > clock_.ApproximateNow()) {
        continue;
      }
      manager_->ResetAckStates(
          QuicUtils::GetEncryptionLevel(static_cast<PacketNumberSpace>(i)));
    }
  }

  MockClock clock_;
  RttStats rtt_stats_;
  QuicConnectionStats stats_;
  std::unique_ptr<UberReceivedPacketManager> manager_;
};

TEST_F(UberReceivedPacketManagerTest, DontWaitForPacketsBefore) {
  EXPECT_TRUE(manager_->IsAckFrameEmpty(APPLICATION_DATA));
  RecordPacketReceipt(2);
  EXPECT_FALSE(manager_->IsAckFrameEmpty(APPLICATION_DATA));
  RecordPacketReceipt(7);
  EXPECT_TRUE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                         QuicPacketNumber(3u)));
  EXPECT_TRUE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                         QuicPacketNumber(6u)));
  manager_->DontWaitForPacketsBefore(ENCRYPTION_FORWARD_SECURE,
                                     QuicPacketNumber(4));
  EXPECT_FALSE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                          QuicPacketNumber(3u)));
  EXPECT_TRUE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                         QuicPacketNumber(6u)));
}

TEST_F(UberReceivedPacketManagerTest, GetUpdatedAckFrame) {
  QuicTime two_ms = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(2);
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  RecordPacketReceipt(2, two_ms);
  EXPECT_TRUE(manager_->IsAckFrameUpdated());

  QuicFrame ack =
      manager_->GetUpdatedAckFrame(APPLICATION_DATA, QuicTime::Zero());
  manager_->ResetAckStates(ENCRYPTION_FORWARD_SECURE);
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  // When UpdateReceivedPacketInfo with a time earlier than the time of the
  // largest observed packet, make sure that the delta is 0, not negative.
  EXPECT_EQ(QuicTime::Delta::Zero(), ack.ack_frame->ack_delay_time);
  EXPECT_EQ(1u, ack.ack_frame->received_packet_times.size());

  QuicTime four_ms = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(4);
  ack = manager_->GetUpdatedAckFrame(APPLICATION_DATA, four_ms);
  manager_->ResetAckStates(ENCRYPTION_FORWARD_SECURE);
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  // When UpdateReceivedPacketInfo after not having received a new packet,
  // the delta should still be accurate.
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(2),
            ack.ack_frame->ack_delay_time);
  // And received packet times won't have change.
  EXPECT_EQ(1u, ack.ack_frame->received_packet_times.size());

  RecordPacketReceipt(999, two_ms);
  RecordPacketReceipt(4, two_ms);
  RecordPacketReceipt(1000, two_ms);
  EXPECT_TRUE(manager_->IsAckFrameUpdated());
  ack = manager_->GetUpdatedAckFrame(APPLICATION_DATA, two_ms);
  manager_->ResetAckStates(ENCRYPTION_FORWARD_SECURE);
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  // UpdateReceivedPacketInfo should discard any times which can't be
  // expressed on the wire.
  EXPECT_EQ(2u, ack.ack_frame->received_packet_times.size());
}

TEST_F(UberReceivedPacketManagerTest, UpdateReceivedConnectionStats) {
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  RecordPacketReceipt(1);
  EXPECT_TRUE(manager_->IsAckFrameUpdated());
  RecordPacketReceipt(6);
  RecordPacketReceipt(2,
                      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1));

  EXPECT_EQ(4u, stats_.max_sequence_reordering);
  EXPECT_EQ(1000, stats_.max_time_reordering_us);
  EXPECT_EQ(1u, stats_.packets_reordered);
}

TEST_F(UberReceivedPacketManagerTest, LimitAckRanges) {
  manager_->set_max_ack_ranges(10);
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  for (int i = 0; i < 100; ++i) {
    RecordPacketReceipt(1 + 2 * i);
    EXPECT_TRUE(manager_->IsAckFrameUpdated());
    manager_->GetUpdatedAckFrame(APPLICATION_DATA, QuicTime::Zero());
    EXPECT_GE(10u, manager_->ack_frame().packets.NumIntervals());
    EXPECT_EQ(QuicPacketNumber(1u + 2 * i),
              manager_->ack_frame().packets.Max());
    for (int j = 0; j < std::min(10, i + 1); ++j) {
      ASSERT_GE(i, j);
      EXPECT_TRUE(manager_->ack_frame().packets.Contains(
          QuicPacketNumber(1 + (i - j) * 2)));
      if (i > j) {
        EXPECT_FALSE(manager_->ack_frame().packets.Contains(
            QuicPacketNumber((i - j) * 2)));
      }
    }
  }
}

TEST_F(UberReceivedPacketManagerTest, IgnoreOutOfOrderTimestamps) {
  EXPECT_FALSE(manager_->IsAckFrameUpdated());
  RecordPacketReceipt(1, QuicTime::Zero());
  EXPECT_TRUE(manager_->IsAckFrameUpdated());
  EXPECT_EQ(1u, manager_->ack_frame().received_packet_times.size());
  RecordPacketReceipt(2,
                      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1));
  EXPECT_EQ(2u, manager_->ack_frame().received_packet_times.size());
  RecordPacketReceipt(3, QuicTime::Zero());
  EXPECT_EQ(2u, manager_->ack_frame().received_packet_times.size());
}

TEST_F(UberReceivedPacketManagerTest, OutOfOrderReceiptCausesAckSent) {
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(3, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 3);
  // Delayed ack is scheduled.
  CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);

  RecordPacketReceipt(2, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 2);
  CheckAckTimeout(clock_.ApproximateNow());

  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 1);
  // Should ack immediately, since this fills the last hole.
  CheckAckTimeout(clock_.ApproximateNow());

  RecordPacketReceipt(4, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 4);
  // Delayed ack is scheduled.
  CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
}

TEST_F(UberReceivedPacketManagerTest, OutOfOrderAckReceiptCausesNoAck) {
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(2, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 2);
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 1);
  EXPECT_FALSE(HasPendingAck());
}

TEST_F(UberReceivedPacketManagerTest, AckReceiptCausesAckSend) {
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 1);
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(2, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 2);
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(3, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 3);
  // Delayed ack is scheduled.
  CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
  clock_.AdvanceTime(kDelayedAckTime);
  CheckAckTimeout(clock_.ApproximateNow());

  RecordPacketReceipt(4, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 4);
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(5, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(!kInstigateAck, 5);
  EXPECT_FALSE(HasPendingAck());
}

TEST_F(UberReceivedPacketManagerTest, AckSentEveryNthPacket) {
  EXPECT_FALSE(HasPendingAck());
  manager_->set_ack_frequency_before_ack_decimation(3);

  // Receives packets 1 - 39.
  for (size_t i = 1; i <= 39; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 3 == 0) {
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }
}

TEST_F(UberReceivedPacketManagerTest, AckDecimationReducesAcks) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(),
                                            ACK_DECIMATION_WITH_REORDERING);

  // Start ack decimation from 10th packet.
  manager_->set_min_received_before_ack_decimation(10);

  // Receives packets 1 - 29.
  for (size_t i = 1; i <= 29; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i <= 10) {
      // For packets 1-10, ack every 2 packets.
      if (i % 2 == 0) {
        CheckAckTimeout(clock_.ApproximateNow());
      } else {
        CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
      }
      continue;
    }
    // ack at 20.
    if (i == 20) {
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kMinRttMs * 0.25);
    }
  }

  // We now receive the 30th packet, and so we send an ack.
  RecordPacketReceipt(30, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 30);
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest, SendDelayedAfterQuiescence) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetFastAckAfterQuiescence(manager_.get(),
                                                           true);
  // The beginning of the connection counts as quiescence.
  QuicTime ack_time =
      clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(1);

  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 1);
  CheckAckTimeout(ack_time);
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  CheckAckTimeout(clock_.ApproximateNow());

  // Process another packet immediately after sending the ack and expect the
  // ack timeout to be set delayed ack time in the future.
  ack_time = clock_.ApproximateNow() + kDelayedAckTime;
  RecordPacketReceipt(2, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 2);
  CheckAckTimeout(ack_time);
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(kDelayedAckTime);
  CheckAckTimeout(clock_.ApproximateNow());

  // Wait 1 second and enesure the ack timeout is set to 1ms in the future.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  ack_time = clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(1);
  RecordPacketReceipt(3, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 3);
  CheckAckTimeout(ack_time);
}

TEST_F(UberReceivedPacketManagerTest, SendDelayedMaxAckDelay) {
  EXPECT_FALSE(HasPendingAck());
  QuicTime::Delta max_ack_delay = QuicTime::Delta::FromMilliseconds(100);
  manager_->set_max_ack_delay(max_ack_delay);
  QuicTime ack_time = clock_.ApproximateNow() + max_ack_delay;

  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 1);
  CheckAckTimeout(ack_time);
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(max_ack_delay);
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest, SendDelayedAckDecimation) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(), ACK_DECIMATION);
  // The ack time should be based on min_rtt * 1/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.25;

  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (uint64_t i = 1; i < 10; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest,
       SendDelayedAckAckDecimationAfterQuiescence) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(), ACK_DECIMATION);
  UberReceivedPacketManagerPeer::SetFastAckAfterQuiescence(manager_.get(),
                                                           true);
  // The beginning of the connection counts as quiescence.
  QuicTime ack_time =
      clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(1);
  RecordPacketReceipt(1, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 1);
  CheckAckTimeout(ack_time);
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  CheckAckTimeout(clock_.ApproximateNow());

  // Process another packet immedately after sending the ack and expect the
  // ack timeout to be set delayed ack time in the future.
  ack_time = clock_.ApproximateNow() + kDelayedAckTime;
  RecordPacketReceipt(2, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 2);
  CheckAckTimeout(ack_time);
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(kDelayedAckTime);
  CheckAckTimeout(clock_.ApproximateNow());

  // Wait 1 second and enesure the ack timeout is set to 1ms in the future.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  ack_time = clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(1);
  RecordPacketReceipt(3, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, 3);
  CheckAckTimeout(ack_time);
  // Process enough packets to get into ack decimation behavior.
  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  ack_time = clock_.ApproximateNow() + kMinRttMs * 0.25;
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 4; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }
  EXPECT_FALSE(HasPendingAck());
  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (uint64_t i = 1; i < 10; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(clock_.ApproximateNow());

  // Wait 1 second and enesure the ack timeout is set to 1ms in the future.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  ack_time = clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(1);
  RecordPacketReceipt(kFirstDecimatedPacket + 10, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 10);
  CheckAckTimeout(ack_time);
}

TEST_F(UberReceivedPacketManagerTest,
       SendDelayedAckDecimationUnlimitedAggregation) {
  EXPECT_FALSE(HasPendingAck());
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kACKD);
  // No limit on the number of packets received before sending an ack.
  connection_options.push_back(kAKDU);
  config.SetConnectionOptionsToSend(connection_options);
  manager_->SetFromConfig(config, Perspective::IS_CLIENT);

  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.25;

  // Process all the initial packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  // 18 packets will not cause an ack to be sent.  19 will because when
  // stop waiting frames are in use, we ack every 20 packets no matter what.
  for (int i = 1; i <= 18; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(ack_time);
}

TEST_F(UberReceivedPacketManagerTest, SendDelayedAckDecimationEighthRtt) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(), ACK_DECIMATION);
  UberReceivedPacketManagerPeer::SetAckDecimationDelay(manager_.get(), 0.125);

  // The ack time should be based on min_rtt/8, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.125;

  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (uint64_t i = 1; i < 10; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest, SendDelayedAckDecimationWithReordering) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(),
                                            ACK_DECIMATION_WITH_REORDERING);

  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.25;
  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  // Receive one packet out of order and then the rest in order.
  // The loop leaves a one packet gap between acks sent to simulate some loss.
  for (int j = 0; j < 3; ++j) {
    // Process packet 10 first and ensure the timeout is one eighth min_rtt.
    RecordPacketReceipt(kFirstDecimatedPacket + 9 + (j * 11),
                        clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 9 + (j * 11));
    ack_time = clock_.ApproximateNow() + QuicTime::Delta::FromMilliseconds(5);
    CheckAckTimeout(ack_time);

    // The 10th received packet causes an ack to be sent.
    for (int i = 0; i < 9; ++i) {
      RecordPacketReceipt(kFirstDecimatedPacket + i + (j * 11),
                          clock_.ApproximateNow());
      MaybeUpdateAckTimeout(kInstigateAck,
                            kFirstDecimatedPacket + i + (j * 11));
    }
    CheckAckTimeout(clock_.ApproximateNow());
  }
}

TEST_F(UberReceivedPacketManagerTest,
       SendDelayedAckDecimationWithLargeReordering) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(),
                                            ACK_DECIMATION_WITH_REORDERING);
  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.25;

  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  RecordPacketReceipt(kFirstDecimatedPacket + 19, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 19);
  ack_time = clock_.ApproximateNow() + kMinRttMs * 0.125;
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (int i = 1; i < 9; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(clock_.ApproximateNow());

  // The next packet received in order will cause an immediate ack, because it
  // fills a hole.
  RecordPacketReceipt(kFirstDecimatedPacket + 10, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 10);
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest,
       SendDelayedAckDecimationWithReorderingEighthRtt) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(),
                                            ACK_DECIMATION_WITH_REORDERING);
  UberReceivedPacketManagerPeer::SetAckDecimationDelay(manager_.get(), 0.125);
  // The ack time should be based on min_rtt/8, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.125;

  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  // Process packet 10 first and ensure the timeout is one eighth min_rtt.
  RecordPacketReceipt(kFirstDecimatedPacket + 9, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 9);
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (int i = 1; i < 9; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck + i, kFirstDecimatedPacket);
  }
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest,
       SendDelayedAckDecimationWithLargeReorderingEighthRtt) {
  EXPECT_FALSE(HasPendingAck());
  UberReceivedPacketManagerPeer::SetAckMode(manager_.get(),
                                            ACK_DECIMATION_WITH_REORDERING);
  UberReceivedPacketManagerPeer::SetAckDecimationDelay(manager_.get(), 0.125);

  // The ack time should be based on min_rtt/8, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() + kMinRttMs * 0.125;
  // Process all the packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (uint64_t i = 1; i < kFirstDecimatedPacket; ++i) {
    RecordPacketReceipt(i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, i);
    if (i % 2 == 0) {
      // Ack every 2 packets by default.
      CheckAckTimeout(clock_.ApproximateNow());
    } else {
      CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);
    }
  }

  RecordPacketReceipt(kFirstDecimatedPacket, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket);
  CheckAckTimeout(ack_time);

  RecordPacketReceipt(kFirstDecimatedPacket + 19, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 19);
  CheckAckTimeout(ack_time);

  // The 10th received packet causes an ack to be sent.
  for (int i = 1; i < 9; ++i) {
    RecordPacketReceipt(kFirstDecimatedPacket + i, clock_.ApproximateNow());
    MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + i);
  }
  CheckAckTimeout(clock_.ApproximateNow());

  // The next packet received in order will cause an immediate ack, because it
  // fills a hole.
  RecordPacketReceipt(kFirstDecimatedPacket + 10, clock_.ApproximateNow());
  MaybeUpdateAckTimeout(kInstigateAck, kFirstDecimatedPacket + 10);
  CheckAckTimeout(clock_.ApproximateNow());
}

TEST_F(UberReceivedPacketManagerTest,
       DontWaitForPacketsBeforeMultiplePacketNumberSpaces) {
  manager_->EnableMultiplePacketNumberSpacesSupport();
  EXPECT_FALSE(
      manager_->GetLargestObserved(ENCRYPTION_HANDSHAKE).IsInitialized());
  EXPECT_FALSE(
      manager_->GetLargestObserved(ENCRYPTION_FORWARD_SECURE).IsInitialized());
  RecordPacketReceipt(ENCRYPTION_HANDSHAKE, 2);
  RecordPacketReceipt(ENCRYPTION_HANDSHAKE, 4);
  RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, 3);
  RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, 7);
  EXPECT_EQ(QuicPacketNumber(4),
            manager_->GetLargestObserved(ENCRYPTION_HANDSHAKE));
  EXPECT_EQ(QuicPacketNumber(7),
            manager_->GetLargestObserved(ENCRYPTION_FORWARD_SECURE));

  EXPECT_TRUE(
      manager_->IsAwaitingPacket(ENCRYPTION_HANDSHAKE, QuicPacketNumber(3)));
  EXPECT_FALSE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                          QuicPacketNumber(3)));
  EXPECT_TRUE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                         QuicPacketNumber(4)));

  manager_->DontWaitForPacketsBefore(ENCRYPTION_FORWARD_SECURE,
                                     QuicPacketNumber(5));
  EXPECT_TRUE(
      manager_->IsAwaitingPacket(ENCRYPTION_HANDSHAKE, QuicPacketNumber(3)));
  EXPECT_FALSE(manager_->IsAwaitingPacket(ENCRYPTION_FORWARD_SECURE,
                                          QuicPacketNumber(4)));
}

TEST_F(UberReceivedPacketManagerTest, AckSendingDifferentPacketNumberSpaces) {
  manager_->EnableMultiplePacketNumberSpacesSupport();
  EXPECT_FALSE(HasPendingAck());
  EXPECT_FALSE(manager_->IsAckFrameUpdated());

  RecordPacketReceipt(ENCRYPTION_HANDSHAKE, 3);
  EXPECT_TRUE(manager_->IsAckFrameUpdated());
  MaybeUpdateAckTimeout(kInstigateAck, ENCRYPTION_HANDSHAKE, 3);
  EXPECT_TRUE(HasPendingAck());
  // Delayed ack is scheduled.
  CheckAckTimeout(clock_.ApproximateNow() +
                  QuicTime::Delta::FromMilliseconds(1));
  // Send delayed handshake data ACK.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  CheckAckTimeout(clock_.ApproximateNow());
  EXPECT_FALSE(HasPendingAck());

  RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, 3);
  MaybeUpdateAckTimeout(kInstigateAck, ENCRYPTION_FORWARD_SECURE, 3);
  EXPECT_TRUE(HasPendingAck());
  // Delayed ack is scheduled.
  CheckAckTimeout(clock_.ApproximateNow() + kDelayedAckTime);

  RecordPacketReceipt(ENCRYPTION_FORWARD_SECURE, 2);
  MaybeUpdateAckTimeout(kInstigateAck, ENCRYPTION_FORWARD_SECURE, 2);
  // Application data ACK should be sent immediately.
  CheckAckTimeout(clock_.ApproximateNow());
  EXPECT_FALSE(HasPendingAck());
}

}  // namespace
}  // namespace test
}  // namespace quic
