// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/tcp_cubic_sender_bytes.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

namespace quic {

using CyclePhase = Bbr2ProbeBwMode::CyclePhase;

namespace test {

// Use the initial CWND of 10, as 32 is too much for the test network.
const uint32_t kDefaultInitialCwndPackets = 10;
const uint32_t kDefaultInitialCwndBytes =
    kDefaultInitialCwndPackets * kDefaultTCPMSS;

struct LinkParams {
 public:
  LinkParams(int64_t kilo_bits_per_sec, int64_t delay_us)
      : bandwidth(QuicBandwidth::FromKBitsPerSecond(kilo_bits_per_sec)),
        delay(QuicTime::Delta::FromMicroseconds(delay_us)) {}
  QuicBandwidth bandwidth;
  QuicTime::Delta delay;
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
  Bbr2SimulatorTest() {
    // Disable Ack Decimation by default, because it can significantly increase
    // srtt. Individual test can enable it via QuicConnectionPeer::SetAckMode().
    SetQuicReloadableFlag(quic_enable_ack_decimation, false);
  }
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
    sender_ = SetupBbr2Sender(&sender_endpoint_);

    uint64_t seed = QuicRandom::GetInstance()->RandUint64();
    random_.set_seed(seed);
    QUIC_LOG(INFO) << "Bbr2DefaultTopologyTest set up.  Seed: " << seed;

    simulator_.set_random_generator(&random_);
  }

  ~Bbr2DefaultTopologyTest() {
    const auto* test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    QUIC_LOG(INFO) << "Bbr2DefaultTopologyTest." << test_info->name()
                   << " completed at simulated time: "
                   << SimulatedNow().ToDebuggingValue() / 1e6 << " sec.";
  }

  Bbr2Sender* SetupBbr2Sender(simulator::QuicEndpoint* endpoint) {
    // Ownership of the sender will be overtaken by the endpoint.
    Bbr2Sender* sender = new Bbr2Sender(
        endpoint->connection()->clock()->Now(),
        endpoint->connection()->sent_packet_manager().GetRttStats(),
        QuicSentPacketManagerPeer::GetUnackedPacketMap(
            QuicConnectionPeer::GetSentPacketManager(endpoint->connection())),
        kDefaultInitialCwndPackets, kDefaultMaxCongestionWindowPackets,
        &random_, QuicConnectionPeer::GetStats(endpoint->connection()));
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  void CreateNetwork(const DefaultTopologyParams& params) {
    QUIC_LOG(INFO) << "CreateNetwork with parameters: " << params.ToString();
    switch_ = QuicMakeUnique<simulator::Switch>(&simulator_, "Switch",
                                                params.switch_port_count,
                                                params.SwitchQueueCapacity());

    // WARNING: The order to add links to network_links_ matters, because some
    // tests adjusts the link bandwidth on the fly.

    // Local link connects sender and port 1.
    network_links_.push_back(QuicMakeUnique<simulator::SymmetricLink>(
        &sender_endpoint_, switch_->port(1), params.local_link.bandwidth,
        params.local_link.delay));

    // Test link connects receiver and port 2.
    network_links_.push_back(QuicMakeUnique<simulator::SymmetricLink>(
        &receiver_endpoint_, switch_->port(2), params.test_link.bandwidth,
        params.test_link.delay));
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

  bool Bbr2ModeIsOneOf(const std::vector<Bbr2Mode>& expected_modes) const {
    const Bbr2Mode mode = sender_->ExportDebugState().mode;
    for (Bbr2Mode expected_mode : expected_modes) {
      if (mode == expected_mode) {
        return true;
      }
    }
    return false;
  }

  QuicTime SimulatedNow() const { return simulator_.GetClock()->Now(); }

  const RttStats* rtt_stats() {
    return sender_endpoint_.connection()->sent_packet_manager().GetRttStats();
  }

  QuicConnection* sender_connection() { return sender_endpoint_.connection(); }

  const QuicConnectionStats& sender_connection_stats() {
    return sender_connection()->GetStats();
  }

  float sender_loss_rate_in_packets() {
    return static_cast<float>(sender_connection_stats().packets_lost) /
           sender_connection_stats().packets_sent;
  }

  simulator::Simulator simulator_;
  simulator::QuicEndpoint sender_endpoint_;
  simulator::QuicEndpoint receiver_endpoint_;
  Bbr2Sender* sender_;
  SimpleRandom random_;

  std::unique_ptr<simulator::Switch> switch_;
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
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->smoothed_rtt(), 0.2f);
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
  DefaultTopologyParams params;
  CreateNetwork(params);
  // 2 RTTs of aggregation, with a max of 10kb.
  EnableAggregation(10 * 1024, 2 * params.RTT());

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));

