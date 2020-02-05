// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quartc/simulated_packet_transport.h"
#include "net/third_party/quiche/src/quic/quartc/test/bidi_test_runner.h"
#include "net/third_party/quiche/src/quic/quartc/test/quartc_competing_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/test/quic_trace_interceptor.h"
#include "net/third_party/quiche/src/quic/quartc/test/random_packet_filter.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

namespace quic {
namespace test {
namespace {

class QuartcBidiTest : public QuicTest {
 protected:
  QuartcBidiTest() {
    uint64_t seed = QuicRandom::GetInstance()->RandUint64();
    QUIC_LOG(INFO) << "Setting random seed to " << seed;
    random_.set_seed(seed);
    simulator_.set_random_generator(&random_);

    client_trace_interceptor_ =
        std::make_unique<QuicTraceInterceptor>("client");
    server_trace_interceptor_ =
        std::make_unique<QuicTraceInterceptor>("server");
  }

  void CreateTransports(QuicBandwidth bandwidth,
                        QuicTime::Delta propagation_delay,
                        QuicByteCount queue_length,
                        int loss_percent) {
    // Endpoints which serve as the transports for client and server.
    client_transport_ =
        std::make_unique<simulator::SimulatedQuartcPacketTransport>(
            &simulator_, "client_transport", "server_transport", queue_length);
    server_transport_ =
        std::make_unique<simulator::SimulatedQuartcPacketTransport>(
            &simulator_, "server_transport", "client_transport", queue_length);

    // Filters on each of the endpoints facilitate random packet loss.
    client_filter_ = std::make_unique<simulator::RandomPacketFilter>(
        &simulator_, "client_filter", client_transport_.get());
    server_filter_ = std::make_unique<simulator::RandomPacketFilter>(
        &simulator_, "server_filter", server_transport_.get());
    client_filter_->set_loss_percent(loss_percent);
    server_filter_->set_loss_percent(loss_percent);

    // Each endpoint connects directly to a switch.
    client_switch_ = std::make_unique<simulator::Switch>(
        &simulator_, "client_switch", /*port_count=*/8, 2 * queue_length);
    server_switch_ = std::make_unique<simulator::Switch>(
        &simulator_, "server_switch", /*port_count=*/8, 2 * queue_length);

    // Links to the switch have significantly higher bandwidth than the
    // bottleneck and insignificant propagation delay.
    client_link_ = std::make_unique<simulator::SymmetricLink>(
        client_filter_.get(), client_switch_->port(1), 10 * bandwidth,
        QuicTime::Delta::FromMicroseconds(1));
    server_link_ = std::make_unique<simulator::SymmetricLink>(
        server_filter_.get(), server_switch_->port(1), 10 * bandwidth,
        QuicTime::Delta::FromMicroseconds(1));

    // The bottleneck link connects the two switches with the bandwidth and
    // propagation delay specified by the test case.
    bottleneck_link_ = std::make_unique<simulator::SymmetricLink>(
        client_switch_->port(2), server_switch_->port(2), bandwidth,
        propagation_delay);
  }

  void SetupCompetingEndpoints(QuicBandwidth bandwidth,
                               QuicTime::Delta send_interval,
                               QuicByteCount bytes_per_interval) {
    competing_client_ = std::make_unique<QuartcCompetingEndpoint>(
        &simulator_, send_interval, bytes_per_interval, "competing_client",
        "competing_server", quic::Perspective::IS_CLIENT,
        quic::test::TestConnectionId(3));
    competing_server_ = std::make_unique<QuartcCompetingEndpoint>(
        &simulator_, send_interval, bytes_per_interval, "competing_server",
        "competing_client", quic::Perspective::IS_SERVER,
        quic::test::TestConnectionId(3));

    competing_client_link_ = std::make_unique<quic::simulator::SymmetricLink>(
        competing_client_->endpoint(), client_switch_->port(3), 10 * bandwidth,
        QuicTime::Delta::FromMicroseconds(1));
    competing_server_link_ = std::make_unique<quic::simulator::SymmetricLink>(
        competing_server_->endpoint(), server_switch_->port(3), 10 * bandwidth,
        QuicTime::Delta::FromMicroseconds(1));
  }

  simulator::Simulator simulator_;
  SimpleRandom random_;

  std::unique_ptr<simulator::SimulatedQuartcPacketTransport> client_transport_;
  std::unique_ptr<simulator::SimulatedQuartcPacketTransport> server_transport_;
  std::unique_ptr<simulator::RandomPacketFilter> client_filter_;
  std::unique_ptr<simulator::RandomPacketFilter> server_filter_;
  std::unique_ptr<simulator::Switch> client_switch_;
  std::unique_ptr<simulator::Switch> server_switch_;
  std::unique_ptr<simulator::SymmetricLink> client_link_;
  std::unique_ptr<simulator::SymmetricLink> server_link_;
  std::unique_ptr<simulator::SymmetricLink> bottleneck_link_;

