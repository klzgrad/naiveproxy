// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_packet_processor.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_processor_test_tools.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace {

using Direction = QbonePacketProcessor::Direction;
using ProcessingResult = QbonePacketProcessor::ProcessingResult;
using OutputInterface = QbonePacketProcessor::OutputInterface;
using ::testing::_;
using ::testing::Return;

// clang-format off
static const char kReferenceClientPacketData[] = {
    // IPv6 with zero TOS and flow label.
    0x60, 0x00, 0x00, 0x00,
    // Payload size is 8 bytes.
    0x00, 0x08,
    // Next header is UDP
    17,
    // TTL is 50.
    50,
    // IP address of the sender is fd00:0:0:1::1
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // IP address of the receiver is fd00:0:0:5::1
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // Source port 12345
    0x30, 0x39,
    // Destination port 443
    0x01, 0xbb,
    // UDP content length is zero
    0x00, 0x00,
    // Checksum is not actually checked in any of the tests, so we leave it as
    // zero
    0x00, 0x00,
};

static const char kReferenceNetworkPacketData[] = {
    // IPv6 with zero TOS and flow label.
    0x60, 0x00, 0x00, 0x00,
    // Payload size is 8 bytes.
    0x00, 0x08,
    // Next header is UDP
    17,
    // TTL is 50.
    50,
    // IP address of the sender is fd00:0:0:5::1
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // IP address of the receiver is fd00:0:0:1::1
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // Source port 443
    0x01, 0xbb,
    // Destination port 12345
    0x30, 0x39,
    // UDP content length is zero
    0x00, 0x00,
    // Checksum is not actually checked in any of the tests, so we leave it as
    // zero
    0x00, 0x00,
};

static const char kReferenceClientSubnetPacketData[] = {
    // IPv6 with zero TOS and flow label.
    0x60, 0x00, 0x00, 0x00,
    // Payload size is 8 bytes.
    0x00, 0x08,
    // Next header is UDP
    17,
    // TTL is 50.
    50,
    // IP address of the sender is fd00:0:0:2::1, which is within the /62 of the
    // client.
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // IP address of the receiver is fd00:0:0:5::1
    0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // Source port 12345
    0x30, 0x39,
    // Destination port 443
    0x01, 0xbb,
    // UDP content length is zero
    0x00, 0x00,
    // Checksum is not actually checked in any of the tests, so we leave it as
    // zero
    0x00, 0x00,
};

// clang-format on

static const quiche::QuicheStringPiece kReferenceClientPacket(
    kReferenceClientPacketData,
    arraysize(kReferenceClientPacketData));

static const quiche::QuicheStringPiece kReferenceNetworkPacket(
    kReferenceNetworkPacketData,
    arraysize(kReferenceNetworkPacketData));

static const quiche::QuicheStringPiece kReferenceClientSubnetPacket(
    kReferenceClientSubnetPacketData,
    arraysize(kReferenceClientSubnetPacketData));

MATCHER_P(IsIcmpMessage,
          icmp_type,
          "Checks whether the argument is an ICMP message of supplied type") {
  if (arg.size() < kTotalICMPv6HeaderSize) {
    return false;
  }

  return arg[40] == icmp_type;
}

class MockPacketFilter : public QbonePacketProcessor::Filter {
 public:
  MOCK_METHOD5(FilterPacket,
               ProcessingResult(Direction,
                                quiche::QuicheStringPiece,
                                quiche::QuicheStringPiece,
                                icmp6_hdr*,
                                OutputInterface*));
};

class QbonePacketProcessorTest : public QuicTest {
 protected:
  QbonePacketProcessorTest() {
    CHECK(client_ip_.FromString("fd00:0:0:1::1"));
    CHECK(self_ip_.FromString("fd00:0:0:4::1"));
    CHECK(network_ip_.FromString("fd00:0:0:5::1"));

    processor_ = std::make_unique<QbonePacketProcessor>(
        self_ip_, client_ip_, /*client_ip_subnet_length=*/62, &output_,
        &stats_);
  }

  void SendPacketFromClient(quiche::QuicheStringPiece packet) {
    std::string packet_buffer(packet.data(), packet.size());
    processor_->ProcessPacket(&packet_buffer, Direction::FROM_OFF_NETWORK);
  }

  void SendPacketFromNetwork(quiche::QuicheStringPiece packet) {
    std::string packet_buffer(packet.data(), packet.size());
    processor_->ProcessPacket(&packet_buffer, Direction::FROM_NETWORK);
  }

  QuicIpAddress client_ip_;
  QuicIpAddress self_ip_;
  QuicIpAddress network_ip_;

  std::unique_ptr<QbonePacketProcessor> processor_;
  testing::StrictMock<MockPacketProcessorOutput> output_;
  testing::StrictMock<MockPacketProcessorStats> stats_;
};

TEST_F(QbonePacketProcessorTest, EmptyPacket) {
  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_OFF_NETWORK));
  SendPacketFromClient("");

  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_NETWORK));
  SendPacketFromNetwork("");
}

