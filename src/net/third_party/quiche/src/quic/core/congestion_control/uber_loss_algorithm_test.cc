// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/uber_loss_algorithm.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_unacked_packet_map_peer.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"

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
                                    true, true);
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
    return VerifyLosses(largest_newly_acked, packets_acked, losses_expected,
                        quiche::QuicheOptional<QuicPacketCount>());
  }

  void VerifyLosses(uint64_t largest_newly_acked,
                    const AckedPacketVector& packets_acked,
                    const std::vector<uint64_t>& losses_expected,
                    quiche::QuicheOptional<QuicPacketCount>
                        max_sequence_reordering_expected) {
    LostPacketVector lost_packets;
    LossDetectionInterface::DetectionStats stats = loss_algorithm_.DetectLosses(
        *unacked_packets_, clock_.Now(), rtt_stats_,
        QuicPacketNumber(largest_newly_acked), packets_acked, &lost_packets);
    if (max_sequence_reordering_expected.has_value()) {
      EXPECT_EQ(stats.sent_packets_max_sequence_reordering,
                max_sequence_reordering_expected.value());
    }
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
  VerifyLosses(4, packets_acked_, std::vector<uint64_t>{}, 0);
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
  VerifyLosses(4, packets_acked_, std::vector<uint64_t>{}, 1);
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());

  // Acking 6 causes 3 to be detected loss.
  AckPackets({6});
  unacked_packets_->MaybeUpdateLargestAckedOfPacketNumberSpace(
      APPLICATION_DATA, QuicPacketNumber(6));
  VerifyLosses(6, packets_acked_, std::vector<uint64_t>{3}, 3);
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
  packets_acked_.clear();

  clock_.AdvanceTime(1.25 * rtt_stats_.latest_rtt());
  // Verify 5 will be early retransmitted.
  VerifyLosses(6, packets_acked_, {5}, 1);
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
  VerifyLosses(5, packets_acked_, std::vector<uint64_t>{}, 2);
  EXPECT_EQ(clock_.Now() + 1.25 * rtt_stats_.smoothed_rtt(),
            loss_algorithm_.GetLossTimeout());
  packets_acked_.clear();

  clock_.AdvanceTime(1.25 * rtt_stats_.latest_rtt());
  // Verify 2 and 3 will be early retransmitted.
  VerifyLosses(5, packets_acked_, std::vector<uint64_t>{2, 3}, 2);
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

class TestLossTuner : public LossDetectionTunerInterface {
 public:
  TestLossTuner(bool forced_start_result,
                LossDetectionParameters forced_parameters)
      : forced_start_result_(forced_start_result),
        forced_parameters_(std::move(forced_parameters)) {}

  ~TestLossTuner() override = default;

  bool Start(LossDetectionParameters* params) override {
    start_called_ = true;
    *params = forced_parameters_;
    return forced_start_result_;
  }

  void Finish(const LossDetectionParameters& /*params*/) override {}

  bool start_called() const { return start_called_; }

 private:
  bool forced_start_result_;
  LossDetectionParameters forced_parameters_;
  bool start_called_ = false;
};