  EXPECT_LE(params.BottleneckBandwidth() * 0.99f,
            sender_->ExportDebugState().bandwidth_hi);
  // TODO(b/36022633): Bandwidth sampler overestimates with aggregation.
  EXPECT_GE(params.BottleneckBandwidth() * 1.5f,
            sender_->ExportDebugState().bandwidth_hi);
  EXPECT_LE(sender_loss_rate_in_packets(), 0.05);
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(params.RTT() * 4, rtt_stats()->smoothed_rtt());
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->min_rtt(), 0.2f);
}

TEST_F(Bbr2DefaultTopologyTest, SimpleTransferAckDecimation) {
  // Enable Ack Decimation on the receiver.
  QuicConnectionPeer::SetAckMode(receiver_endpoint_.connection(),
                                 AckMode::ACK_DECIMATION);
  DefaultTopologyParams params;
  CreateNetwork(params);

  // Transfer 12MB.
  DoSimpleTransfer(12 * 1024 * 1024, QuicTime::Delta::FromSeconds(35));
  EXPECT_TRUE(Bbr2ModeIsOneOf({Bbr2Mode::PROBE_BW, Bbr2Mode::PROBE_RTT}));

  EXPECT_LE(params.BottleneckBandwidth() * 0.99f,
            sender_->ExportDebugState().bandwidth_hi);
  // TODO(b/36022633): Bandwidth sampler overestimates with aggregation.
  EXPECT_GE(params.BottleneckBandwidth() * 1.1f,
            sender_->ExportDebugState().bandwidth_hi);
  EXPECT_LE(sender_loss_rate_in_packets(), 0.001);
  EXPECT_FALSE(sender_->ExportDebugState().last_sample_is_app_limited);
  // The margin here is high, because the aggregation greatly increases
  // smoothed rtt.
  EXPECT_GE(params.RTT() * 2, rtt_stats()->smoothed_rtt());
  EXPECT_APPROX_EQ(params.RTT(), rtt_stats()->min_rtt(), 0.1f);
}