TEST_F(QbonePacketProcessorTest, RandomGarbage) {
  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_OFF_NETWORK));
  SendPacketFromClient(std::string(1280, 'a'));

  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_NETWORK));
  SendPacketFromNetwork(std::string(1280, 'a'));
}

TEST_F(QbonePacketProcessorTest, RandomGarbageWithCorrectLengthFields) {
  std::string packet(40, 'a');
  packet[4] = 0;
  packet[5] = 0;

  EXPECT_CALL(stats_, OnPacketDroppedWithIcmp(Direction::FROM_OFF_NETWORK));
  EXPECT_CALL(output_, SendPacketToClient(IsIcmpMessage(ICMP6_DST_UNREACH)));
  SendPacketFromClient(packet);
}

TEST_F(QbonePacketProcessorTest, GoodPacketFromClient) {
  EXPECT_CALL(stats_, OnPacketForwarded(Direction::FROM_OFF_NETWORK));
  EXPECT_CALL(output_, SendPacketToNetwork(_));
  SendPacketFromClient(kReferenceClientPacket);
}

TEST_F(QbonePacketProcessorTest, GoodPacketFromClientSubnet) {
  EXPECT_CALL(stats_, OnPacketForwarded(Direction::FROM_OFF_NETWORK));
  EXPECT_CALL(output_, SendPacketToNetwork(_));
  SendPacketFromClient(kReferenceClientSubnetPacket);
}

TEST_F(QbonePacketProcessorTest, GoodPacketFromNetwork) {
  EXPECT_CALL(stats_, OnPacketForwarded(Direction::FROM_NETWORK));
  EXPECT_CALL(output_, SendPacketToClient(_));
  SendPacketFromNetwork(kReferenceNetworkPacket);
}

TEST_F(QbonePacketProcessorTest, GoodPacketFromNetworkWrongDirection) {
  EXPECT_CALL(stats_, OnPacketDroppedWithIcmp(Direction::FROM_OFF_NETWORK));
  EXPECT_CALL(output_, SendPacketToClient(IsIcmpMessage(ICMP6_DST_UNREACH)));
  SendPacketFromClient(kReferenceNetworkPacket);
}

TEST_F(QbonePacketProcessorTest, TtlExpired) {
  std::string packet(kReferenceNetworkPacket);
  packet[7] = 1;

  EXPECT_CALL(stats_, OnPacketDroppedWithIcmp(Direction::FROM_NETWORK));
  EXPECT_CALL(output_, SendPacketToNetwork(IsIcmpMessage(ICMP6_TIME_EXCEEDED)));
  SendPacketFromNetwork(packet);
}

TEST_F(QbonePacketProcessorTest, UnknownProtocol) {
  std::string packet(kReferenceNetworkPacket);
  packet[6] = IPPROTO_SCTP;

  EXPECT_CALL(stats_, OnPacketDroppedWithIcmp(Direction::FROM_NETWORK));
  EXPECT_CALL(output_, SendPacketToNetwork(IsIcmpMessage(ICMP6_PARAM_PROB)));
  SendPacketFromNetwork(packet);
}

TEST_F(QbonePacketProcessorTest, FilterFromClient) {
  auto filter = std::make_unique<MockPacketFilter>();
  EXPECT_CALL(*filter, FilterPacket(_, _, _, _, _))
      .WillRepeatedly(Return(ProcessingResult::SILENT_DROP));
  processor_->set_filter(std::move(filter));

  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_OFF_NETWORK));
  SendPacketFromClient(kReferenceClientPacket);
}

class TestFilter : public QbonePacketProcessor::Filter {
 public:
  TestFilter(QuicIpAddress client_ip, QuicIpAddress network_ip)
      : client_ip_(client_ip), network_ip_(network_ip) {}
  ProcessingResult FilterPacket(Direction direction,
                                quiche::QuicheStringPiece full_packet,
                                quiche::QuicheStringPiece payload,
                                icmp6_hdr* icmp_header,
                                OutputInterface* output) override {
    EXPECT_EQ(kIPv6HeaderSize, full_packet.size() - payload.size());
    EXPECT_EQ(IPPROTO_UDP, TransportProtocolFromHeader(full_packet));
    EXPECT_EQ(client_ip_, SourceIpFromHeader(full_packet));
    EXPECT_EQ(network_ip_, DestinationIpFromHeader(full_packet));

    called_++;
    return ProcessingResult::SILENT_DROP;
  }

  int called() const { return called_; }

 private:
  int called_ = 0;

  QuicIpAddress client_ip_;
  QuicIpAddress network_ip_;
};

// Verify that the parameters are passed correctly into the filter, and that the
// helper functions of the filter class work.
TEST_F(QbonePacketProcessorTest, FilterHelperFunctions) {
  auto filter_owned = std::make_unique<TestFilter>(client_ip_, network_ip_);
  TestFilter* filter = filter_owned.get();
  processor_->set_filter(std::move(filter_owned));

  EXPECT_CALL(stats_, OnPacketDroppedSilently(Direction::FROM_OFF_NETWORK));
  SendPacketFromClient(kReferenceClientPacket);
  ASSERT_EQ(1, filter->called());
}

}  // namespace
}  // namespace quic
