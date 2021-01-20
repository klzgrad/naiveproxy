// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_trace_visitor.h"

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

namespace quic {
namespace {

const QuicByteCount kTransferSize = 1000 * kMaxOutgoingPacketSize;
const QuicByteCount kTestStreamNumber = 3;
const QuicTime::Delta kDelay = QuicTime::Delta::FromMilliseconds(20);

// The trace for this test is generated using a simulator transfer.
class QuicTraceVisitorTest : public QuicTest {
 public:
  QuicTraceVisitorTest() {
    QuicConnectionId connection_id = test::TestConnectionId();
    simulator::Simulator simulator;
    simulator::QuicEndpoint client(&simulator, "Client", "Server",
                                   Perspective::IS_CLIENT, connection_id);
    simulator::QuicEndpoint server(&simulator, "Server", "Client",
                                   Perspective::IS_SERVER, connection_id);

    const QuicBandwidth kBandwidth = QuicBandwidth::FromKBitsPerSecond(1000);
    const QuicByteCount kBdp = kBandwidth * (2 * kDelay);

    // Create parameters such that some loss is observed.
    simulator::Switch network_switch(&simulator, "Switch", 8, 0.5 * kBdp);
    simulator::SymmetricLink client_link(&client, network_switch.port(1),
                                         2 * kBandwidth, kDelay);
    simulator::SymmetricLink server_link(&server, network_switch.port(2),
                                         kBandwidth, kDelay);

    QuicTraceVisitor visitor(client.connection());
    client.connection()->set_debug_visitor(&visitor);

    // Transfer about a megabyte worth of data from client to server.
    const QuicTime::Delta kDeadline =
        3 * kBandwidth.TransferTime(kTransferSize);
    client.AddBytesToTransfer(kTransferSize);
    bool simulator_result = simulator.RunUntilOrTimeout(
        [&]() { return server.bytes_received() >= kTransferSize; }, kDeadline);
    CHECK(simulator_result);

    // Save the trace and ensure some loss was observed.
    trace_.Swap(visitor.trace());
    CHECK_NE(0u, client.connection()->GetStats().packets_retransmitted);
    packets_sent_ = client.connection()->GetStats().packets_sent;
  }

  std::vector<quic_trace::Event> AllEventsWithType(
      quic_trace::EventType event_type) {
    std::vector<quic_trace::Event> result;
    for (const auto& event : trace_.events()) {
      if (event.event_type() == event_type) {
        result.push_back(event);
      }
    }
    return result;
  }

 protected:
  quic_trace::Trace trace_;
  QuicPacketCount packets_sent_;
};

TEST_F(QuicTraceVisitorTest, ConnectionId) {
  char expected_cid[] = {0, 0, 0, 0, 0, 0, 0, 42};
  EXPECT_EQ(std::string(expected_cid, sizeof(expected_cid)),
            trace_.destination_connection_id());
}

TEST_F(QuicTraceVisitorTest, Version) {
  std::string version = trace_.protocol_version();
  ASSERT_EQ(4u, version.size());
  EXPECT_NE(0, version[0]);
}

// Check that basic metadata about sent packets is recorded.
TEST_F(QuicTraceVisitorTest, SentPacket) {
  auto sent_packets = AllEventsWithType(quic_trace::PACKET_SENT);
  EXPECT_EQ(packets_sent_, sent_packets.size());
  ASSERT_GT(sent_packets.size(), 0u);

  EXPECT_EQ(sent_packets[0].packet_size(), kDefaultMaxPacketSize);
  EXPECT_EQ(sent_packets[0].packet_number(), 1u);
}

// Ensure that every stream frame that was sent is recorded.
TEST_F(QuicTraceVisitorTest, SentStream) {
  auto sent_packets = AllEventsWithType(quic_trace::PACKET_SENT);

  QuicIntervalSet<QuicStreamOffset> offsets;
  for (const quic_trace::Event& packet : sent_packets) {
    for (const quic_trace::Frame& frame : packet.frames()) {
      if (frame.frame_type() != quic_trace::STREAM) {
        continue;
      }

      const quic_trace::StreamFrameInfo& info = frame.stream_frame_info();
      if (info.stream_id() != kTestStreamNumber) {
        continue;
      }

      ASSERT_GT(info.length(), 0u);
      offsets.Add(info.offset(), info.offset() + info.length());
    }
  }

  ASSERT_EQ(1u, offsets.Size());
  EXPECT_EQ(0u, offsets.begin()->min());
  EXPECT_EQ(kTransferSize, offsets.rbegin()->max());
}

// Ensure that all packets are either acknowledged or lost.
TEST_F(QuicTraceVisitorTest, AckPackets) {
  QuicIntervalSet<QuicPacketNumber> packets;
  for (const quic_trace::Event& packet : trace_.events()) {
    if (packet.event_type() == quic_trace::PACKET_RECEIVED) {
      for (const quic_trace::Frame& frame : packet.frames()) {
        if (frame.frame_type() != quic_trace::ACK) {
          continue;
        }

        const quic_trace::AckInfo& info = frame.ack_info();
        for (const auto& block : info.acked_packets()) {
          packets.Add(QuicPacketNumber(block.first_packet()),
                      QuicPacketNumber(block.last_packet()) + 1);
        }
      }
    }
    if (packet.event_type() == quic_trace::PACKET_LOST) {
      packets.Add(QuicPacketNumber(packet.packet_number()),
                  QuicPacketNumber(packet.packet_number()) + 1);
    }
  }

  ASSERT_EQ(1u, packets.Size());
  EXPECT_EQ(QuicPacketNumber(1u), packets.begin()->min());
  // We leave some room (20 packets) for the packets which did not receive
  // conclusive status at the end of simulation.
  EXPECT_GT(packets.rbegin()->max(), QuicPacketNumber(packets_sent_ - 20));
}

TEST_F(QuicTraceVisitorTest, TransportState) {
  auto acks = AllEventsWithType(quic_trace::PACKET_RECEIVED);
  ASSERT_EQ(1, acks[0].frames_size());
  ASSERT_EQ(quic_trace::ACK, acks[0].frames(0).frame_type());

  // Check that min-RTT at the end is a reasonable approximation.
  EXPECT_LE((4 * kDelay).ToMicroseconds() * 1.,
            acks.rbegin()->transport_state().min_rtt_us());
  EXPECT_GE((4 * kDelay).ToMicroseconds() * 1.25,
            acks.rbegin()->transport_state().min_rtt_us());
}

TEST_F(QuicTraceVisitorTest, EncryptionLevels) {
  for (const auto& event : trace_.events()) {
    switch (event.event_type()) {
      case quic_trace::PACKET_SENT:
      case quic_trace::PACKET_RECEIVED:
      case quic_trace::PACKET_LOST:
        ASSERT_TRUE(event.has_encryption_level());
        ASSERT_NE(event.encryption_level(), quic_trace::ENCRYPTION_UNKNOWN);
        break;

      default:
        break;
    }
  }
}

}  // namespace
}  // namespace quic