// Verify the parameters are changed if first call SetFromConfig(), then call
// OnMinRttAvailable().
TEST_F(UberLossAlgorithmTest, LossDetectionTuning_SetFromConfigFirst) {
  const int old_reordering_shift = loss_algorithm_.GetPacketReorderingShift();
  const QuicPacketCount old_reordering_threshold =
      loss_algorithm_.GetPacketReorderingThreshold();

  loss_algorithm_.OnUserAgentIdKnown();

  // Not owned.
  TestLossTuner* test_tuner = new TestLossTuner(
      /*forced_start_result=*/true,
      LossDetectionParameters{
          /*reordering_shift=*/old_reordering_shift + 1,
          /*reordering_threshold=*/old_reordering_threshold * 2});
  loss_algorithm_.SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface>(test_tuner));

  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kELDT);
  config.SetInitialReceivedConnectionOptions(connection_options);
  loss_algorithm_.SetFromConfig(config, Perspective::IS_SERVER);

  // MinRtt was not available when SetFromConfig was called.
  EXPECT_FALSE(test_tuner->start_called());
  EXPECT_EQ(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_EQ(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());

  // MinRtt available. Tuner should not start yet because no reordering yet.
  loss_algorithm_.OnMinRttAvailable();
  EXPECT_FALSE(test_tuner->start_called());

  // Reordering happened. Tuner should start now.
  loss_algorithm_.OnReorderingDetected();
  EXPECT_TRUE(test_tuner->start_called());
  EXPECT_NE(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_NE(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());
}

// Verify the parameters are changed if first call OnMinRttAvailable(), then
// call SetFromConfig().
TEST_F(UberLossAlgorithmTest, LossDetectionTuning_OnMinRttAvailableFirst) {
  const int old_reordering_shift = loss_algorithm_.GetPacketReorderingShift();
  const QuicPacketCount old_reordering_threshold =
      loss_algorithm_.GetPacketReorderingThreshold();

  loss_algorithm_.OnUserAgentIdKnown();

  // Not owned.
  TestLossTuner* test_tuner = new TestLossTuner(
      /*forced_start_result=*/true,
      LossDetectionParameters{
          /*reordering_shift=*/old_reordering_shift + 1,
          /*reordering_threshold=*/old_reordering_threshold * 2});
  loss_algorithm_.SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface>(test_tuner));

  loss_algorithm_.OnMinRttAvailable();
  EXPECT_FALSE(test_tuner->start_called());
  EXPECT_EQ(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_EQ(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());

  // Pretend a reodering has happened.
  loss_algorithm_.OnReorderingDetected();
  EXPECT_FALSE(test_tuner->start_called());

  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kELDT);
  config.SetInitialReceivedConnectionOptions(connection_options);
  // Should start tuning since MinRtt is available.
  loss_algorithm_.SetFromConfig(config, Perspective::IS_SERVER);

  EXPECT_TRUE(test_tuner->start_called());
  EXPECT_NE(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_NE(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());
}

// Verify the parameters are not changed if Tuner.Start() returns false.
TEST_F(UberLossAlgorithmTest, LossDetectionTuning_StartFailed) {
  const int old_reordering_shift = loss_algorithm_.GetPacketReorderingShift();
  const QuicPacketCount old_reordering_threshold =
      loss_algorithm_.GetPacketReorderingThreshold();

  loss_algorithm_.OnUserAgentIdKnown();

  // Not owned.
  TestLossTuner* test_tuner = new TestLossTuner(
      /*forced_start_result=*/false,
      LossDetectionParameters{
          /*reordering_shift=*/old_reordering_shift + 1,
          /*reordering_threshold=*/old_reordering_threshold * 2});
  loss_algorithm_.SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface>(test_tuner));

  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kELDT);
  config.SetInitialReceivedConnectionOptions(connection_options);
  loss_algorithm_.SetFromConfig(config, Perspective::IS_SERVER);

  // MinRtt was not available when SetFromConfig was called.
  EXPECT_FALSE(test_tuner->start_called());
  EXPECT_EQ(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_EQ(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());

  // Pretend a reodering has happened.
  loss_algorithm_.OnReorderingDetected();
  EXPECT_FALSE(test_tuner->start_called());

  // Parameters should not change since test_tuner->Start() returns false.
  loss_algorithm_.OnMinRttAvailable();
  EXPECT_TRUE(test_tuner->start_called());
  EXPECT_EQ(old_reordering_shift, loss_algorithm_.GetPacketReorderingShift());
  EXPECT_EQ(old_reordering_threshold,
            loss_algorithm_.GetPacketReorderingThreshold());
}

}  // namespace
}  // namespace test
}  // namespace quic