  std::unique_ptr<QuartcCompetingEndpoint> competing_client_;
  std::unique_ptr<QuartcCompetingEndpoint> competing_server_;
  std::unique_ptr<simulator::SymmetricLink> competing_client_link_;
  std::unique_ptr<simulator::SymmetricLink> competing_server_link_;

  std::unique_ptr<QuicTraceInterceptor> client_trace_interceptor_;
  std::unique_ptr<QuicTraceInterceptor> server_trace_interceptor_;
};

TEST_F(QuartcBidiTest, Basic300kbps200ms) {
  CreateTransports(QuicBandwidth::FromKBitsPerSecond(300),
                   QuicTime::Delta::FromMilliseconds(200),
                   10 * kDefaultMaxPacketSize, /*loss_percent=*/0);
  BidiTestRunner runner(&simulator_, client_transport_.get(),
                        server_transport_.get());
  runner.set_client_interceptor(client_trace_interceptor_.get());
  runner.set_server_interceptor(server_trace_interceptor_.get());
  EXPECT_TRUE(runner.RunTest(QuicTime::Delta::FromSeconds(30)));
}

TEST_F(QuartcBidiTest, 300kbps200ms2PercentLoss) {
  CreateTransports(QuicBandwidth::FromKBitsPerSecond(300),
                   QuicTime::Delta::FromMilliseconds(200),
                   10 * kDefaultMaxPacketSize, /*loss_percent=*/2);
  BidiTestRunner runner(&simulator_, client_transport_.get(),
                        server_transport_.get());
  runner.set_client_interceptor(client_trace_interceptor_.get());
  runner.set_server_interceptor(server_trace_interceptor_.get());
  EXPECT_TRUE(runner.RunTest(QuicTime::Delta::FromSeconds(30)));
}

TEST_F(QuartcBidiTest, 300kbps200ms2PercentLossCompetingBurst) {
  QuicBandwidth bandwidth = QuicBandwidth::FromKBitsPerSecond(300);
  CreateTransports(bandwidth, QuicTime::Delta::FromMilliseconds(200),
                   10 * quic::kDefaultMaxPacketSize, /*loss_percent=*/2);
  SetupCompetingEndpoints(bandwidth, QuicTime::Delta::FromSeconds(15),
                          /*bytes_per_interval=*/50 * 1024);

  quic::test::BidiTestRunner runner(&simulator_, client_transport_.get(),
                                    server_transport_.get());
  runner.set_client_interceptor(client_trace_interceptor_.get());
  runner.set_server_interceptor(server_trace_interceptor_.get());
  EXPECT_TRUE(runner.RunTest(QuicTime::Delta::FromSeconds(30)));
}

TEST_F(QuartcBidiTest, 300kbps200ms2PercentLossSmallCompetingSpikes) {
  QuicBandwidth bandwidth = QuicBandwidth::FromKBitsPerSecond(300);
  CreateTransports(bandwidth, QuicTime::Delta::FromMilliseconds(200),
                   10 * quic::kDefaultMaxPacketSize, /*loss_percent=*/2);

  // Competition sends a small amount of data (10 kb) every 2 seconds.
  SetupCompetingEndpoints(bandwidth, QuicTime::Delta::FromSeconds(2),
                          /*bytes_per_interval=*/10 * 1024);

  quic::test::BidiTestRunner runner(&simulator_, client_transport_.get(),
                                    server_transport_.get());
  runner.set_client_interceptor(client_trace_interceptor_.get());
  runner.set_server_interceptor(server_trace_interceptor_.get());
  EXPECT_TRUE(runner.RunTest(QuicTime::Delta::FromSeconds(30)));
}

TEST_F(QuartcBidiTest, 300kbps200ms2PercentLossAggregation) {
  QuicBandwidth bandwidth = QuicBandwidth::FromKBitsPerSecond(300);
  CreateTransports(bandwidth, QuicTime::Delta::FromMilliseconds(200),
                   10 * quic::kDefaultMaxPacketSize, /*loss_percent=*/2);

  // Set aggregation on the queues at either end of the bottleneck.
  client_switch_->port_queue(2)->EnableAggregation(
      10 * 1024, QuicTime::Delta::FromMilliseconds(100));
  server_switch_->port_queue(2)->EnableAggregation(
      10 * 1024, QuicTime::Delta::FromMilliseconds(100));

  quic::test::BidiTestRunner runner(&simulator_, client_transport_.get(),
                                    server_transport_.get());
  runner.set_client_interceptor(client_trace_interceptor_.get());
  runner.set_server_interceptor(server_trace_interceptor_.get());
  EXPECT_TRUE(runner.RunTest(QuicTime::Delta::FromSeconds(30)));
}

}  // namespace
}  // namespace test
}  // namespace quic
