// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/simulated_packet_transport.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/switch.h"

namespace quic {
namespace simulator {
namespace {

using ::testing::ElementsAre;

const QuicBandwidth kDefaultBandwidth =
    QuicBandwidth::FromKBitsPerSecond(10 * 1000);
const QuicTime::Delta kDefaultPropagationDelay =
    QuicTime::Delta::FromMilliseconds(20);
const QuicByteCount kDefaultBdp = kDefaultBandwidth * kDefaultPropagationDelay;
const QuicByteCount kDefaultPacketSize = 1200;
const QuicPacketCount kDefaultQueueLength = 10;

class FakeDelegate : public QuartcPacketTransport::Delegate {
 public:
  explicit FakeDelegate(QuartcPacketTransport* transport)
      : transport_(transport) {
    transport_->SetDelegate(this);
  }

  ~FakeDelegate() { transport_->SetDelegate(nullptr); }

  void OnTransportCanWrite() override {
    while (!packets_to_send_.empty()) {
      const std::string& packet = packets_to_send_.front();
      if (transport_->Write(packet.data(), packet.size(),
                            QuartcPacketTransport::PacketInfo()) <
          static_cast<int>(packet.size())) {
        ++write_blocked_count_;
        return;
      }
      packets_to_send_.pop();
    }
  }

  void OnTransportReceived(const char* data, size_t data_len) override {
    packets_received_.emplace_back(data, data_len);
  }

  void AddPacketToSend(const std::string& packet) {
    packets_to_send_.push(packet);
  }

  size_t packets_to_send() { return packets_to_send_.size(); }
  const std::vector<std::string>& packets_received() {
    return packets_received_;
  }
  int write_blocked_count() { return write_blocked_count_; }

 private:
  QuartcPacketTransport* const transport_ = nullptr;
  std::queue<std::string> packets_to_send_;
  std::vector<std::string> packets_received_;
  int write_blocked_count_ = 0;
};

class SimulatedPacketTransportTest : public QuicTest {
 protected:
  SimulatedPacketTransportTest()
      : switch_(&simulator_, "Switch", /*port_count=*/8, 2 * kDefaultBdp),
        client_(&simulator_,
                "sender",
                "receiver",
                kDefaultQueueLength * kDefaultPacketSize),
        server_(&simulator_,
                "receiver",
                "sender",
                kDefaultQueueLength * kDefaultPacketSize),
        client_link_(&client_,
                     switch_.port(1),
                     kDefaultBandwidth,
                     kDefaultPropagationDelay),
        server_link_(&server_,
                     switch_.port(2),
                     kDefaultBandwidth,
                     kDefaultPropagationDelay),
        client_delegate_(&client_),
        server_delegate_(&server_) {}

  Simulator simulator_;
  Switch switch_;

  SimulatedQuartcPacketTransport client_;
  SimulatedQuartcPacketTransport server_;

  SymmetricLink client_link_;
  SymmetricLink server_link_;

  FakeDelegate client_delegate_;
  FakeDelegate server_delegate_;
};

TEST_F(SimulatedPacketTransportTest, OneWayTransmission) {
  std::string packet_1(kDefaultPacketSize, 'a');
  std::string packet_2(kDefaultPacketSize, 'b');
  client_delegate_.AddPacketToSend(packet_1);
  client_delegate_.AddPacketToSend(packet_2);

  simulator_.RunUntil(
      [this] { return client_delegate_.packets_to_send() == 0; });
  simulator_.RunFor(3 * kDefaultPropagationDelay);

  EXPECT_THAT(server_delegate_.packets_received(),
              ElementsAre(packet_1, packet_2));
  EXPECT_THAT(client_delegate_.packets_received(), ElementsAre());
}

TEST_F(SimulatedPacketTransportTest, TwoWayTransmission) {
  std::string packet_1(kDefaultPacketSize, 'a');
  std::string packet_2(kDefaultPacketSize, 'b');
  std::string packet_3(kDefaultPacketSize, 'c');
  std::string packet_4(kDefaultPacketSize, 'd');

  client_delegate_.AddPacketToSend(packet_1);
  client_delegate_.AddPacketToSend(packet_2);
  server_delegate_.AddPacketToSend(packet_3);
  server_delegate_.AddPacketToSend(packet_4);

  simulator_.RunUntil(
      [this] { return client_delegate_.packets_to_send() == 0; });
  simulator_.RunUntil(
      [this] { return server_delegate_.packets_to_send() == 0; });
  simulator_.RunFor(3 * kDefaultPropagationDelay);

  EXPECT_THAT(server_delegate_.packets_received(),
              ElementsAre(packet_1, packet_2));
  EXPECT_THAT(client_delegate_.packets_received(),
              ElementsAre(packet_3, packet_4));
}

TEST_F(SimulatedPacketTransportTest, TestWriteBlocked) {
  // Add 10 packets beyond what fits in the egress queue.
  std::vector<std::string> packets;
  for (unsigned int i = 0; i < kDefaultQueueLength + 10; ++i) {
    packets.push_back(std::string(kDefaultPacketSize, 'a' + i));
    client_delegate_.AddPacketToSend(packets.back());
  }

  simulator_.RunUntil(
      [this] { return client_delegate_.packets_to_send() == 0; });
  simulator_.RunFor(3 * kDefaultPropagationDelay);

  // Each of the 10 packets in excess of the sender's egress queue length will
  // block the first time |client_delegate_| tries to write them.
  EXPECT_EQ(client_delegate_.write_blocked_count(), 10);
  EXPECT_EQ(server_delegate_.packets_received(), packets);
}

}  // namespace
}  // namespace simulator
}  // namespace quic
