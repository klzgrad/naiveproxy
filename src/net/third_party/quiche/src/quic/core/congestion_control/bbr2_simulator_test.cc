// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <utility>

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/tcp_cubic_sender_bytes.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_number.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_result.pb.h"
#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/traffic_policer.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    quic_bbr2_test_regression_mode,
    "",
    "One of a) 'record' to record test result (one file per test), or "
    "b) 'regress' to regress against recorded results, or "
    "c) <anything else> for non-regression mode.");

namespace quic {

using CyclePhase = Bbr2ProbeBwMode::CyclePhase;

namespace test {

// Use the initial CWND of 10, as 32 is too much for the test network.
const uint32_t kDefaultInitialCwndPackets = 10;
const uint32_t kDefaultInitialCwndBytes =
    kDefaultInitialCwndPackets * kDefaultTCPMSS;

struct LinkParams {
  LinkParams(int64_t kilo_bits_per_sec, int64_t delay_us)
      : bandwidth(QuicBandwidth::FromKBitsPerSecond(kilo_bits_per_sec)),
        delay(QuicTime::Delta::FromMicroseconds(delay_us)) {}
  QuicBandwidth bandwidth;
  QuicTime::Delta delay;
};

struct TrafficPolicerParams {
  std::string name = "policer";
  QuicByteCount initial_burst_size;
  QuicByteCount max_bucket_size;
  QuicBandwidth target_bandwidth = QuicBandwidth::Zero();
};

// All Bbr2DefaultTopologyTests uses the default network topology:
//
//            Sender
//               |
//               |  <-- local_link
//               |
//        Network switch
//               *  <-- the bottleneck queue in the direction
//               |          of the receiver
//               |
//               |  <-- test_link
//               |
//               |
//           Receiver
class DefaultTopologyParams {
 public:
  LinkParams local_link = {10000, 2000};
  LinkParams test_link = {4000, 30000};

  const simulator::SwitchPortNumber switch_port_count = 2;
  // Network switch queue capacity, in number of BDPs.
  float switch_queue_capacity_in_bdp = 2;

  quiche::QuicheOptional<TrafficPolicerParams> sender_policer_params;

  QuicBandwidth BottleneckBandwidth() const {
    return std::min(local_link.bandwidth, test_link.bandwidth);
  }

  // Round trip time of a single full size packet.
  QuicTime::Delta RTT() const {
    return 2 * (local_link.delay + test_link.delay +
                local_link.bandwidth.TransferTime(kMaxOutgoingPacketSize) +
                test_link.bandwidth.TransferTime(kMaxOutgoingPacketSize));
  }

  QuicByteCount BDP() const { return BottleneckBandwidth() * RTT(); }

  QuicByteCount SwitchQueueCapacity() const {
    return switch_queue_capacity_in_bdp * BDP();
  }

  std::string ToString() const {
    std::ostringstream os;
    os << "{ BottleneckBandwidth: " << BottleneckBandwidth()
       << " RTT: " << RTT() << " BDP: " << BDP()
       << " BottleneckQueueSize: " << SwitchQueueCapacity() << "}";
    return os.str();
  }
};

class Bbr2SimulatorTest : public QuicTest {
 protected:
  Bbr2SimulatorTest() : simulator_(&random_) {
    // Disable Ack Decimation by default, because it can significantly increase
    // srtt. Individual test can enable it via QuicConnectionPeer::SetAckMode().
    SetQuicReloadableFlag(quic_enable_ack_decimation, false);
  }

  void SetUp() override {
    if (GetQuicFlag(FLAGS_quic_bbr2_test_regression_mode) == "regress") {
      SendAlgorithmTestResult expected;
      ASSERT_TRUE(LoadSendAlgorithmTestResult(&expected));
      random_seed_ = expected.random_seed();
    } else {
      random_seed_ = QuicRandom::GetInstance()->RandUint64();
    }
    random_.set_seed(random_seed_);
    QUIC_LOG(INFO) << "Using random seed: " << random_seed_;
  }

  ~Bbr2SimulatorTest() override {
    const std::string regression_mode =
        GetQuicFlag(FLAGS_quic_bbr2_test_regression_mode);
    const QuicTime::Delta simulated_duration =
        SimulatedNow() - QuicTime::Zero();
    if (regression_mode == "record") {
      RecordSendAlgorithmTestResult(random_seed_,
                                    simulated_duration.ToMicroseconds());
    } else if (regression_mode == "regress") {
      CompareSendAlgorithmTestResult(simulated_duration.ToMicroseconds());
    }
  }

  QuicTime SimulatedNow() const { return simulator_.GetClock()->Now(); }

  uint64_t random_seed_;
  SimpleRandom random_;
  simulator::Simulator simulator_;
};

class Bbr2DefaultTopologyTest : public Bbr2SimulatorTest {
 protected:
  Bbr2DefaultTopologyTest()
      : sender_endpoint_(&simulator_,
                         "Sender",
                         "Receiver",
                         Perspective::IS_CLIENT,
                         TestConnectionId(42)),
        receiver_endpoint_(&simulator_,
                           "Receiver",
                           "Sender",
                           Perspective::IS_SERVER,
                           TestConnectionId(42)) {
    sender_ = SetupBbr2Sender(&sender_endpoint_, /*old_sender=*/nullptr);
  }

  ~Bbr2DefaultTopologyTest() {
    const auto* test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    const Bbr2Sender::DebugState& debug_state = sender_->ExportDebugState();
    QUIC_LOG(INFO) << "Bbr2DefaultTopologyTest." << test_info->name()
                   << " completed at simulated time: "
                   << SimulatedNow().ToDebuggingValue() / 1e6
                   << " sec. packet loss:"
                   << sender_loss_rate_in_packets() * 100
                   << "%, bw_hi:" << debug_state.bandwidth_hi;
  }

  QuicUnackedPacketMap* GetUnackedMap(QuicConnection* connection) {
    return QuicSentPacketManagerPeer::GetUnackedPacketMap(
        QuicConnectionPeer::GetSentPacketManager(connection));
  }