// Test Bbr2's reaction to a 100x bandwidth decrease during a transfer.
TEST_F(Bbr2DefaultTopologyTest, BandwidthDecrease) {
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
TEST_F(Bbr2DefaultTopologyTest, BandwidthIncrease) {
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

// All Bbr2MultiSenderTests uses the following network topology:
//
//   Sender 0  (A Bbr2Sender)
//       |
//       | <-- local_links[0]
//       |
//       |  Sender N (1 <= N < kNumLocalLinks) (May or may not be a Bbr2Sender)
//       |      |
//       |      | <-- local_links[N]
//       |      |
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
    std::vector<simulator::QuicEndpoint*> receiver_endpoint_pointers;
    for (size_t i = 0; i < MultiSenderTopologyParams::kNumLocalLinks; ++i) {
      std::string sender_name = QuicStrCat("Sender", i + 1);
      std::string receiver_name = QuicStrCat("Receiver", i + 1);
      sender_endpoints_.push_back(QuicMakeUnique<simulator::QuicEndpoint>(
          &simulator_, sender_name, receiver_name, Perspective::IS_CLIENT,
          TestConnectionId(first_connection_id + i)));
      receiver_endpoints_.push_back(QuicMakeUnique<simulator::QuicEndpoint>(
          &simulator_, receiver_name, sender_name, Perspective::IS_SERVER,
          TestConnectionId(first_connection_id + i)));
      receiver_endpoint_pointers.push_back(receiver_endpoints_.back().get());
    }
    receiver_multiplexer_ = QuicMakeUnique<simulator::QuicEndpointMultiplexer>(
        "Receiver multiplexer", receiver_endpoint_pointers);
    sender_1_ = SetupBbr2Sender(sender_endpoints_[0].get());

    uint64_t seed = QuicRandom::GetInstance()->RandUint64();
    random_.set_seed(seed);
    QUIC_LOG(INFO) << "Bbr2MultiSenderTest set up.  Seed: " << seed;

    simulator_.set_random_generator(&random_);
  }

  ~Bbr2MultiSenderTest() {
    const auto* test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    QUIC_LOG(INFO) << "Bbr2MultiSenderTest." << test_info->name()
                   << " completed at simulated time: "
                   << SimulatedNow().ToDebuggingValue() / 1e6 << " sec.";
  }

  Bbr2Sender* SetupBbr2Sender(simulator::QuicEndpoint* endpoint) {
    // Ownership of the sender will be overtaken by the endpoint.
    Bbr2Sender* sender = new Bbr2Sender(
        endpoint->connection()->clock()->Now(),
        endpoint->connection()->sent_packet_manager().GetRttStats(),
        QuicSentPacketManagerPeer::GetUnackedPacketMap(
            QuicConnectionPeer::GetSentPacketManager(endpoint->connection())),
        kDefaultInitialCwndPackets, kDefaultMaxCongestionWindowPackets,
        &random_, QuicConnectionPeer::GetStats(endpoint->connection()));
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
        kDefaultInitialCwndPackets, kDefaultMaxCongestionWindowPackets,
        &random_, QuicConnectionPeer::GetStats(endpoint->connection()));
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
        kDefaultInitialCwndPackets, kDefaultMaxCongestionWindowPackets,
        QuicConnectionPeer::GetStats(endpoint->connection()));
    QuicConnectionPeer::SetSendAlgorithm(endpoint->connection(), sender);
    endpoint->RecordTrace();
    return sender;
  }

  void CreateNetwork(const MultiSenderTopologyParams& params) {
    QUIC_LOG(INFO) << "CreateNetwork with parameters: " << params.ToString();
    switch_ = QuicMakeUnique<simulator::Switch>(&simulator_, "Switch",
                                                params.switch_port_count,
                                                params.SwitchQueueCapacity());

    network_links_.push_back(QuicMakeUnique<simulator::SymmetricLink>(
        receiver_multiplexer_.get(), switch_->port(1),
        params.test_link.bandwidth, params.test_link.delay));
    for (size_t i = 0; i < MultiSenderTopologyParams::kNumLocalLinks; ++i) {
      simulator::SwitchPortNumber port_number = i + 2;
      network_links_.push_back(QuicMakeUnique<simulator::SymmetricLink>(
          sender_endpoints_[i].get(), switch_->port(port_number),
          params.local_links[i].bandwidth, params.local_links[i].delay));
    }
  }

  QuicTime SimulatedNow() const { return simulator_.GetClock()->Now(); }

  simulator::Simulator simulator_;
  std::vector<std::unique_ptr<simulator::QuicEndpoint>> sender_endpoints_;
  std::vector<std::unique_ptr<simulator::QuicEndpoint>> receiver_endpoints_;
  std::unique_ptr<simulator::QuicEndpointMultiplexer> receiver_multiplexer_;
  Bbr2Sender* sender_1_;
  SimpleRandom random_;

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

TEST_F(Bbr2MultiSenderTest, MultipleBbr2s) {
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

TEST_F(Bbr2MultiSenderTest, Bbr2VsReno) {
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

TEST_F(Bbr2MultiSenderTest, Bbr2VsCubic) {
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
