// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr_sender.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_result.pb.h"
#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    quic_bbr_test_regression_mode,
    "",
    "One of a) 'record' to record test result (one file per test), or "
    "b) 'regress' to regress against recorded results, or "
    "c) <anything else> for non-regression mode.");

namespace quic {
namespace test {

// Use the initial CWND of 10, as 32 is too much for the test network.
const uint32_t kInitialCongestionWindowPackets = 10;
const uint32_t kDefaultWindowTCP =
    kInitialCongestionWindowPackets * kDefaultTCPMSS;

// Test network parameters.  Here, the topology of the network is:
//
//          BBR sender
//               |
//               |  <-- local link (10 Mbps, 2 ms delay)
//               |
//        Network switch
//               *  <-- the bottleneck queue in the direction
//               |          of the receiver
//               |
//               |  <-- test link (4 Mbps, 30 ms delay)
//               |
//               |
//           Receiver
//
// The reason the bandwidths chosen are relatively low is the fact that the
// connection simulator uses QuicTime for its internal clock, and as such has
// the granularity of 1us, meaning that at bandwidth higher than 20 Mbps the
// packets can start to land on the same timestamp.
const QuicBandwidth kTestLinkBandwidth =
    QuicBandwidth::FromKBitsPerSecond(4000);
const QuicBandwidth kLocalLinkBandwidth =
    QuicBandwidth::FromKBitsPerSecond(10000);
const QuicTime::Delta kTestPropagationDelay =
    QuicTime::Delta::FromMilliseconds(30);
const QuicTime::Delta kLocalPropagationDelay =
    QuicTime::Delta::FromMilliseconds(2);
const QuicTime::Delta kTestTransferTime =
    kTestLinkBandwidth.TransferTime(kMaxOutgoingPacketSize) +
    kLocalLinkBandwidth.TransferTime(kMaxOutgoingPacketSize);
const QuicTime::Delta kTestRtt =
    (kTestPropagationDelay + kLocalPropagationDelay + kTestTransferTime) * 2;
const QuicByteCount kTestBdp = kTestRtt * kTestLinkBandwidth;

class BbrSenderTest : public QuicTest {
 protected:
  BbrSenderTest()
      : simulator_(&random_),
        bbr_sender_(&simulator_,
                    "BBR sender",
                    "Receiver",
                    Perspective::IS_CLIENT,
                    /*connection_id=*/TestConnectionId(42)),
        competing_sender_(&simulator_,
                          "Competing sender",
                          "Competing receiver",
                          Perspective::IS_CLIENT,
                          /*connection_id=*/TestConnectionId(43)),
        receiver_(&simulator_,
                  "Receiver",
                  "BBR sender",
                  Perspective::IS_SERVER,
                  /*connection_id=*/TestConnectionId(42)),
        competing_receiver_(&simulator_,
                            "Competing receiver",
                            "Competing sender",
                            Perspective::IS_SERVER,
                            /*connection_id=*/TestConnectionId(43)),
        receiver_multiplexer_("Receiver multiplexer",
                              {&receiver_, &competing_receiver_}) {
    rtt_stats_ = bbr_sender_.connection()->sent_packet_manager().GetRttStats();
    sender_ = SetupBbrSender(&bbr_sender_);

    clock_ = simulator_.GetClock();
  }

  void SetUp() override {
    if (GetQuicFlag(FLAGS_quic_bbr_test_regression_mode) == "regress") {
      SendAlgorithmTestResult expected;
      ASSERT_TRUE(LoadSendAlgorithmTestResult(&expected));
      random_seed_ = expected.random_seed();
    } else {
      random_seed_ = QuicRandom::GetInstance()->RandUint64();
    }
    random_.set_seed(random_seed_);
    QUIC_LOG(INFO) << "BbrSenderTest simulator set up.  Seed: " << random_seed_;
  }

  ~BbrSenderTest() {
    const std::string regression_mode =
        GetQuicFlag(FLAGS_quic_bbr_test_regression_mode);
    const QuicTime::Delta simulated_duration = clock_->Now() - QuicTime::Zero();
    if (regression_mode == "record") {
      RecordSendAlgorithmTestResult(random_seed_,
                                    simulated_duration.ToMicroseconds());
    } else if (regression_mode == "regress") {
      CompareSendAlgorithmTestResult(simulated_duration.ToMicroseconds());
    }
  }

  uint64_t random_seed_;
  SimpleRandom random_;
  simulator::Simulator simulator_;
  simulator::QuicEndpoint bbr_sender_;
  simulator::QuicEndpoint competing_sender_;
  simulator::QuicEndpoint receiver_;
  simulator::QuicEndpoint competing_receiver_;
  simulator::QuicEndpointMultiplexer receiver_multiplexer_;
  std::unique_ptr<simulator::Switch> switch_;
  std::unique_ptr<simulator::SymmetricLink> bbr_sender_link_;
  std::unique_ptr<simulator::SymmetricLink> competing_sender_link_;
  std::unique_ptr<simulator::SymmetricLink> receiver_link_;

  // Owned by different components of the connection.
  const QuicClock* clock_;
  const RttStats* rtt_stats_;
  BbrSender* sender_;