  Bbr2Sender* SetupBbr2Sender(simulator::QuicEndpoint* endpoint,
                              BbrSender* old_sender) {
    // Ownership of the sender will be overtaken by the endpoint.
    Bbr2Sender* sender = new Bbr2Sender(
        endpoint->connection()->clock()->Now(),
        endpoint->connection()->sent_packet_manager().GetRttStats(),
        GetUnackedMap(endpoint->connection()), kDefaultInitialCwndPackets,
        GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
        QuicConnectionPeer::GetStats(endpoint->connection()), old_sender);
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  void CreateNetwork(const DefaultTopologyParams& params) {
    QUIC_LOG(INFO) << "CreateNetwork with parameters: " << params.ToString();
    switch_ = std::make_unique<simulator::Switch>(&simulator_, "Switch",
                                                  params.switch_port_count,
                                                  params.SwitchQueueCapacity());

    // WARNING: The order to add links to network_links_ matters, because some
    // tests adjusts the link bandwidth on the fly.

    // Local link connects sender and port 1.
    network_links_.push_back(std::make_unique<simulator::SymmetricLink>(
        &sender_endpoint_, switch_->port(1), params.local_link.bandwidth,
        params.local_link.delay));

    // Test link connects receiver and port 2.
    if (params.sender_policer_params.has_value()) {
      const TrafficPolicerParams& policer_params =
          params.sender_policer_params.value();
      sender_policer_ = std::make_unique<simulator::TrafficPolicer>(
          &simulator_, policer_params.name, policer_params.initial_burst_size,
          policer_params.max_bucket_size, policer_params.target_bandwidth,
          switch_->port(2));
      network_links_.push_back(std::make_unique<simulator::SymmetricLink>(
          &receiver_endpoint_, sender_policer_.get(),
          params.test_link.bandwidth, params.test_link.delay));
    } else {
      network_links_.push_back(std::make_unique<simulator::SymmetricLink>(
          &receiver_endpoint_, switch_->port(2), params.test_link.bandwidth,
          params.test_link.delay));
    }
  }

  simulator::SymmetricLink* TestLink() { return network_links_[1].get(); }

  void DoSimpleTransfer(QuicByteCount transfer_size, QuicTime::Delta timeout) {
    sender_endpoint_.AddBytesToTransfer(transfer_size);
    // TODO(wub): consider rewriting this to run until the receiver actually
    // receives the intended amount of bytes.
    bool simulator_result = simulator_.RunUntilOrTimeout(
        [this]() { return sender_endpoint_.bytes_to_transfer() == 0; },
        timeout);
    EXPECT_TRUE(simulator_result)
        << "Simple transfer failed.  Bytes remaining: "
        << sender_endpoint_.bytes_to_transfer();
    QUIC_LOG(INFO) << "Simple transfer state: " << sender_->ExportDebugState();
  }

  // Drive the simulator by sending enough data to enter PROBE_BW.
  void DriveOutOfStartup(const DefaultTopologyParams& params) {
    ASSERT_FALSE(sender_->ExportDebugState().startup.full_bandwidth_reached);
    DoSimpleTransfer(1024 * 1024, QuicTime::Delta::FromSeconds(15));
    EXPECT_EQ(Bbr2Mode::PROBE_BW, sender_->ExportDebugState().mode);
    EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                     sender_->ExportDebugState().bandwidth_hi, 0.02f);
  }

  // Send |bytes|-sized bursts of data |number_of_bursts| times, waiting for
  // |wait_time| between each burst.
  void SendBursts(const DefaultTopologyParams& params,
                  size_t number_of_bursts,
                  QuicByteCount bytes,
                  QuicTime::Delta wait_time) {
    ASSERT_EQ(0u, sender_endpoint_.bytes_to_transfer());
    for (size_t i = 0; i < number_of_bursts; i++) {
      sender_endpoint_.AddBytesToTransfer(bytes);

      // Transfer data and wait for three seconds between each transfer.
      simulator_.RunFor(wait_time);

      // Ensure the connection did not time out.
      ASSERT_TRUE(sender_endpoint_.connection()->connected());
      ASSERT_TRUE(receiver_endpoint_.connection()->connected());
    }

    simulator_.RunFor(wait_time + params.RTT());
    ASSERT_EQ(0u, sender_endpoint_.bytes_to_transfer());
  }

  template <class TerminationPredicate>
  bool SendUntilOrTimeout(TerminationPredicate termination_predicate,
                          QuicTime::Delta timeout) {
    EXPECT_EQ(0u, sender_endpoint_.bytes_to_transfer());
    const QuicTime deadline = SimulatedNow() + timeout;
    do {
      sender_endpoint_.AddBytesToTransfer(4 * kDefaultTCPMSS);
      if (simulator_.RunUntilOrTimeout(
              [this]() { return sender_endpoint_.bytes_to_transfer() == 0; },
              deadline - SimulatedNow()) &&
          termination_predicate()) {
        return true;
      }
    } while (SimulatedNow() < deadline);
    return false;
  }

  void EnableAggregation(QuicByteCount aggregation_bytes,
                         QuicTime::Delta aggregation_timeout) {
    switch_->port_queue(1)->EnableAggregation(aggregation_bytes,
                                              aggregation_timeout);
  }

  void SetConnectionOption(QuicTag option) {
    QuicConfig config;
    QuicTagVector options;
    options.push_back(option);
    QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
    sender_->SetFromConfig(config, Perspective::IS_SERVER);
  }

  bool Bbr2ModeIsOneOf(const std::vector<Bbr2Mode>& expected_modes) const {
    const Bbr2Mode mode = sender_->ExportDebugState().mode;
    for (Bbr2Mode expected_mode : expected_modes) {
      if (mode == expected_mode) {
        return true;
      }
    }
    return false;
  }

  const RttStats* rtt_stats() {
    return sender_endpoint_.connection()->sent_packet_manager().GetRttStats();
  }

  QuicConnection* sender_connection() { return sender_endpoint_.connection(); }

  const QuicConnectionStats& sender_connection_stats() {
    return sender_connection()->GetStats();
  }

  QuicUnackedPacketMap* sender_unacked_map() {
    return GetUnackedMap(sender_connection());
  }

  float sender_loss_rate_in_packets() {
    return static_cast<float>(sender_connection_stats().packets_lost) /
           sender_connection_stats().packets_sent;
  }

  simulator::QuicEndpoint sender_endpoint_;
  simulator::QuicEndpoint receiver_endpoint_;
  Bbr2Sender* sender_;

