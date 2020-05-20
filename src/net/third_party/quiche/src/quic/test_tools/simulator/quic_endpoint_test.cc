// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace quic {
namespace simulator {

const QuicBandwidth kDefaultBandwidth =
    QuicBandwidth::FromKBitsPerSecond(10 * 1000);
const QuicTime::Delta kDefaultPropagationDelay =
    QuicTime::Delta::FromMilliseconds(20);
const QuicByteCount kDefaultBdp = kDefaultBandwidth * kDefaultPropagationDelay;

// A simple test harness where all hosts are connected to a switch with
// identical links.
class QuicEndpointTest : public QuicTest {
 public:
  QuicEndpointTest()
      : simulator_(), switch_(&simulator_, "Switch", 8, kDefaultBdp * 2) {}

 protected:
  Simulator simulator_;
  Switch switch_;

  std::unique_ptr<SymmetricLink> Link(Endpoint* a, Endpoint* b) {
    return std::make_unique<SymmetricLink>(a, b, kDefaultBandwidth,
                                           kDefaultPropagationDelay);
  }

  std::unique_ptr<SymmetricLink> CustomLink(Endpoint* a,
                                            Endpoint* b,
                                            uint64_t extra_rtt_ms) {
    return std::make_unique<SymmetricLink>(
        a, b, kDefaultBandwidth,
        kDefaultPropagationDelay +
            QuicTime::Delta::FromMilliseconds(extra_rtt_ms));
  }
};

// Test transmission from one host to another.
TEST_F(QuicEndpointTest, OneWayTransmission) {
  QuicEndpoint endpoint_a(&simulator_, "Endpoint A", "Endpoint B",
                          Perspective::IS_CLIENT, test::TestConnectionId(42));
  QuicEndpoint endpoint_b(&simulator_, "Endpoint B", "Endpoint A",
                          Perspective::IS_SERVER, test::TestConnectionId(42));
  auto link_a = Link(&endpoint_a, switch_.port(1));
  auto link_b = Link(&endpoint_b, switch_.port(2));

  // First transmit a small, packet-size chunk of data.
  endpoint_a.AddBytesToTransfer(600);
  QuicTime end_time =
      simulator_.GetClock()->Now() + QuicTime::Delta::FromMilliseconds(1000);
  simulator_.RunUntil(
      [this, end_time]() { return simulator_.GetClock()->Now() >= end_time; });

  EXPECT_EQ(600u, endpoint_a.bytes_transferred());
  ASSERT_EQ(600u, endpoint_b.bytes_received());
  EXPECT_FALSE(endpoint_a.wrong_data_received());
  EXPECT_FALSE(endpoint_b.wrong_data_received());

  // After a small chunk succeeds, try to transfer 2 MiB.
  endpoint_a.AddBytesToTransfer(2 * 1024 * 1024);
  end_time = simulator_.GetClock()->Now() + QuicTime::Delta::FromSeconds(5);
  simulator_.RunUntil(
      [this, end_time]() { return simulator_.GetClock()->Now() >= end_time; });

  const QuicByteCount total_bytes_transferred = 600 + 2 * 1024 * 1024;
  EXPECT_EQ(total_bytes_transferred, endpoint_a.bytes_transferred());
  EXPECT_EQ(total_bytes_transferred, endpoint_b.bytes_received());
  EXPECT_EQ(0u, endpoint_a.write_blocked_count());
  EXPECT_FALSE(endpoint_a.wrong_data_received());
  EXPECT_FALSE(endpoint_b.wrong_data_received());
}

// Test the situation in which the writer becomes write-blocked.
TEST_F(QuicEndpointTest, WriteBlocked) {
  QuicEndpoint endpoint_a(&simulator_, "Endpoint A", "Endpoint B",
                          Perspective::IS_CLIENT, test::TestConnectionId(42));
  QuicEndpoint endpoint_b(&simulator_, "Endpoint B", "Endpoint A",
                          Perspective::IS_SERVER, test::TestConnectionId(42));
  auto link_a = Link(&endpoint_a, switch_.port(1));
  auto link_b = Link(&endpoint_b, switch_.port(2));

  // Will be owned by the sent packet manager.
  auto* sender = new NiceMock<test::MockSendAlgorithm>();
  EXPECT_CALL(*sender, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*sender, PacingRate(_))
      .WillRepeatedly(Return(10 * kDefaultBandwidth));
  EXPECT_CALL(*sender, BandwidthEstimate())
      .WillRepeatedly(Return(10 * kDefaultBandwidth));
  EXPECT_CALL(*sender, GetCongestionWindow())
      .WillRepeatedly(Return(kMaxOutgoingPacketSize *
                             GetQuicFlag(FLAGS_quic_max_congestion_window)));
  test::QuicConnectionPeer::SetSendAlgorithm(endpoint_a.connection(), sender);

  // First transmit a small, packet-size chunk of data.
  QuicByteCount bytes_to_transfer = 3 * 1024 * 1024;
  endpoint_a.AddBytesToTransfer(bytes_to_transfer);
  QuicTime end_time =
      simulator_.GetClock()->Now() + QuicTime::Delta::FromSeconds(30);
  simulator_.RunUntil([this, &endpoint_b, bytes_to_transfer, end_time]() {
    return endpoint_b.bytes_received() == bytes_to_transfer ||
           simulator_.GetClock()->Now() >= end_time;
  });

  EXPECT_EQ(bytes_to_transfer, endpoint_a.bytes_transferred());
  EXPECT_EQ(bytes_to_transfer, endpoint_b.bytes_received());
  EXPECT_GT(endpoint_a.write_blocked_count(), 0u);
  EXPECT_FALSE(endpoint_a.wrong_data_received());
  EXPECT_FALSE(endpoint_b.wrong_data_received());
}

// Test transmission of 1 MiB of data between two hosts simultaneously in both
// directions.
TEST_F(QuicEndpointTest, TwoWayTransmission) {
  QuicEndpoint endpoint_a(&simulator_, "Endpoint A", "Endpoint B",
                          Perspective::IS_CLIENT, test::TestConnectionId(42));
  QuicEndpoint endpoint_b(&simulator_, "Endpoint B", "Endpoint A",
                          Perspective::IS_SERVER, test::TestConnectionId(42));
  auto link_a = Link(&endpoint_a, switch_.port(1));
  auto link_b = Link(&endpoint_b, switch_.port(2));

  endpoint_a.RecordTrace();
  endpoint_b.RecordTrace();

  endpoint_a.AddBytesToTransfer(1024 * 1024);
  endpoint_b.AddBytesToTransfer(1024 * 1024);
  QuicTime end_time =
      simulator_.GetClock()->Now() + QuicTime::Delta::FromSeconds(5);
  simulator_.RunUntil(
      [this, end_time]() { return simulator_.GetClock()->Now() >= end_time; });

  EXPECT_EQ(1024u * 1024u, endpoint_a.bytes_transferred());
  EXPECT_EQ(1024u * 1024u, endpoint_b.bytes_transferred());
  EXPECT_EQ(1024u * 1024u, endpoint_a.bytes_received());
  EXPECT_EQ(1024u * 1024u, endpoint_b.bytes_received());
  EXPECT_FALSE(endpoint_a.wrong_data_received());
  EXPECT_FALSE(endpoint_b.wrong_data_received());
}

// Simulate three hosts trying to send data to a fourth one simultaneously.
TEST_F(QuicEndpointTest, Competition) {
  // TODO(63765788): Turn back on this flag when the issue if fixed.
  SetQuicReloadableFlag(quic_bbr_one_mss_conservation, false);
  auto endpoint_a = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint A", "Endpoint D (A)", Perspective::IS_CLIENT,
      test::TestConnectionId(42));
  auto endpoint_b = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint B", "Endpoint D (B)", Perspective::IS_CLIENT,
      test::TestConnectionId(43));
  auto endpoint_c = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint C", "Endpoint D (C)", Perspective::IS_CLIENT,
      test::TestConnectionId(44));
  auto endpoint_d_a = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint D (A)", "Endpoint A", Perspective::IS_SERVER,
      test::TestConnectionId(42));
  auto endpoint_d_b = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint D (B)", "Endpoint B", Perspective::IS_SERVER,
      test::TestConnectionId(43));
  auto endpoint_d_c = std::make_unique<QuicEndpoint>(
      &simulator_, "Endpoint D (C)", "Endpoint C", Perspective::IS_SERVER,
      test::TestConnectionId(44));
  QuicEndpointMultiplexer endpoint_d(
      "Endpoint D",
      {endpoint_d_a.get(), endpoint_d_b.get(), endpoint_d_c.get()});

  // Create links with slightly different RTTs in order to avoid pathological
  // side-effects of packets entering the queue at the exactly same time.
  auto link_a = CustomLink(endpoint_a.get(), switch_.port(1), 0);
  auto link_b = CustomLink(endpoint_b.get(), switch_.port(2), 1);
  auto link_c = CustomLink(endpoint_c.get(), switch_.port(3), 2);
  auto link_d = Link(&endpoint_d, switch_.port(4));

  endpoint_a->AddBytesToTransfer(2 * 1024 * 1024);
  endpoint_b->AddBytesToTransfer(2 * 1024 * 1024);
  endpoint_c->AddBytesToTransfer(2 * 1024 * 1024);
  QuicTime end_time =
      simulator_.GetClock()->Now() + QuicTime::Delta::FromSeconds(12);
  simulator_.RunUntil(
      [this, end_time]() { return simulator_.GetClock()->Now() >= end_time; });

  for (QuicEndpoint* endpoint :
       {endpoint_a.get(), endpoint_b.get(), endpoint_c.get()}) {
    EXPECT_EQ(2u * 1024u * 1024u, endpoint->bytes_transferred());
    EXPECT_GE(endpoint->connection()->GetStats().packets_lost, 0u);
  }
  for (QuicEndpoint* endpoint :
       {endpoint_d_a.get(), endpoint_d_b.get(), endpoint_d_c.get()}) {
    EXPECT_EQ(2u * 1024u * 1024u, endpoint->bytes_received());
    EXPECT_FALSE(endpoint->wrong_data_received());
  }
}

}  // namespace simulator
}  // namespace quic