  // Enables BBR on |endpoint| and returns the associated BBR congestion
  // controller.
  BbrSender* SetupBbrSender(simulator::QuicEndpoint* endpoint) {
    const RttStats* rtt_stats =
        endpoint->connection()->sent_packet_manager().GetRttStats();
    // Ownership of the sender will be overtaken by the endpoint.
    BbrSender* sender = new BbrSender(
        endpoint->connection()->clock()->Now(), rtt_stats,
        QuicSentPacketManagerPeer::GetUnackedPacketMap(
            QuicConnectionPeer::GetSentPacketManager(endpoint->connection())),
        kInitialCongestionWindowPackets,
        GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
        QuicConnectionPeer::GetStats(endpoint->connection()));
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  // Creates a default setup, which is a network with a bottleneck between the
  // receiver and the switch.  The switch has the buffers four times larger than
  // the bottleneck BDP, which should guarantee a lack of losses.
  void CreateDefaultSetup() {
    switch_ = std::make_unique<simulator::Switch>(&simulator_, "Switch", 8,
                                                  2 * kTestBdp);
    bbr_sender_link_ = std::make_unique<simulator::SymmetricLink>(
        &bbr_sender_, switch_->port(1), kLocalLinkBandwidth,
        kLocalPropagationDelay);
    receiver_link_ = std::make_unique<simulator::SymmetricLink>(
        &receiver_, switch_->port(2), kTestLinkBandwidth,
        kTestPropagationDelay);
  }

  // Same as the default setup, except the buffer now is half of the BDP.
  void CreateSmallBufferSetup() {
    switch_ = std::make_unique<simulator::Switch>(&simulator_, "Switch", 8,
                                                  0.5 * kTestBdp);
    bbr_sender_link_ = std::make_unique<simulator::SymmetricLink>(
        &bbr_sender_, switch_->port(1), kLocalLinkBandwidth,
        kLocalPropagationDelay);
    receiver_link_ = std::make_unique<simulator::SymmetricLink>(
        &receiver_, switch_->port(2), kTestLinkBandwidth,
        kTestPropagationDelay);
  }

  // Creates the variation of the default setup in which there is another sender
  // that competes for the same bottleneck link.
  void CreateCompetitionSetup() {
    switch_ = std::make_unique<simulator::Switch>(&simulator_, "Switch", 8,
                                                  2 * kTestBdp);

    // Add a small offset to the competing link in order to avoid
    // synchronization effects.
    const QuicTime::Delta small_offset = QuicTime::Delta::FromMicroseconds(3);
    bbr_sender_link_ = std::make_unique<simulator::SymmetricLink>(
        &bbr_sender_, switch_->port(1), kLocalLinkBandwidth,
        kLocalPropagationDelay);
    competing_sender_link_ = std::make_unique<simulator::SymmetricLink>(
        &competing_sender_, switch_->port(3), kLocalLinkBandwidth,
        kLocalPropagationDelay + small_offset);
    receiver_link_ = std::make_unique<simulator::SymmetricLink>(
        &receiver_multiplexer_, switch_->port(2), kTestLinkBandwidth,
        kTestPropagationDelay);
  }

  // Creates a BBR vs BBR competition setup.
  void CreateBbrVsBbrSetup() {
    SetupBbrSender(&competing_sender_);
    CreateCompetitionSetup();
  }

  void EnableAggregation(QuicByteCount aggregation_bytes,
                         QuicTime::Delta aggregation_timeout) {
    // Enable aggregation on the path from the receiver to the sender.
    switch_->port_queue(1)->EnableAggregation(aggregation_bytes,
                                              aggregation_timeout);
  }

  void DoSimpleTransfer(QuicByteCount transfer_size, QuicTime::Delta deadline) {
    bbr_sender_.AddBytesToTransfer(transfer_size);
    // TODO(vasilvv): consider rewriting this to run until the receiver actually
    // receives the intended amount of bytes.
    bool simulator_result = simulator_.RunUntilOrTimeout(
        [this]() { return bbr_sender_.bytes_to_transfer() == 0; }, deadline);
    EXPECT_TRUE(simulator_result)
        << "Simple transfer failed.  Bytes remaining: "
        << bbr_sender_.bytes_to_transfer();
    QUIC_LOG(INFO) << "Simple transfer state: " << sender_->ExportDebugState();
  }

  // Drive the simulator by sending enough data to enter PROBE_BW.
  void DriveOutOfStartup() {
    ASSERT_FALSE(sender_->ExportDebugState().is_at_full_bandwidth);
    DoSimpleTransfer(1024 * 1024, QuicTime::Delta::FromSeconds(15));
    EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.02f);
  }

  // Send |bytes|-sized bursts of data |number_of_bursts| times, waiting for
  // |wait_time| between each burst.
  void SendBursts(size_t number_of_bursts,
                  QuicByteCount bytes,
                  QuicTime::Delta wait_time) {
    ASSERT_EQ(0u, bbr_sender_.bytes_to_transfer());
    for (size_t i = 0; i < number_of_bursts; i++) {
      bbr_sender_.AddBytesToTransfer(bytes);

      // Transfer data and wait for three seconds between each transfer.
      simulator_.RunFor(wait_time);

      // Ensure the connection did not time out.
      ASSERT_TRUE(bbr_sender_.connection()->connected());
      ASSERT_TRUE(receiver_.connection()->connected());
    }

    simulator_.RunFor(wait_time + kTestRtt);
    ASSERT_EQ(0u, bbr_sender_.bytes_to_transfer());
  }

  void SetConnectionOption(QuicTag option) {
    QuicConfig config;
    QuicTagVector options;
    options.push_back(option);
    QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
    sender_->SetFromConfig(config, Perspective::IS_SERVER);
  }
};

TEST_F(BbrSenderTest, SetInitialCongestionWindow) {
  EXPECT_NE(3u * kDefaultTCPMSS, sender_->GetCongestionWindow());
  sender_->SetInitialCongestionWindowInPackets(3);
  EXPECT_EQ(3u * kDefaultTCPMSS, sender_->GetCongestionWindow());
}

// Test a simple long data transfer in the default setup.
TEST_F(BbrSenderTest, SimpleTransfer) {
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();

  // At startup make sure we are at the default.
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());
  // At startup make sure we can send.
  EXPECT_TRUE(sender_->CanSend(0));
  // And that window is un-affected.
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());

  // Verify that Sender is in slow start.
  EXPECT_TRUE(sender_->InSlowStart());

  // Verify that pacing rate is based on the initial RTT.
  QuicBandwidth expected_pacing_rate = QuicBandwidth::FromBytesAndTimeDelta(
      2.885 * kDefaultWindowTCP, rtt_stats_->initial_rtt());
  EXPECT_APPROX_EQ(expected_pacing_rate.ToBitsPerSecond(),
                   sender_->PacingRate(0).ToBitsPerSecond(), 0.01f);

  ASSERT_GE(kTestBdp, kDefaultWindowTCP + kDefaultTCPMSS);

  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(30));
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  // The margin here is quite high, since there exists a possibility that the
  // connection just exited high gain cycle.
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->smoothed_rtt(), 0.2f);
}

// Test a simple transfer in a situation when the buffer is less than BDP.
TEST_F(BbrSenderTest, SimpleTransferSmallBuffer) {
  CreateSmallBufferSetup();

  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(30));
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
  EXPECT_GE(bbr_sender_.connection()->GetStats().packets_lost, 0u);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  // The margin here is quite high, since there exists a possibility that the
  // connection just exited high gain cycle.
  EXPECT_APPROX_EQ(kTestRtt, sender_->GetMinRtt(), 0.2f);
}