  std::unique_ptr<simulator::Switch> switch_;
  std::unique_ptr<simulator::TrafficPolicer> sender_policer_;
  std::vector<std::unique_ptr<simulator::SymmetricLink>> network_links_;
};

TEST_F(Bbr2DefaultTopologyTest, NormalStartup) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  // Run until the full bandwidth is reached and check how many rounds it was.
  sender_endpoint_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().bandwidth_hi) {
          max_bw = sender_->ExportDebugState().bandwidth_hi;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().startup.full_bandwidth_reached;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(Bbr2Mode::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_EQ(3u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(
      3u,
      sender_->ExportDebugState().startup.round_trips_without_bandwidth_growth);
  EXPECT_EQ(0u, sender_connection_stats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Test a simple long data transfer in the default setup.
TEST_F(Bbr2DefaultTopologyTest, SimpleTransfer) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  // At startup make sure we are at the default.
  EXPECT_EQ(kDefaultInitialCwndBytes, sender_->GetCongestionWindow());
  // At startup make sure we can send.
  EXPECT_TRUE(sender_->CanSend(0));
  // And that window is un-affected.
  EXPECT_EQ(kDefaultInitialCwndBytes, sender_->GetCongestionWindow());

  // Verify that Sender is in slow start.
  EXPECT_TRUE(sender_->InSlowStart());

  // Verify that pacing rate is based on the initial RTT.
  QuicBandwidth expected_pacing_rate = QuicBandwidth::FromBytesAndTimeDelta(
      2.885 * kDefaultInitialCwndBytes, rtt_stats()->initial_rtt());
  EXPECT_APPROX_EQ(expected_pacing_rate.ToBitsPerSecond(),
                   sender_->PacingRate(0).ToBitsPerSecond(), 0.01f);

  ASSERT_GE(params.BDP(), kDefaultInitialCwndBytes + kDefaultTCPMSS);

  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(30));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));
  EXPECT_EQ(0u, sender_connection_stats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  // The margin here is quite high, since there exists a possibility that the
  // connection just exited high gain cycle.
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->smoothed_rtt(), 1.0f);
}

TEST_F(Bbr2DefaultTopologyTest, SimpleTransferSmallBuffer) {
  DefaultTopologyParams params;
  params.switch_queue_capacity_in_bdp = 0.5;
  CreateNetwork(params);

  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(30));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));
  EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                   sender_->ExportDebugState().bandwidth_hi, 0.01f);
  EXPECT_GE(sender_connection_stats().packets_lost, 0u);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

TEST_F(Bbr2DefaultTopologyTest, SimpleTransfer2RTTAggregationBytes) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  DefaultTopologyParams params;
  CreateNetwork(params);
  // 2 RTTs of aggregation, with a max of 10kb.
  EnableAggregation(10 * 1024, 2 * params.RTT());

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));

  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                     sender_->ExportDebugState().bandwidth_hi, 0.01f);
  } else {
    EXPECT_LE(params.BottleneckBandwidth() * 0.99f,
              sender_->ExportDebugState().bandwidth_hi);
    // TODO(b/36022633): Bandwidth sampler overestimates with aggregation.
    EXPECT_GE(params.BottleneckBandwidth() * 1.5f,
              sender_->ExportDebugState().bandwidth_hi);
  }
  EXPECT_LE(sender_loss_rate_in_packets(), 0.05);
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(params.RTT() * 4, rtt_stats()->smoothed_rtt());
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->min_rtt(), 0.2f);
}

TEST_F(Bbr2DefaultTopologyTest, SimpleTransferAckDecimation) {
  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    SetConnectionOption(kBSAO);
  }
  // Enable Ack Decimation on the receiver.
  QuicConnectionPeer::SetAckMode(receiver_endpoint_.connection(),
                                 AckMode::ACK_DECIMATION);
  DefaultTopologyParams params;
  CreateNetwork(params);

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));

  if (GetQuicReloadableFlag(
          quic_avoid_overestimate_bandwidth_with_aggregation)) {
    EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                     sender_->ExportDebugState().bandwidth_hi, 0.01f);
  } else {
    EXPECT_LE(params.BottleneckBandwidth() * 0.99f,
              sender_->ExportDebugState().bandwidth_hi);
    // TODO(b/36022633): Bandwidth sampler overestimates with aggregation.
    EXPECT_GE(params.BottleneckBandwidth() * 1.1f,
              sender_->ExportDebugState().bandwidth_hi);
  }
  EXPECT_LE(sender_loss_rate_in_packets(), 0.001);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(params.RTT() * 3, rtt_stats()->smoothed_rtt());
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->min_rtt(), 0.1f);
}

// Test Bbr2's reaction to a 100x bandwidth decrease during a transfer.
TEST_F(Bbr2DefaultTopologyTest, QUIC_SLOW_TEST(BandwidthDecrease)) {
  DefaultTopologyParams params;
  params.local_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(15000);
  params.test_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(10000);
  CreateNetwork(params);

  sender_endpoint_.AddBytesToTransfer(20 * 1024 * 1024);

  // We can transfer ~12MB in the first 10 seconds. The rest ~8MB needs about
  // 640 seconds.
  simulator_.RunFor(QuicTime::Delta::FromSeconds(10));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));
  QUIC_LOG(INFO) << "Bandwidth decreasing at time " << SimulatedNow();

  EXPECT_APPROX_EQ(params.test_link.bandwidth,
                   sender_->ExportDebugState().bandwidth_est, 0.1f);
  EXPECT_EQ(0u, sender_connection_stats().packets_lost);

  // Now decrease the bottleneck bandwidth from 10Mbps to 100Kbps.
  params.test_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(100);
  TestLink()->set_bandwidth(params.test_link.bandwidth);

  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_endpoint_.bytes_to_transfer() == 0; },
      QuicTime::Delta::FromSeconds(800));
  EXPECT_TRUE(simulator_result);
}

// Test Bbr2's reaction to a 100x bandwidth increase during a transfer.
TEST_F(Bbr2DefaultTopologyTest, QUIC_SLOW_TEST(BandwidthIncrease)) {
  DefaultTopologyParams params;
  params.local_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(15000);
  params.test_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(100);
  CreateNetwork(params);

  sender_endpoint_.AddBytesToTransfer(20 * 1024 * 1024);

  simulator_.RunFor(QuicTime::Delta::FromSeconds(15));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));
  QUIC_LOG(INFO) << "Bandwidth increasing at time " << SimulatedNow();

  EXPECT_APPROX_EQ(params.test_link.bandwidth,
                   sender_->ExportDebugState().bandwidth_est, 0.1f);
  EXPECT_LE(sender_loss_rate_in_packets(), 0.30);

  // Now increase the bottleneck bandwidth from 100Kbps to 10Mbps.
  params.test_link.bandwidth = QuicBandwidth::FromKBitsPerSecond(10000);
  TestLink()->set_bandwidth(params.test_link.bandwidth);

  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_endpoint_.bytes_to_transfer() == 0; },
      QuicTime::Delta::FromSeconds(50));
  EXPECT_TRUE(simulator_result);
}

// Test the number of losses incurred by the startup phase in a situation when
// the buffer is less than BDP.
TEST_F(Bbr2DefaultTopologyTest, PacketLossOnSmallBufferStartup) {
  DefaultTopologyParams params;
  params.switch_queue_capacity_in_bdp = 0.5;
  CreateNetwork(params);

  DriveOutOfStartup(params);
  EXPECT_LE(sender_loss_rate_in_packets(), 0.20);
}

// Verify the behavior of the algorithm in the case when the connection sends
// small bursts of data after sending continuously for a while.
TEST_F(Bbr2DefaultTopologyTest, ApplicationLimitedBursts) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  SendBursts(params, 20, 512, QuicTime::Delta::FromSeconds(3));
  EXPECT_TRUE(sender_->ExportDebugState().last_sample_is_app_limited);
  EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                   sender_->ExportDebugState().bandwidth_hi, 0.01f);
}

// Verify the behavior of the algorithm in the case when the connection sends
// small bursts of data and then starts sending continuously.
TEST_F(Bbr2DefaultTopologyTest, ApplicationLimitedBurstsWithoutPrior) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  SendBursts(params, 40, 512, QuicTime::Delta::FromSeconds(3));
  EXPECT_TRUE(sender_->ExportDebugState().last_sample_is_app_limited);

  DriveOutOfStartup(params);
  EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                   sender_->ExportDebugState().bandwidth_hi, 0.01f);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

// Verify that the DRAIN phase works correctly.
TEST_F(Bbr2DefaultTopologyTest, Drain) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(10);
  // Get the queue at the bottleneck, which is the outgoing queue at the port to
  // which the receiver is connected.
  const simulator::Queue* queue = switch_->port_queue(2);
  bool simulator_result;

  // We have no intention of ever finishing this transfer.
  sender_endpoint_.AddBytesToTransfer(100 * 1024 * 1024);

  // Run the startup, and verify that it fills up the queue.
  ASSERT_EQ(Bbr2Mode::STARTUP, sender_->ExportDebugState().mode);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode != Bbr2Mode::STARTUP;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(Bbr2Mode::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_APPROX_EQ(sender_->BandwidthEstimate() * (1 / 2.885f),
                   sender_->PacingRate(0), 0.01f);
  // BBR uses CWND gain of 2.88 during STARTUP, hence it will fill the buffer
  // with approximately 1.88 BDPs.  Here, we use 1.5 to give some margin for
  // error.
  EXPECT_GE(queue->bytes_queued(), 1.5 * params.BDP());

  // Observe increased RTT due to bufferbloat.
  const QuicTime::Delta queueing_delay =
      params.test_link.bandwidth.TransferTime(queue->bytes_queued());
  EXPECT_APPROX_EQ(params.RTT() + queueing_delay, rtt_stats()->latest_rtt(),
                   0.1f);

  // Transition to the drain phase and verify that it makes the queue
  // have at most a BDP worth of packets.
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() { return sender_->ExportDebugState().mode != Bbr2Mode::DRAIN; },
      timeout);
  ASSERT_TRUE(simulator_result);
  ASSERT_EQ(Bbr2Mode::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_LE(queue->bytes_queued(), params.BDP());

  // Wait for a few round trips and ensure we're in appropriate phase of gain
  // cycling before taking an RTT measurement.
  const QuicRoundTripCount start_round_trip =
      sender_->ExportDebugState().round_trip_count;
  simulator_result = simulator_.RunUntilOrTimeout(
      [this, start_round_trip]() {
        const auto& debug_state = sender_->ExportDebugState();
        QuicRoundTripCount rounds_passed =
            debug_state.round_trip_count - start_round_trip;
        return rounds_passed >= 4 && debug_state.mode == Bbr2Mode::PROBE_BW &&
               debug_state.probe_bw.phase == CyclePhase::PROBE_REFILL;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Observe the bufferbloat go away.
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->smoothed_rtt(), 0.1f);
}

// Ensure that a connection that is app-limited and is at sufficiently low
// bandwidth will not exit high gain phase, and similarly ensure that the
// connection will exit low gain early if the number of bytes in flight is low.
TEST_F(Bbr2DefaultTopologyTest, InFlightAwareGainCycling) {
  DefaultTopologyParams params;
  CreateNetwork(params);
  DriveOutOfStartup(params);

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result;

  // Start a few cycles prior to the high gain one.
  simulator_result = SendUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().probe_bw.phase ==
               CyclePhase::PROBE_REFILL;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Send at 10% of available rate.  Run for 3 seconds, checking in the middle
  // and at the end.  The pacing gain should be high throughout.
  QuicBandwidth target_bandwidth = 0.1f * params.BottleneckBandwidth();
  QuicTime::Delta burst_interval = QuicTime::Delta::FromMilliseconds(300);
  for (int i = 0; i < 2; i++) {
    SendBursts(params, 5, target_bandwidth * burst_interval, burst_interval);
    EXPECT_EQ(Bbr2Mode::PROBE_BW, sender_->ExportDebugState().mode);
    EXPECT_EQ(CyclePhase::PROBE_UP, sender_->ExportDebugState().probe_bw.phase);
    EXPECT_APPROX_EQ(params.BottleneckBandwidth(),
                     sender_->ExportDebugState().bandwidth_hi, 0.01f);
  }

  // Now that in-flight is almost zero and the pacing gain is still above 1,
  // send approximately 1.25 BDPs worth of data.  This should cause the
  // PROBE_BW mode to enter low gain cycle(PROBE_DOWN), and exit it earlier than
  // one min_rtt due to running out of data to send.
  sender_endpoint_.AddBytesToTransfer(1.3 * params.BDP());
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().probe_bw.phase ==
               CyclePhase::PROBE_DOWN;
      },
      timeout);
  ASSERT_TRUE(simulator_result);
  simulator_.RunFor(0.75 * sender_->ExportDebugState().min_rtt);
  EXPECT_EQ(Bbr2Mode::PROBE_BW, sender_->ExportDebugState().mode);
  EXPECT_EQ(CyclePhase::PROBE_CRUISE,
            sender_->ExportDebugState().probe_bw.phase);
}

// Test exiting STARTUP earlier upon loss due to loss.
TEST_F(Bbr2DefaultTopologyTest, ExitStartupDueToLoss) {
  DefaultTopologyParams params;
  params.switch_queue_capacity_in_bdp = 0.5;
  CreateNetwork(params);

  // Run until the full bandwidth is reached and check how many rounds it was.
  sender_endpoint_.AddBytesToTransfer(12 * 1024 * 1024);
  QuicRoundTripCount max_bw_round = 0;
  QuicBandwidth max_bw(QuicBandwidth::Zero());
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this, &max_bw, &max_bw_round]() {
        if (max_bw < sender_->ExportDebugState().bandwidth_hi) {
          max_bw = sender_->ExportDebugState().bandwidth_hi;
          max_bw_round = sender_->ExportDebugState().round_trip_count;
        }
        return sender_->ExportDebugState().startup.full_bandwidth_reached;
      },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);
  EXPECT_EQ(Bbr2Mode::DRAIN, sender_->ExportDebugState().mode);
  EXPECT_GE(2u, sender_->ExportDebugState().round_trip_count - max_bw_round);
  EXPECT_EQ(
      1u,
      sender_->ExportDebugState().startup.round_trips_without_bandwidth_growth);
  EXPECT_NE(0u, sender_connection_stats().packets_lost);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
}