TEST_F(BbrSenderTest, RemoveBytesLostInRecovery) {
  SetQuicReloadableFlag(quic_bbr_one_mss_conservation, false);
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();

  DriveOutOfStartup();

  // Drop a packet to enter recovery.
  receiver_.DropNextIncomingPacket();
  ASSERT_TRUE(
      simulator_.RunUntilOrTimeout([this]() { return sender_->InRecovery(); },
                                   QuicTime::Delta::FromSeconds(30)));

  QuicUnackedPacketMap* unacked_packets =
      QuicSentPacketManagerPeer::GetUnackedPacketMap(
          QuicConnectionPeer::GetSentPacketManager(bbr_sender_.connection()));
  QuicPacketNumber largest_sent =
      bbr_sender_.connection()->sent_packet_manager().GetLargestSentPacket();
  // least_inflight is the smallest inflight packet.
  QuicPacketNumber least_inflight =
      bbr_sender_.connection()->sent_packet_manager().GetLeastUnacked();
  while (!unacked_packets->GetTransmissionInfo(least_inflight).in_flight) {
    ASSERT_LE(least_inflight, largest_sent);
    least_inflight++;
  }
  QuicPacketLength least_inflight_packet_size =
      unacked_packets->GetTransmissionInfo(least_inflight).bytes_sent;
  QuicByteCount prior_recovery_window =
      sender_->ExportDebugState().recovery_window;
  QuicByteCount prior_inflight = unacked_packets->bytes_in_flight();
  QUIC_LOG(INFO) << "Recovery window:" << prior_recovery_window
                 << ", least_inflight_packet_size:"
                 << least_inflight_packet_size
                 << ", bytes_in_flight:" << prior_inflight;
  ASSERT_GT(prior_recovery_window, least_inflight_packet_size);

  // Lose the least inflight packet and expect the recovery window to drop.
  unacked_packets->RemoveFromInFlight(least_inflight);
  LostPacketVector lost_packets;
  lost_packets.emplace_back(least_inflight, least_inflight_packet_size);
  sender_->OnCongestionEvent(false, prior_inflight, clock_->Now(), {},
                             lost_packets);
  EXPECT_EQ(sender_->ExportDebugState().recovery_window,
            prior_inflight - least_inflight_packet_size);
  EXPECT_LT(sender_->ExportDebugState().recovery_window, prior_recovery_window);
}

// Test a simple long data transfer with 2 rtts of aggregation.
TEST_F(BbrSenderTest, SimpleTransfer2RTTAggregationBytes) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  CreateDefaultSetup();
  // 2 RTTs of aggregation, with a max of 10kb.
  EnableAggregation(10 * 1024, 2 * kTestRtt);

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(sender_->ExportDebugState().mode == BbrSender::PROBE_BW ||
              sender_->ExportDebugState().mode == BbrSender::PROBE_RTT);
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.01f);
  } else {
    // It's possible to read a bandwidth as much as 50% too high with
    // aggregation.
    EXPECT_LE(kTestLinkBandwidth * 0.93f,
              sender_->ExportDebugState().max_bandwidth);
    // TODO(ianswett): Tighten this bound once we understand why BBR is
    // overestimating bandwidth with aggregation. b/36022633
    EXPECT_GE(kTestLinkBandwidth * 1.5f,
              sender_->ExportDebugState().max_bandwidth);
  }
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(kTestRtt * 4, rtt_stats_->smoothed_rtt());
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->min_rtt(), 0.5f);
}

// Test a simple long data transfer with 2 rtts of aggregation.
TEST_F(BbrSenderTest, SimpleTransferAckDecimation) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  // Decrease the CWND gain so extra CWND is required with stretch acks.
  SetQuicFlag(FLAGS_quic_bbr_cwnd_gain, 1.0);
  sender_ = new BbrSender(
      bbr_sender_.connection()->clock()->Now(), rtt_stats_,
      QuicSentPacketManagerPeer::GetUnackedPacketMap(
          QuicConnectionPeer::GetSentPacketManager(bbr_sender_.connection())),
      kInitialCongestionWindowPackets,
      GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
      QuicConnectionPeer::GetStats(bbr_sender_.connection()));
  QuicConnectionPeer::SetSendAlgorithm(bbr_sender_.connection(), sender_);
  // Enable Ack Decimation on the receiver.
  QuicConnectionPeer::SetAckMode(receiver_.connection(),
                                 AckMode::ACK_DECIMATION);
  CreateDefaultSetup();

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);

  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.01f);
  } else {
    // It's possible to read a bandwidth as much as 50% too high with
    // aggregation.
    EXPECT_LE(kTestLinkBandwidth * 0.93f,
              sender_->ExportDebugState().max_bandwidth);
    // TODO(ianswett): Tighten this bound once we understand why BBR is
    // overestimating bandwidth with aggregation. b/36022633
    EXPECT_GE(kTestLinkBandwidth * 1.5f,
              sender_->ExportDebugState().max_bandwidth);
  }
  // TODO(ianswett): Expect 0 packets are lost once BBR no longer measures
  // bandwidth higher than the link rate.
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(kTestRtt * 2, rtt_stats_->smoothed_rtt());
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->min_rtt(), 0.1f);
}

// Test a simple long data transfer with 2 rtts of aggregation.
TEST_F(BbrSenderTest, SimpleTransfer2RTTAggregationBytes20RTTWindow) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();
  SetConnectionOption(kBBR4);
  // 2 RTTs of aggregation, with a max of 10kb.
  EnableAggregation(10 * 1024, 2 * kTestRtt);

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(sender_->ExportDebugState().mode == BbrSender::PROBE_BW ||
              sender_->ExportDebugState().mode == BbrSender::PROBE_RTT);
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.01f);
  } else {
    // It's possible to read a bandwidth as much as 50% too high with
    // aggregation.
    EXPECT_LE(kTestLinkBandwidth * 0.93f,
              sender_->ExportDebugState().max_bandwidth);
    // TODO(ianswett): Tighten this bound once we understand why BBR is
    // overestimating bandwidth with aggregation. b/36022633
    EXPECT_GE(kTestLinkBandwidth * 1.5f,
              sender_->ExportDebugState().max_bandwidth);
  }
  // TODO(ianswett): Expect 0 packets are lost once BBR no longer measures
  // bandwidth higher than the link rate.
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(kTestRtt * 4, rtt_stats_->smoothed_rtt());
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->min_rtt(), 0.25f);
}