TEST_F(Bbr2DefaultTopologyTest, SenderPoliced) {
  DefaultTopologyParams params;
  params.sender_policer_params = TrafficPolicerParams();
  params.sender_policer_params->initial_burst_size = 1000 * 10;
  params.sender_policer_params->max_bucket_size = 1000 * 100;
  params.sender_policer_params->target_bandwidth =
      params.BottleneckBandwidth() * 0.25;

  CreateNetwork(params);

  ASSERT_GE(params.BDP(), kDefaultInitialCwndBytes + kDefaultTCPMSS);

  DoSimpleTransfer(3 * 1024 * 1024, QuicTime::Delta::FromSeconds(30));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));
  // TODO(wub): Fix (long-term) bandwidth overestimation in policer mode, then
  // reduce the loss rate upper bound.
  EXPECT_LE(sender_loss_rate_in_packets(), 0.30);
}

// TODO(wub): Add other slowstart stats to BBRv2.
TEST_F(Bbr2DefaultTopologyTest, StartupStats) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);
  ASSERT_FALSE(sender_->InSlowStart());

  const QuicConnectionStats& stats = sender_connection_stats();
  EXPECT_EQ(1u, stats.slowstart_count);
  EXPECT_FALSE(stats.slowstart_duration.IsRunning());
  EXPECT_THAT(stats.slowstart_duration.GetTotalElapsedTime(),
              AllOf(Ge(QuicTime::Delta::FromMilliseconds(500)),
                    Le(QuicTime::Delta::FromMilliseconds(1500))));
  EXPECT_EQ(stats.slowstart_duration.GetTotalElapsedTime(),
            QuicConnectionPeer::GetSentPacketManager(sender_connection())
                ->GetSlowStartDuration());
}

TEST_F(Bbr2DefaultTopologyTest, ProbeUpAdaptInflightHiGradually) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);

  AckedPacketVector acked_packets;
  QuicPacketNumber acked_packet_number =
      sender_unacked_map()->GetLeastUnacked();
  for (auto& info : *sender_unacked_map()) {
    acked_packets.emplace_back(acked_packet_number++, info.bytes_sent,
                               SimulatedNow());
  }

  // Advance time significantly so the OnCongestionEvent enters PROBE_REFILL.
  QuicTime now = SimulatedNow() + QuicTime::Delta::FromSeconds(5);
  auto next_packet_number = sender_unacked_map()->largest_sent_packet() + 1;
  sender_->OnCongestionEvent(
      /*rtt_updated=*/true, sender_unacked_map()->bytes_in_flight(), now,
      acked_packets, {});
  ASSERT_EQ(CyclePhase::PROBE_REFILL,
            sender_->ExportDebugState().probe_bw.phase);

  // Send and Ack one packet to exit app limited and enter PROBE_UP.
  sender_->OnPacketSent(now, /*bytes_in_flight=*/0, next_packet_number++,
                        kDefaultMaxPacketSize, HAS_RETRANSMITTABLE_DATA);
  now = now + params.RTT();
  sender_->OnCongestionEvent(
      /*rtt_updated=*/true, kDefaultMaxPacketSize, now,
      {AckedPacket(next_packet_number - 1, kDefaultMaxPacketSize, now)}, {});
  ASSERT_EQ(CyclePhase::PROBE_UP, sender_->ExportDebugState().probe_bw.phase);

  // Send 2 packets and lose the first one(50% loss) to exit PROBE_UP.
  for (uint64_t i = 0; i < 2; ++i) {
    sender_->OnPacketSent(now, /*bytes_in_flight=*/i * kDefaultMaxPacketSize,
                          next_packet_number++, kDefaultMaxPacketSize,
                          HAS_RETRANSMITTABLE_DATA);
  }
  now = now + params.RTT();
  sender_->OnCongestionEvent(
      /*rtt_updated=*/true, kDefaultMaxPacketSize, now,
      {AckedPacket(next_packet_number - 1, kDefaultMaxPacketSize, now)},
      {LostPacket(next_packet_number - 2, kDefaultMaxPacketSize)});

  QuicByteCount inflight_hi = sender_->ExportDebugState().inflight_hi;
  EXPECT_LT(2 * kDefaultMaxPacketSize, inflight_hi);
}

// Ensures bandwidth estimate does not change after a loss only event.
TEST_F(Bbr2DefaultTopologyTest, LossOnlyCongestionEvent) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);

  // Send some bursts, each burst increments round count by 1, since it only
  // generates small, app-limited samples, the max_bandwidth_filter_ will not be
  // updated.
  SendBursts(params, 20, 512, QuicTime::Delta::FromSeconds(3));

  // Run until we have something in flight.
  sender_endpoint_.AddBytesToTransfer(50 * 1024 * 1024);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [&]() { return sender_unacked_map()->bytes_in_flight() > 0; },
      QuicTime::Delta::FromSeconds(5));
  ASSERT_TRUE(simulator_result);

  const QuicBandwidth prior_bandwidth_estimate = sender_->BandwidthEstimate();
  EXPECT_APPROX_EQ(params.BottleneckBandwidth(), prior_bandwidth_estimate,
                   0.01f);

  // Lose the least unacked packet.
  LostPacketVector lost_packets;
  lost_packets.emplace_back(
      sender_connection()->sent_packet_manager().GetLeastUnacked(),
      kDefaultMaxPacketSize);

  QuicTime now = simulator_.GetClock()->Now() + params.RTT() * 0.25;
  sender_->OnCongestionEvent(false, sender_unacked_map()->bytes_in_flight(),
                             now, {}, lost_packets);

  // Bandwidth estimate should not change for the loss only event.
  EXPECT_EQ(prior_bandwidth_estimate, sender_->BandwidthEstimate());
}

// After quiescence, if the sender is in PROBE_RTT, it should transition to
// PROBE_BW immediately on the first sent packet after quiescence.
TEST_F(Bbr2DefaultTopologyTest, ProbeRttAfterQuiescenceImmediatelyExits) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(15);
  bool simulator_result;

  // Keep sending until reach PROBE_RTT.
  simulator_result = SendUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().mode == Bbr2Mode::PROBE_RTT;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  // Wait for entering a quiescence of 5 seconds.
  ASSERT_TRUE(simulator_.RunUntilOrTimeout(
      [this]() {
        return sender_unacked_map()->bytes_in_flight() == 0 &&
               sender_->ExportDebugState().mode == Bbr2Mode::PROBE_RTT;
      },
      timeout));

  simulator_.RunFor(QuicTime::Delta::FromSeconds(5));

  // Send one packet to exit quiescence.
  EXPECT_EQ(sender_->ExportDebugState().mode, Bbr2Mode::PROBE_RTT);
  sender_->OnPacketSent(SimulatedNow(), /*bytes_in_flight=*/0,
                        sender_unacked_map()->largest_sent_packet() + 1,
                        kDefaultMaxPacketSize, HAS_RETRANSMITTABLE_DATA);
  if (GetQuicReloadableFlag(quic_bbr2_avoid_unnecessary_probe_rtt)) {
    EXPECT_EQ(sender_->ExportDebugState().mode, Bbr2Mode::PROBE_BW);
  } else {
    EXPECT_EQ(sender_->ExportDebugState().mode, Bbr2Mode::PROBE_RTT);
  }
}

TEST_F(Bbr2DefaultTopologyTest, ProbeBwAfterQuiescencePostponeMinRttTimestamp) {
  DefaultTopologyParams params;
  CreateNetwork(params);

  DriveOutOfStartup(params);

  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  bool simulator_result;

  // Keep sending until reach PROBE_REFILL.
  simulator_result = SendUntilOrTimeout(
      [this]() {
        return sender_->ExportDebugState().probe_bw.phase ==
               CyclePhase::PROBE_REFILL;
      },
      timeout);
  ASSERT_TRUE(simulator_result);

  const QuicTime min_rtt_timestamp_before_idle =
      sender_->ExportDebugState().min_rtt_timestamp;

  // Wait for entering a quiescence of 15 seconds.
  ASSERT_TRUE(simulator_.RunUntilOrTimeout(
      [this]() { return sender_unacked_map()->bytes_in_flight() == 0; },
      params.RTT()));

  simulator_.RunFor(QuicTime::Delta::FromSeconds(15));

  // Send some data to exit quiescence.
  SendBursts(params, 1, kDefaultTCPMSS, QuicTime::Delta::Zero());
  const QuicTime min_rtt_timestamp_after_idle =
      sender_->ExportDebugState().min_rtt_timestamp;
  if (GetQuicReloadableFlag(quic_bbr2_avoid_unnecessary_probe_rtt)) {
    EXPECT_LT(min_rtt_timestamp_before_idle + QuicTime::Delta::FromSeconds(14),
              min_rtt_timestamp_after_idle);
  } else {
    EXPECT_EQ(min_rtt_timestamp_before_idle, min_rtt_timestamp_after_idle);
  }
}

// Regression test for http://shortn/_Jt1QWtshAM.
TEST_F(Bbr2DefaultTopologyTest, SwitchToBbr2MidConnection) {
  if (!GetQuicReloadableFlag(quic_bbr_copy_sampler_state_from_v1_to_v2)) {
    return;
  }
  QuicTime now = QuicTime::Zero();
  BbrSender old_sender(sender_connection()->clock()->Now(),
                       sender_connection()->sent_packet_manager().GetRttStats(),
                       GetUnackedMap(sender_connection()),
                       kDefaultInitialCwndPackets,
                       GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
                       QuicConnectionPeer::GetStats(sender_connection()));

  QuicPacketNumber next_packet_number(1);

  // Send packets 1-4.
  while (next_packet_number < QuicPacketNumber(5)) {
    now = now + QuicTime::Delta::FromMilliseconds(10);

    old_sender.OnPacketSent(now, /*bytes_in_flight=*/0, next_packet_number++,
                            /*bytes=*/1350, HAS_RETRANSMITTABLE_DATA);
  }

  // Switch from |old_sender| to |sender_|.
  sender_ = SetupBbr2Sender(&sender_endpoint_, &old_sender);

  // Send packets 5-7.
  now = now + QuicTime::Delta::FromMilliseconds(10);
  sender_->OnPacketSent(now, /*bytes_in_flight=*/1350, next_packet_number++,
                        /*bytes=*/23, NO_RETRANSMITTABLE_DATA);

  now = now + QuicTime::Delta::FromMilliseconds(10);
  sender_->OnPacketSent(now, /*bytes_in_flight=*/1350, next_packet_number++,
                        /*bytes=*/767, HAS_RETRANSMITTABLE_DATA);

  QuicByteCount bytes_in_flight = 767;
  while (next_packet_number < QuicPacketNumber(30)) {
    now = now + QuicTime::Delta::FromMilliseconds(10);
    bytes_in_flight += 1350;
    sender_->OnPacketSent(now, bytes_in_flight, next_packet_number++,
                          /*bytes=*/1350, HAS_RETRANSMITTABLE_DATA);
  }

  // Ack 1 & 2.
  AckedPacketVector acked = {
      AckedPacket(QuicPacketNumber(1), /*bytes_acked=*/0, QuicTime::Zero()),
      AckedPacket(QuicPacketNumber(2), /*bytes_acked=*/0, QuicTime::Zero()),
  };
  now = now + QuicTime::Delta::FromMilliseconds(2000);
  sender_->OnCongestionEvent(true, bytes_in_flight, now, acked, {});

  // Send 30-41.
  while (next_packet_number < QuicPacketNumber(42)) {
    now = now + QuicTime::Delta::FromMilliseconds(10);
    bytes_in_flight += 1350;
    sender_->OnPacketSent(now, bytes_in_flight, next_packet_number++,
                          /*bytes=*/1350, HAS_RETRANSMITTABLE_DATA);
  }

  // Ack 3.
  acked = {
      AckedPacket(QuicPacketNumber(3), /*bytes_acked=*/0, QuicTime::Zero()),
  };
  now = now + QuicTime::Delta::FromMilliseconds(2000);
  sender_->OnCongestionEvent(true, bytes_in_flight, now, acked, {});

  // Send 42.
  now = now + QuicTime::Delta::FromMilliseconds(10);
  bytes_in_flight += 1350;
  sender_->OnPacketSent(now, bytes_in_flight, next_packet_number++,
                        /*bytes=*/1350, HAS_RETRANSMITTABLE_DATA);

  // Ack 4-7.
  acked = {
      AckedPacket(QuicPacketNumber(4), /*bytes_acked=*/0, QuicTime::Zero()),
      AckedPacket(QuicPacketNumber(5), /*bytes_acked=*/0, QuicTime::Zero()),
      AckedPacket(QuicPacketNumber(6), /*bytes_acked=*/767, QuicTime::Zero()),
      AckedPacket(QuicPacketNumber(7), /*bytes_acked=*/1350, QuicTime::Zero()),
  };
  now = now + QuicTime::Delta::FromMilliseconds(2000);
  sender_->OnCongestionEvent(true, bytes_in_flight, now, acked, {});
  EXPECT_FALSE(sender_->BandwidthEstimate().IsZero());
}