// Test a simple long data transfer with 2 rtts of aggregation.
TEST_F(BbrSenderTest, SimpleTransfer2RTTAggregationBytes40RTTWindow) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();
  SetConnectionOption(kBBR5);
  // 2 RTTs of aggregation, with a max of 10kb.
  EnableAggregation(10 * 1024, 2 * kTestRtt);

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(sender_->ExportDebugState().mode == BbrSender::PROBE_BW ||
              sender_->ExportDebugState().mode == BbrSender::PROBE_RTT);
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.01f);
  } else {
    // It's possible to read a bandwidth as much as 50% too high with
    // aggregation.
    EXPECT_LE(kTestLinkBandwidth * 0.93f,
              sender_->ExportDebugState().max_bandwidth);
    // TODO(ianswett): Tighten this bound once we understand why BBR is
    // overestimating bandwidth with aggregation. b/36022633
    EXPECT_GE(kTestLinkBandwidth * 1.5f,
              sender_->ExportDebugState().max_bandwidth);
  }
  // TODO(ianswett): Expect 0 packets are lost once BBR no longer measures
  // bandwidth higher than the link rate.
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(kTestRtt * 4, rtt_stats_->smoothed_rtt());
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->min_rtt(), 0.25f);
}

// Test the number of losses incurred by the startup phase in a situation when
// the buffer is less than BDP.
TEST_F(BbrSenderTest, PacketLossOnSmallBufferStartup) {
  CreateSmallBufferSetup();

  DriveOutOfStartup();
  float loss_rate =
      static_cast<float>(bbr_sender_.connection()->GetStats().packets_lost) /
      bbr_sender_.connection()->GetStats().packets_sent;
  EXPECT_LE(loss_rate, 0.31);
}

// Test the number of losses incurred by the startup phase in a situation when
// the buffer is less than BDP, with a STARTUP CWND gain of 2.
TEST_F(BbrSenderTest, PacketLossOnSmallBufferStartupDerivedCWNDGain) {
  CreateSmallBufferSetup();

  SetConnectionOption(kBBQ2);
  DriveOutOfStartup();
  float loss_rate =
      static_cast<float>(bbr_sender_.connection()->GetStats().packets_lost) /
      bbr_sender_.connection()->GetStats().packets_sent;
  EXPECT_LE(loss_rate, 0.1);
}

// Ensures the code transitions loss recovery states correctly (NOT_IN_RECOVERY
// -> CONSERVATION -> GROWTH -> NOT_IN_RECOVERY).
TEST_F(BbrSenderTest, RecoveryStates) {
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(10);
  bool simulator_result;
  CreateSmallBufferSetup();

  bbr_sender_.AddBytesToTransfer(100 * 1024 * 1024);
  ASSERT_EQ(BbrSender::NOT_IN_RECOVERY,
            sender_->ExportDebugState().recovery_state);

  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().recovery_state !=
               BbrSender::NOT_IN_RECOVERY;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::CONSERVATION,
            sender_->ExportDebugState().recovery_state);

  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().recovery_state !=
               BbrSender::CONSERVATION;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::GROWTH, sender_->ExportDebugState().recovery_state);

  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().recovery_state != BbrSender::GROWTH;
      },
      timeout);

  ASSERT_EQ(BbrSender::NOT_IN_RECOVERY,
            sender_->ExportDebugState().recovery_state);
  ASSERT_TRUE(simulator_result);
}

// Verify the behavior of the algorithm in the case when the connection sends
// small bursts of data after sending continuously for a while.
TEST_F(BbrSenderTest, ApplicationLimitedBursts) {
  CreateDefaultSetup();

  DriveOutOfStartup();
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  SendBursts(20, 512, QuicTime::Delta::FromSeconds(3));
  EXPECT_TRUE(sender_->ExportDebugState().last_sample_is_app_limited);
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
}