// All Bbr2MultiSenderTests uses the following network topology:
//
//   Sender 0  (A Bbr2Sender)
//       |
//       | <-- local_links[0]
//       |
//       |  Sender N (1 <= N < kNumLocalLinks) (May or may not be a
//       Bbr2Sender) |      | |      | <-- local_links[N] |      |
//    Network switch
//           *  <-- the bottleneck queue in the direction
//           |          of the receiver
//           |
//           |  <-- test_link
//           |
//           |
//       Receiver
class MultiSenderTopologyParams {
 public:
  static const size_t kNumLocalLinks = 8;
  std::array<LinkParams, kNumLocalLinks> local_links = {
      LinkParams(10000, 1987), LinkParams(10000, 1993), LinkParams(10000, 1997),
      LinkParams(10000, 1999), LinkParams(10000, 2003), LinkParams(10000, 2011),
      LinkParams(10000, 2017), LinkParams(10000, 2027),
  };

  LinkParams test_link = LinkParams(4000, 30000);

  const simulator::SwitchPortNumber switch_port_count = kNumLocalLinks + 1;

  // Network switch queue capacity, in number of BDPs.
  float switch_queue_capacity_in_bdp = 2;

  QuicBandwidth BottleneckBandwidth() const {
    // Make sure all local links have a higher bandwidth than the test link.
    for (size_t i = 0; i < local_links.size(); ++i) {
      CHECK_GT(local_links[i].bandwidth, test_link.bandwidth);
    }
    return test_link.bandwidth;
  }

  // Sender n's round trip time of a single full size packet.
  QuicTime::Delta Rtt(size_t n) const {
    return 2 * (local_links[n].delay + test_link.delay +
                local_links[n].bandwidth.TransferTime(kMaxOutgoingPacketSize) +
                test_link.bandwidth.TransferTime(kMaxOutgoingPacketSize));
  }

  QuicByteCount Bdp(size_t n) const { return BottleneckBandwidth() * Rtt(n); }

  QuicByteCount SwitchQueueCapacity() const {
    return switch_queue_capacity_in_bdp * Bdp(1);
  }

  std::string ToString() const {
    std::ostringstream os;
    os << "{ BottleneckBandwidth: " << BottleneckBandwidth();
    for (size_t i = 0; i < local_links.size(); ++i) {
      os << " RTT_" << i << ": " << Rtt(i) << " BDP_" << i << ": " << Bdp(i);
    }
    os << " BottleneckQueueSize: " << SwitchQueueCapacity() << "}";
    return os.str();
  }
};

class Bbr2MultiSenderTest : public Bbr2SimulatorTest {
 protected:
  Bbr2MultiSenderTest() {
    uint64_t first_connection_id = 42;
    std::vector<simulator::QuicEndpointBase*> receiver_endpoint_pointers;
    for (size_t i = 0; i < MultiSenderTopologyParams::kNumLocalLinks; ++i) {
      std::string sender_name = quiche::QuicheStrCat("Sender", i + 1);
      std::string receiver_name = quiche::QuicheStrCat("Receiver", i + 1);
      sender_endpoints_.push_back(std::make_unique<simulator::QuicEndpoint>(
          &simulator_, sender_name, receiver_name, Perspective::IS_CLIENT,
          TestConnectionId(first_connection_id + i)));
      receiver_endpoints_.push_back(std::make_unique<simulator::QuicEndpoint>(
          &simulator_, receiver_name, sender_name, Perspective::IS_SERVER,
          TestConnectionId(first_connection_id + i)));
      receiver_endpoint_pointers.push_back(receiver_endpoints_.back().get());
    }
    receiver_multiplexer_ =
        std::make_unique<simulator::QuicEndpointMultiplexer>(
            "Receiver multiplexer", receiver_endpoint_pointers);
    sender_1_ = SetupBbr2Sender(sender_endpoints_[0].get());
  }

  ~Bbr2MultiSenderTest() {
    const auto* test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    QUIC_LOG(INFO) << "Bbr2MultiSenderTest." << test_info->name()
                   << " completed at simulated time: "
                   << SimulatedNow().ToDebuggingValue() / 1e6
                   << " sec. Per sender stats:";
    for (size_t i = 0; i < sender_endpoints_.size(); ++i) {
      QUIC_LOG(INFO) << "sender[" << i << "]: "
                     << sender_connection(i)
                            ->sent_packet_manager()
                            .GetSendAlgorithm()
                            ->GetCongestionControlType()
                     << ", packet_loss:"
                     << 100.0 * sender_loss_rate_in_packets(i) << "%";
    }
  }

  Bbr2Sender* SetupBbr2Sender(simulator::QuicEndpoint* endpoint) {
    // Ownership of the sender will be overtaken by the endpoint.
    Bbr2Sender* sender = new Bbr2Sender(
        endpoint->connection()->clock()->Now(),
        endpoint->connection()->sent_packet_manager().GetRttStats(),
        QuicSentPacketManagerPeer::GetUnackedPacketMap(
            QuicConnectionPeer::GetSentPacketManager(endpoint->connection())),
        kDefaultInitialCwndPackets,
        GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
        QuicConnectionPeer::GetStats(endpoint->connection()), nullptr);
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  BbrSender* SetupBbrSender(simulator::QuicEndpoint* endpoint) {
    // Ownership of the sender will be overtaken by the endpoint.
    BbrSender* sender = new BbrSender(
        endpoint->connection()->clock()->Now(),
        endpoint->connection()->sent_packet_manager().GetRttStats(),
        QuicSentPacketManagerPeer::GetUnackedPacketMap(
            QuicConnectionPeer::GetSentPacketManager(endpoint->connection())),
        kDefaultInitialCwndPackets,
        GetQuicFlag(FLAGS_quic_max_congestion_window), &random_,
        QuicConnectionPeer::GetStats(endpoint->connection()));
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  // reno => Reno. !reno => Cubic.
  TcpCubicSenderBytes* SetupTcpSender(simulator::QuicEndpoint* endpoint,
                                      bool reno) {
    // Ownership of the sender will be overtaken by the endpoint.
    TcpCubicSenderBytes* sender = new TcpCubicSenderBytes(
        endpoint->connection()->clock(),
        endpoint->connection()->sent_packet_manager().GetRttStats(), reno,
        kDefaultInitialCwndPackets,
        GetQuicFlag(FLAGS_quic_max_congestion_window),
        QuicConnectionPeer::GetStats(endpoint->connection()));
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  void CreateNetwork(const MultiSenderTopologyParams& params) {
    QUIC_LOG(INFO) << "CreateNetwork with parameters: " << params.ToString();
    switch_ = std::make_unique<simulator::Switch>(&simulator_, "Switch",
                                                  params.switch_port_count,
                                                  params.SwitchQueueCapacity());

    network_links_.push_back(std::make_unique<simulator::SymmetricLink>(
        receiver_multiplexer_.get(), switch_->port(1),
        params.test_link.bandwidth, params.test_link.delay));
    for (size_t i = 0; i < MultiSenderTopologyParams::kNumLocalLinks; ++i) {
      simulator::SwitchPortNumber port_number = i + 2;
      network_links_.push_back(std::make_unique<simulator::SymmetricLink>(
          sender_endpoints_[i].get(), switch_->port(port_number),
          params.local_links[i].bandwidth, params.local_links[i].delay));
    }
  }

  QuicConnection* sender_connection(size_t which) {
    return sender_endpoints_[which]->connection();
  }

  const QuicConnectionStats& sender_connection_stats(size_t which) {
    return sender_connection(which)->GetStats();
  }

  float sender_loss_rate_in_packets(size_t which) {
    return static_cast<float>(sender_connection_stats(which).packets_lost) /
           sender_connection_stats(which).packets_sent;
  }

  std::vector<std::unique_ptr<simulator::QuicEndpoint>> sender_endpoints_;
  std::vector<std::unique_ptr<simulator::QuicEndpoint>> receiver_endpoints_;
  std::unique_ptr<simulator::QuicEndpointMultiplexer> receiver_multiplexer_;
  Bbr2Sender* sender_1_;

  std::unique_ptr<simulator::Switch> switch_;
  std::vector<std::unique_ptr<simulator::SymmetricLink>> network_links_;
};

TEST_F(Bbr2MultiSenderTest, Bbr2VsBbr2) {
  SetupBbr2Sender(sender_endpoints_[1].get());

  MultiSenderTopologyParams params;
  CreateNetwork(params);

  const QuicByteCount transfer_size = 10 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time;

  // Transfer 10% of data in first transfer.
  sender_endpoints_[0]->AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() >= 0.1 * transfer_size;
      },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  sender_endpoints_[1]->AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() == transfer_size &&
               receiver_endpoints_[1]->bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}

TEST_F(Bbr2MultiSenderTest, QUIC_SLOW_TEST(MultipleBbr2s)) {
  const int kTotalNumSenders = 6;
  for (int i = 1; i < kTotalNumSenders; ++i) {
    SetupBbr2Sender(sender_endpoints_[i].get());
  }

  MultiSenderTopologyParams params;
  CreateNetwork(params);

  const QuicByteCount transfer_size = 10 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time
                 << ". Now: " << SimulatedNow();

  // Start all transfers.
  for (int i = 0; i < kTotalNumSenders; ++i) {
    if (i != 0) {
      const QuicTime sender_start_time =
          SimulatedNow() + QuicTime::Delta::FromSeconds(2);
      bool simulator_result = simulator_.RunUntilOrTimeout(
          [&]() { return SimulatedNow() >= sender_start_time; }, transfer_time);
      ASSERT_TRUE(simulator_result);
    }

    sender_endpoints_[i]->AddBytesToTransfer(transfer_size);
  }

  // Wait for all transfers to finish.
  QuicTime::Delta expected_total_transfer_time_upper_bound =
      QuicTime::Delta::FromMicroseconds(kTotalNumSenders *
                                        transfer_time.ToMicroseconds() * 1.1);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        for (int i = 0; i < kTotalNumSenders; ++i) {
          if (receiver_endpoints_[i]->bytes_received() < transfer_size) {
            return false;
          }
        }
        return true;
      },
      expected_total_transfer_time_upper_bound);
  ASSERT_TRUE(simulator_result)
      << "Expected upper bound: " << expected_total_transfer_time_upper_bound;
}

/* The first 11 packets are sent at the same time, but the duration between the
 * acks of the 1st and the 11th packet is 49 milliseconds, causing very low bw
 * samples. This happens for both large and small buffers.
 */
/*
TEST_F(Bbr2MultiSenderTest, Bbr2VsBbr2LargeRttTinyBuffer) {
  SetupBbr2Sender(sender_endpoints_[1].get());

  MultiSenderTopologyParams params;
  params.switch_queue_capacity_in_bdp = 0.05;
  params.test_link.delay = QuicTime::Delta::FromSeconds(1);
  CreateNetwork(params);

  const QuicByteCount transfer_size = 10 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time;

  // Transfer 10% of data in first transfer.
  sender_endpoints_[0]->AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() >= 0.1 * transfer_size;
      },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  sender_endpoints_[1]->AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() == transfer_size &&
               receiver_endpoints_[1]->bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}
*/

TEST_F(Bbr2MultiSenderTest, Bbr2VsBbr1) {
  SetupBbrSender(sender_endpoints_[1].get());

  MultiSenderTopologyParams params;
  CreateNetwork(params);

  const QuicByteCount transfer_size = 10 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time;

  // Transfer 10% of data in first transfer.
  sender_endpoints_[0]->AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() >= 0.1 * transfer_size;
      },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  sender_endpoints_[1]->AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() == transfer_size &&
               receiver_endpoints_[1]->bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}

TEST_F(Bbr2MultiSenderTest, QUIC_SLOW_TEST(Bbr2VsReno)) {
  SetupTcpSender(sender_endpoints_[1].get(), /*reno=*/true);

  MultiSenderTopologyParams params;
  CreateNetwork(params);

  const QuicByteCount transfer_size = 50 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time;

  // Transfer 10% of data in first transfer.
  sender_endpoints_[0]->AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() >= 0.1 * transfer_size;
      },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  sender_endpoints_[1]->AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() == transfer_size &&
               receiver_endpoints_[1]->bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}

TEST_F(Bbr2MultiSenderTest, QUIC_SLOW_TEST(Bbr2VsCubic)) {
  SetupTcpSender(sender_endpoints_[1].get(), /*reno=*/false);

  MultiSenderTopologyParams params;
  CreateNetwork(params);

  const QuicByteCount transfer_size = 50 * 1024 * 1024;
  const QuicTime::Delta transfer_time =
      params.BottleneckBandwidth().TransferTime(transfer_size);
  QUIC_LOG(INFO) << "Single flow transfer time: " << transfer_time;

  // Transfer 10% of data in first transfer.
  sender_endpoints_[0]->AddBytesToTransfer(transfer_size);
  bool simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() >= 0.1 * transfer_size;
      },
      transfer_time);
  ASSERT_TRUE(simulator_result);

  // Start the second transfer and wait until both finish.
  sender_endpoints_[1]->AddBytesToTransfer(transfer_size);
  simulator_result = simulator_.RunUntilOrTimeout(
      [this]() {
        return receiver_endpoints_[0]->bytes_received() == transfer_size &&
               receiver_endpoints_[1]->bytes_received() == transfer_size;
      },
      3 * transfer_time);
  ASSERT_TRUE(simulator_result);
}

}  // namespace test
}  // namespace quic