// Verify the behavior of the algorithm in the case when the connection sends
// small bursts of data and then starts sending continuously.
TEST_F(BbrSenderTest, ApplicationLimitedBurstsWithoutPrior) {
  CreateDefaultSetup();

  SendBursts(40, 512, QuicTime::Delta::FromSeconds(3));
  EXPECT_TRUE(sender_->ExportDebugState().last_sample_is_app_limited);

  DriveOutOfStartup();
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Verify that the DRAIN phase works correctly.
TEST_F(BbrSenderTest, Drain) {
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(10);
  // Get the queue at the bottleneck, which is the outgoing queue at the port to
  // which the receiver is connected.
  const simulator::Queue* queue = switch_->port_queue(2);
  bool simulator_result;

  // We have no intention of ever finishing this transfer.
  bbr_sender_.AddBytesToTransfer(100 * 1024 * 1024);

  // Run the startup, and verify that it fills up the queue.
  ASSERT_EQ(BbrSender::STARTUP, sender_->ExportDebugState().mode);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode != BbrSender::STARTUP;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_APPROX_EQ(sender_->BandwidthEstimate() * (1 / 2.885f),
                   sender_->PacingRate(0), 0.01f);

  if (!GetQuicReloadableFlag(quic_bbr_default_exit_startup_on_loss)) {
    // BBR uses CWND gain of 2.88 during STARTUP, hence it will fill the buffer
    // with approximately 1.88 BDPs.  Here, we use 1.5 to give some margin for
    // error.
    EXPECT_GE(queue->bytes_queued(), 1.5 * kTestBdp);
  } else {
    // BBR uses CWND gain of 2 during STARTUP, hence it will fill the buffer
    // with approximately 1 BDP.  Here, we use 0.8 to give some margin for
    // error.
    EXPECT_GE(queue->bytes_queued(), 0.8 * kTestBdp);
  }

  // Observe increased RTT due to bufferbloat.
  const QuicTime::Delta queueing_delay =
      kTestLinkBandwidth.TransferTime(queue->bytes_queued());
  EXPECT_APPROX_EQ(kTestRtt + queueing_delay, rtt_stats_->latest_rtt(), 0.1f);

  // Transition to the drain phase and verify that it makes the queue
  // have at most a BDP worth of packets.
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().mode != BbrSender::DRAIN; },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_LE(queue->bytes_queued(), kTestBdp);

  // Wait for a few round trips and ensure we're in appropriate phase of gain
  // cycling before taking an RTT measurement.
  const QuicRoundTripCount start_round_trip =
      sender_->ExportDebugState().round_trip_count;
  simulator_result = simulator_.RunUntilOrTimeout(
      [this, start_round_trip]() {
        QuicRoundTripCount rounds_passed =
            sender_->ExportDebugState().round_trip_count - start_round_trip;
        return rounds_passed >= 4 &&
               sender_->ExportDebugState().gain_cycle_index == 7;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Observe the bufferbloat go away.
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->smoothed_rtt(), 0.1f);
}

// TODO(wub): Re-enable this test once default drain_gain changed to 0.75.
// Verify that the DRAIN phase works correctly.
TEST_F(BbrSenderTest, DISABLED_ShallowDrain) {
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);

  CreateDefaultSetup();
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(10);
  // Get the queue at the bottleneck, which is the outgoing queue at the port to
  // which the receiver is connected.
  const simulator::Queue* queue = switch_->port_queue(2);
  bool simulator_result;

  // We have no intention of ever finishing this transfer.
  bbr_sender_.AddBytesToTransfer(100 * 1024 * 1024);

  // Run the startup, and verify that it fills up the queue.
  ASSERT_EQ(BbrSender::STARTUP, sender_->ExportDebugState().mode);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode != BbrSender::STARTUP;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(0.75 * sender_->BandwidthEstimate(), sender_->PacingRate(0));
  // BBR uses CWND gain of 2.88 during STARTUP, hence it will fill the buffer
  // with approximately 1.88 BDPs.  Here, we use 1.5 to give some margin for
  // error.
  EXPECT_GE(queue->bytes_queued(), 1.5 * kTestBdp);

  // Observe increased RTT due to bufferbloat.
  const QuicTime::Delta queueing_delay =
      kTestLinkBandwidth.TransferTime(queue->bytes_queued());
  EXPECT_APPROX_EQ(kTestRtt + queueing_delay, rtt_stats_->latest_rtt(), 0.1f);

  // Transition to the drain phase and verify that it makes the queue
  // have at most a BDP worth of packets.
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().mode != BbrSender::DRAIN; },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_LE(queue->bytes_queued(), kTestBdp);

  // Wait for a few round trips and ensure we're in appropriate phase of gain
  // cycling before taking an RTT measurement.
  const QuicRoundTripCount start_round_trip =
      sender_->ExportDebugState().round_trip_count;
  simulator_result = simulator_.RunUntilOrTimeout(
      [this, start_round_trip]() {
        QuicRoundTripCount rounds_passed =
            sender_->ExportDebugState().round_trip_count - start_round_trip;
        return rounds_passed >= 4 &&
               sender_->ExportDebugState().gain_cycle_index == 7;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Observe the bufferbloat go away.
  EXPECT_APPROX_EQ(kTestRtt, rtt_stats_->smoothed_rtt(), 0.1f);
}

// Verify that the connection enters and exits PROBE_RTT correctly.
TEST_F(BbrSenderTest, ProbeRtt) {
  CreateDefaultSetup();
  DriveOutOfStartup();

  // We have no intention of ever finishing this transfer.
  bbr_sender_.AddBytesToTransfer(100 * 1024 * 1024);

  // Wait until the connection enters PROBE_RTT.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(12);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode == BbrSender::PROBE_RTT;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::PROBE_RTT, sender_->ExportDebugState().mode);

  // Exit PROBE_RTT.
  const QuicTime probe_rtt_start = clock_->Now();
  const QuicTime::Delta time_to_exit_probe_rtt =
      kTestRtt + QuicTime::Delta::FromMilliseconds(200);
  simulator_.RunFor(1.5 * time_to_exit_probe_rtt);
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_GE(sender_->ExportDebugState().min_rtt_timestamp, probe_rtt_start);
}

// Ensure that a connection that is app-limited and is at sufficiently low
// bandwidth will not exit high gain phase, and similarly ensure that the
// connection will exit low gain early if the number of bytes in flight is low.
TEST_F(BbrSenderTest, InFlightAwareGainCycling) {
  // Disable Ack Decimation on the receiver, because it can increase srtt.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();
  DriveOutOfStartup();

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  while (!(sender_->ExportDebugState().gain_cycle_index >= 4 &&
           bbr_sender_.bytes_to_transfer() == 0)) {
    bbr_sender_.AddBytesToTransfer(kTestLinkBandwidth.ToBytesPerSecond());
    ASSERT_TRUE(simulator_.RunUntilOrTimeout(
        [this]() { return bbr_sender_.bytes_to_transfer() == 0; }, timeout));
  }

  // Send at 10% of available rate.  Run for 3 seconds, checking in the middle
  // and at the end.  The pacing gain should be high throughout.
  QuicBandwidth target_bandwidth = 0.1f * kTestLinkBandwidth;
  QuicTime::Delta burst_interval = QuicTime::Delta::FromMilliseconds(300);
  for (int i = 0; i < 2; i++) {
    SendBursts(5, target_bandwidth * burst_interval, burst_interval);
    EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
    EXPECT_EQ(0, sender_->ExportDebugState().gain_cycle_index);
    EXPECT_APPROX_EQ(kTestLinkBandwidth,
                     sender_->ExportDebugState().max_bandwidth, 0.01f);
  }

  // Now that in-flight is almost zero and the pacing gain is still above 1,
  // send approximately 1.25 BDPs worth of data.  This should cause the
  // PROBE_BW mode to enter low gain cycle, and exit it earlier than one min_rtt
  // due to running out of data to send.
  bbr_sender_.AddBytesToTransfer(1.3 * kTestBdp);
  ASSERT_TRUE(simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().gain_cycle_index == 1; },
      timeout));

  simulator_.RunFor(0.75 * sender_->ExportDebugState().min_rtt);
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_EQ(2, sender_->ExportDebugState().gain_cycle_index);
}

// Ensure that the pacing rate does not drop at startup.
TEST_F(BbrSenderTest, NoBandwidthDropOnStartup) {
  CreateDefaultSetup();

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result;

  QuicBandwidth initial_rate = QuicBandwidth::FromBytesAndTimeDelta(
      kInitialCongestionWindowPackets * kDefaultTCPMSS,
      rtt_stats_->initial_rtt());
  EXPECT_GE(sender_->PacingRate(0), initial_rate);

  // Send a packet.
  bbr_sender_.AddBytesToTransfer(1000);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return receiver_.bytes_received() == 1000; }, timeout);
  ASSERT_TRUE(simulator_result);
  EXPECT_GE(sender_->PacingRate(0), initial_rate);

  // Wait for a while.
  simulator_.RunFor(QuicTime::Delta::FromSeconds(2));
  EXPECT_GE(sender_->PacingRate(0), initial_rate);

  // Send another packet.
  bbr_sender_.AddBytesToTransfer(1000);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return receiver_.bytes_received() == 2000; }, timeout);
  ASSERT_TRUE(simulator_result);
  EXPECT_GE(sender_->PacingRate(0), initial_rate);
}

// Test exiting STARTUP earlier due to the 1RTT connection option.
TEST_F(BbrSenderTest, SimpleTransfer1RTTStartup) {
  CreateDefaultSetup();

  SetConnectionOption(k1RTT);
  EXPECT_EQ(1u, sender_->num_startup_rtts());

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().max_bandwidth) {
          max_bw = sender_->ExportDebugState().max_bandwidth;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().is_at_full_bandwidth;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(1u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(1u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Test exiting STARTUP earlier due to the 2RTT connection option.
TEST_F(BbrSenderTest, SimpleTransfer2RTTStartup) {
  CreateDefaultSetup();

  SetConnectionOption(k2RTT);
  EXPECT_EQ(2u, sender_->num_startup_rtts());

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().max_bandwidth) {
          max_bw = sender_->ExportDebugState().max_bandwidth;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().is_at_full_bandwidth;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(2u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(2u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Test exiting STARTUP earlier upon loss.
TEST_F(BbrSenderTest, SimpleTransferExitStartupOnLoss) {
  CreateDefaultSetup();

  if (!GetQuicReloadableFlag(quic_bbr_default_exit_startup_on_loss)) {
    SetConnectionOption(kLRTT);
  }
  EXPECT_EQ(3u, sender_->num_startup_rtts());

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().max_bandwidth) {
          max_bw = sender_->ExportDebugState().max_bandwidth;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().is_at_full_bandwidth;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(3u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(3u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Test exiting STARTUP earlier upon loss with a small buffer.
TEST_F(BbrSenderTest, SimpleTransferExitStartupOnLossSmallBuffer) {
  CreateSmallBufferSetup();

  if (!GetQuicReloadableFlag(quic_bbr_default_exit_startup_on_loss)) {
    SetConnectionOption(kLRTT);
  }
  EXPECT_EQ(3u, sender_->num_startup_rtts());

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().max_bandwidth) {
          max_bw = sender_->ExportDebugState().max_bandwidth;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().is_at_full_bandwidth;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_GE(2u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(1u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_NE(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

TEST_F(BbrSenderTest, DerivedPacingGainStartup) {
  CreateDefaultSetup();

  SetConnectionOption(kBBQ1);
  EXPECT_EQ(3u, sender_->num_startup_rtts());
  // Verify that Sender is in slow start.
  EXPECT_TRUE(sender_->InSlowStart());
  // Verify that pacing rate is based on the initial RTT.
  QuicBandwidth expected_pacing_rate = QuicBandwidth::FromBytesAndTimeDelta(
      2.773 * kDefaultWindowTCP, rtt_stats_->initial_rtt());
  EXPECT_APPROX_EQ(expected_pacing_rate.ToBitsPerSecond(),
                   sender_->PacingRate(0).ToBitsPerSecond(), 0.01f);

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().is_at_full_bandwidth; },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(3u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

TEST_F(BbrSenderTest, DerivedCWNDGainStartup) {
  CreateSmallBufferSetup();

  if (!GetQuicReloadableFlag(quic_bbr_default_exit_startup_on_loss)) {
    SetConnectionOption(kBBQ2);
  }
  EXPECT_EQ(3u, sender_->num_startup_rtts());
  // Verify that Sender is in slow start.
  EXPECT_TRUE(sender_->InSlowStart());
  // Verify that pacing rate is based on the initial RTT.
  QuicBandwidth expected_pacing_rate = QuicBandwidth::FromBytesAndTimeDelta(
      2.885 * kDefaultWindowTCP, rtt_stats_->initial_rtt());
  EXPECT_APPROX_EQ(expected_pacing_rate.ToBitsPerSecond(),
                   sender_->PacingRate(0).ToBitsPerSecond(), 0.01f);

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().is_at_full_bandwidth; },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  if (!bbr_sender_.connection()->GetStats().bbr_exit_startup_due_to_loss) {
    EXPECT_EQ(3u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  }
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
  float loss_rate =
      static_cast<float>(bbr_sender_.connection()->GetStats().packets_lost) /
      bbr_sender_.connection()->GetStats().packets_sent;
  EXPECT_LT(loss_rate, 0.15f);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
  // Expect an SRTT less than 2.7 * Min RTT on exit from STARTUP.
  EXPECT_GT(kTestRtt * 2.7, rtt_stats_->smoothed_rtt());
}

TEST_F(BbrSenderTest, AckAggregationInStartup) {
  // Disable Ack Decimation on the receiver to avoid loss and make results
  // consistent.
  QuicConnectionPeer::SetAckMode(receiver_.connection(), AckMode::TCP_ACKING);
  CreateDefaultSetup();

  SetConnectionOption(kBBQ3);
  EXPECT_EQ(3u, sender_->num_startup_rtts());
  // Verify that Sender is in slow start.
  EXPECT_TRUE(sender_->InSlowStart());
  // Verify that pacing rate is based on the initial RTT.
  QuicBandwidth expected_pacing_rate = QuicBandwidth::FromBytesAndTimeDelta(
      2.885 * kDefaultWindowTCP, rtt_stats_->initial_rtt());
  EXPECT_APPROX_EQ(expected_pacing_rate.ToBitsPerSecond(),
                   sender_->PacingRate(0).ToBitsPerSecond(), 0.01f);

  // Run until the full bandwidth is reached and check how many rounds it was.
  bbr_sender_.AddBytesToTransfer(12 * 1024 * 1024);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().is_at_full_bandwidth; },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(3u, sender_->ExportDebugState().rounds_without_bandwidth_gain);
  EXPECT_APPROX_EQ(kTestLinkBandwidth,
                   sender_->ExportDebugState().max_bandwidth, 0.01f);
  EXPECT_EQ(0u, bbr_sender_.connection()->GetStats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Test that two BBR flows started slightly apart from each other terminate.
TEST_F(BbrSenderTest, SimpleCompetition) {
  const QuicByteCount transfer_size = 10 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      kTestLinkBandwidth.TransferTime(transfer_size);
  CreateBbrVsBbrSetup();

  // Transfer 10% of data in first transfer.
  bbr_sender_.AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return receiver_.bytes_received() >= 0.1 * transfer_size; },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  competing_sender_.AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_.bytes_received() == transfer_size &&
               competing_receiver_.bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}

// Test that BBR can resume bandwidth from cached network parameters.
TEST_F(BbrSenderTest, ResumeConnectionState) {
  CreateDefaultSetup();

  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(kTestLinkBandwidth, kTestRtt,
                                            false));
  if (!GetQuicReloadableFlag(quic_bbr_donot_inject_bandwidth)) {
    EXPECT_EQ(kTestLinkBandwidth, sender_->ExportDebugState().max_bandwidth);
    EXPECT_EQ(kTestLinkBandwidth, sender_->BandwidthEstimate());
  }
  EXPECT_EQ(kTestLinkBandwidth * kTestRtt,
            sender_->ExportDebugState().congestion_window);
  if (GetQuicReloadableFlag(quic_bbr_fix_pacing_rate)) {
    EXPECT_EQ(kTestLinkBandwidth, sender_->PacingRate(/*bytes_in_flight=*/0));
  }
  EXPECT_APPROX_EQ(kTestRtt, sender_->ExportDebugState().min_rtt, 0.01f);

  DriveOutOfStartup();
}

// Test with a min CWND of 1 instead of 4 packets.
TEST_F(BbrSenderTest, ProbeRTTMinCWND1) {
  CreateDefaultSetup();
  SetConnectionOption(kMIN1);
  DriveOutOfStartup();

  // We have no intention of ever finishing this transfer.
  bbr_sender_.AddBytesToTransfer(100 * 1024 * 1024);

  // Wait until the connection enters PROBE_RTT.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(12);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode == BbrSender::PROBE_RTT;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(BbrSender::PROBE_RTT, sender_->ExportDebugState().mode);
  // The PROBE_RTT CWND should be 1 if the min CWND is 1.
  EXPECT_EQ(kDefaultTCPMSS, sender_->GetCongestionWindow());

  // Exit PROBE_RTT.
  const QuicTime probe_rtt_start = clock_->Now();
  const QuicTime::Delta time_to_exit_probe_rtt =
      kTestRtt + QuicTime::Delta::FromMilliseconds(200);
  simulator_.RunFor(1.5 * time_to_exit_probe_rtt);
  EXPECT_EQ(BbrSender::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_GE(sender_->ExportDebugState().min_rtt_timestamp, probe_rtt_start);
}

TEST_F(BbrSenderTest, StartupStats) {
  CreateDefaultSetup();

  DriveOutOfStartup();
  ASSERT_FALSE(sender_->InSlowStart());

  const QuicConnectionStats& stats = bbr_sender_.connection()->GetStats();
  EXPECT_EQ(1u, stats.slowstart_count);
  EXPECT_THAT(stats.slowstart_num_rtts, AllOf(Ge(5u), Le(15u)));
  EXPECT_THAT(stats.slowstart_packets_sent, AllOf(Ge(100u), Le(1000u)));
  EXPECT_THAT(stats.slowstart_bytes_sent, AllOf(Ge(100000u), Le(1000000u)));
  EXPECT_LE(stats.slowstart_packets_lost, 10u);
  EXPECT_LE(stats.slowstart_bytes_lost, 10000u);
  EXPECT_FALSE(stats.slowstart_duration.IsRunning());
  EXPECT_THAT(stats.slowstart_duration.GetTotalElapsedTime(),
              AllOf(Ge(QuicTime::Delta::FromMilliseconds(500)),
                    Le(QuicTime::Delta::FromMilliseconds(1500))));
  EXPECT_EQ(stats.slowstart_duration.GetTotalElapsedTime(),
            QuicConnectionPeer::GetSentPacketManager(bbr_sender_.connection())
                ->GetSlowStartDuration());
}

// Regression test for b/143540157.
TEST_F(BbrSenderTest, RecalculatePacingRateOnCwndChange1RTT) {
  CreateDefaultSetup();

  bbr_sender_.AddBytesToTransfer(1 * 1024 * 1024);
  // Wait until an ACK comes back.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return !sender_->ExportDebugState().min_rtt.IsZero(); },
      timeout);
  ASSERT_TRUE(simulator_result);
  const QuicByteCount previous_cwnd =
      sender_->ExportDebugState().congestion_window;

  // Bootstrap cwnd.
  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(kTestLinkBandwidth,
                                            QuicTime::Delta::Zero(), false));
  if (!GetQuicReloadableFlag(quic_bbr_donot_inject_bandwidth)) {
    EXPECT_EQ(kTestLinkBandwidth, sender_->ExportDebugState().max_bandwidth);
    EXPECT_EQ(kTestLinkBandwidth, sender_->BandwidthEstimate());
  }
  EXPECT_LT(previous_cwnd, sender_->ExportDebugState().congestion_window);

  if (GetQuicReloadableFlag(quic_bbr_fix_pacing_rate)) {
    // Verify pacing rate is re-calculated based on the new cwnd and min_rtt.
    EXPECT_APPROX_EQ(QuicBandwidth::FromBytesAndTimeDelta(
                         sender_->ExportDebugState().congestion_window,
                         sender_->ExportDebugState().min_rtt),
                     sender_->PacingRate(/*bytes_in_flight=*/0), 0.01f);
  } else {
    // Pacing rate is still based on initial cwnd.
    EXPECT_APPROX_EQ(QuicBandwidth::FromBytesAndTimeDelta(
                         kInitialCongestionWindowPackets * kDefaultTCPMSS,
                         sender_->ExportDebugState().min_rtt),
                     sender_->PacingRate(/*bytes_in_flight=*/0), 0.01f);
  }
}

TEST_F(BbrSenderTest, RecalculatePacingRateOnCwndChange0RTT) {
  CreateDefaultSetup();
  // Initial RTT is available.
  const_cast<RttStats*>(rtt_stats_)->set_initial_rtt(kTestRtt);

  // Bootstrap cwnd.
  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(kTestLinkBandwidth,
                                            QuicTime::Delta::Zero(), false));
  if (!GetQuicReloadableFlag(quic_bbr_donot_inject_bandwidth)) {
    EXPECT_EQ(kTestLinkBandwidth, sender_->ExportDebugState().max_bandwidth);
    EXPECT_EQ(kTestLinkBandwidth, sender_->BandwidthEstimate());
  }
  EXPECT_LT(kInitialCongestionWindowPackets * kDefaultTCPMSS,
            sender_->ExportDebugState().congestion_window);
  // No Rtt sample is available.
  EXPECT_TRUE(sender_->ExportDebugState().min_rtt.IsZero());

  if (GetQuicReloadableFlag(quic_bbr_fix_pacing_rate)) {
    // Verify pacing rate is re-calculated based on the new cwnd and initial
    // RTT.
    EXPECT_APPROX_EQ(QuicBandwidth::FromBytesAndTimeDelta(
                         sender_->ExportDebugState().congestion_window,
                         rtt_stats_->initial_rtt()),
                     sender_->PacingRate(/*bytes_in_flight=*/0), 0.01f);
  } else {
    // Pacing rate is still based on initial cwnd.
    EXPECT_APPROX_EQ(
        2.885f * QuicBandwidth::FromBytesAndTimeDelta(
                     kInitialCongestionWindowPackets * kDefaultTCPMSS,
                     rtt_stats_->initial_rtt()),
        sender_->PacingRate(/*bytes_in_flight=*/0), 0.01f);
  }
}

TEST_F(BbrSenderTest, MitigateCwndBootstrappingOvershoot) {
  SetQuicReloadableFlag(quic_bbr_mitigate_overly_large_bandwidth_sample, true);
  CreateDefaultSetup();
  bbr_sender_.AddBytesToTransfer(1 * 1024 * 1024);

  // Wait until an ACK comes back.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return !sender_->ExportDebugState().min_rtt.IsZero(); },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Bootstrap cwnd by a overly large bandwidth sample.
  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(8 * kTestLinkBandwidth,
                                            QuicTime::Delta::Zero(), false));
  QuicBandwidth pacing_rate = sender_->PacingRate(0);
  EXPECT_EQ(8 * kTestLinkBandwidth, pacing_rate);

  // Wait until pacing_rate decreases.
  simulator_result = simulator_.RunUntilOrTimeout(
      [this, pacing_rate]() { return sender_->PacingRate(0) < pacing_rate; },
      timeout);
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(BbrSender::STARTUP, sender_->ExportDebugState().mode);
  if (GetQuicReloadableFlag(quic_conservative_cwnd_and_pacing_gains)) {
    EXPECT_APPROX_EQ(2.0f * sender_->BandwidthEstimate(),
                     sender_->PacingRate(0), 0.01f);
  } else {
    EXPECT_APPROX_EQ(2.885f * sender_->BandwidthEstimate(),
                     sender_->PacingRate(0), 0.01f);
  }
}

TEST_F(BbrSenderTest, 200InitialCongestionWindowWithNetworkParameterAdjusted) {
  CreateDefaultSetup();

  bbr_sender_.AddBytesToTransfer(1 * 1024 * 1024);
  // Wait until an ACK comes back.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return !sender_->ExportDebugState().min_rtt.IsZero(); },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Bootstrap cwnd by a overly large bandwidth sample.
  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(1024 * kTestLinkBandwidth,
                                            QuicTime::Delta::Zero(), false));
  // Verify cwnd is capped at 200.
  EXPECT_EQ(200 * kDefaultTCPMSS,
            sender_->ExportDebugState().congestion_window);
  EXPECT_GT(1024 * kTestLinkBandwidth, sender_->PacingRate(0));
}

TEST_F(BbrSenderTest, 100InitialCongestionWindowWithNetworkParameterAdjusted) {
  SetConnectionOption(kICW1);
  CreateDefaultSetup();

  bbr_sender_.AddBytesToTransfer(1 * 1024 * 1024);
  // Wait until an ACK comes back.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return !sender_->ExportDebugState().min_rtt.IsZero(); },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Bootstrap cwnd by a overly large bandwidth sample.
  bbr_sender_.connection()->AdjustNetworkParameters(
      SendAlgorithmInterface::NetworkParams(1024 * kTestLinkBandwidth,
                                            QuicTime::Delta::Zero(), false));
  // Verify cwnd is capped at 100.
  EXPECT_EQ(100 * kDefaultTCPMSS,
            sender_->ExportDebugState().congestion_window);
  EXPECT_GT(1024 * kTestLinkBandwidth, sender_->PacingRate(0));
}

// Ensures bandwidth estimate does not change after a loss only event.
// Regression test for b/151239871.
TEST_F(BbrSenderTest, LossOnlyCongestionEvent) {
  CreateDefaultSetup();

  DriveOutOfStartup();
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  // Send some bursts, each burst increments round count by 1, since it only
  // generates small, app-limited samples, the max_bandwidth_ will not be
  // updated. At the end of all bursts, all estimates in max_bandwidth_ will
  // look very old such that any Update() will reset all estimates.
  SendBursts(20, 512, QuicTime::Delta::FromSeconds(3));

  QuicUnackedPacketMap* unacked_packets =
      QuicSentPacketManagerPeer::GetUnackedPacketMap(
          QuicConnectionPeer::GetSentPacketManager(bbr_sender_.connection()));
  // Run until we have something in flight.
  bbr_sender_.AddBytesToTransfer(50 * 1024 * 1024);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [&]() { return unacked_packets->bytes_in_flight() > 0; },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);

  const QuicBandwidth prior_bandwidth_estimate = sender_->BandwidthEstimate();
  EXPECT_APPROX_EQ(kTestLinkBandwidth, prior_bandwidth_estimate, 0.01f);

  // Lose the least unacked packet.
  LostPacketVector lost_packets;
  lost_packets.emplace_back(
      bbr_sender_.connection()->sent_packet_manager().GetLeastUnacked(),
      kDefaultMaxPacketSize);

  QuicTime now = simulator_.GetClock()->Now() + kTestRtt * 0.25;
  sender_->OnCongestionEvent(false, unacked_packets->bytes_in_flight(), now, {},
                             lost_packets);

  // Bandwidth estimate should not change for the loss only event.
  if (GetQuicReloadableFlag(quic_bbr_fix_zero_bw_on_loss_only_event)) {
    EXPECT_EQ(prior_bandwidth_estimate, sender_->BandwidthEstimate());
  }
}

}  // namespace test
}  // namespace quic
