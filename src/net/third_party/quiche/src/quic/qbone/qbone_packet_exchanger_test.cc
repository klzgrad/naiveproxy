// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_packet_exchanger.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/mock_qbone_client.h"

namespace quic {
namespace {

using ::testing::StrEq;
using ::testing::StrictMock;

const size_t kMaxPendingPackets = 2;

class MockVisitor : public QbonePacketExchanger::Visitor {
 public:
  MOCK_METHOD1(OnReadError, void(const std::string&));
  MOCK_METHOD1(OnWriteError, void(const std::string&));
};

class FakeQbonePacketExchanger : public QbonePacketExchanger {
 public:
  using QbonePacketExchanger::QbonePacketExchanger;

  // Adds a packet to the end of list of packets to be returned by ReadPacket.
  // When the list is empty, ReadPacket returns nullptr to signify error as
  // defined by QbonePacketExchanger. If SetReadError is not called or called
  // with empty error string, ReadPacket sets blocked to true.
  void AddPacketToBeRead(std::unique_ptr<QuicData> packet) {
    packets_to_be_read_.push_back(std::move(packet));
  }

  // Sets the error to be returned by ReadPacket when the list of packets is
  // empty. If error is empty string, blocked is set by ReadPacket.
  void SetReadError(const std::string& error) { read_error_ = error; }

  // Force WritePacket to fail with the given status. WritePacket returns true
  // when blocked == true and error is empty.
  void ForceWriteFailure(bool blocked, const std::string& error) {
    write_blocked_ = blocked;
    write_error_ = error;
  }

  // Packets that have been successfully written by WritePacket.
  const std::vector<std::string>& packets_written() const {
    return packets_written_;
  }

 private:
  // Implements QbonePacketExchanger::ReadPacket.
  std::unique_ptr<QuicData> ReadPacket(bool* blocked,
                                       std::string* error) override {
    *blocked = false;

    if (packets_to_be_read_.empty()) {
      *blocked = read_error_.empty();
      *error = read_error_;
      return nullptr;
    }

    std::unique_ptr<QuicData> packet = std::move(packets_to_be_read_.front());
    packets_to_be_read_.pop_front();
    return packet;
  }

  // Implements QbonePacketExchanger::WritePacket.
  bool WritePacket(const char* packet,
                   size_t size,
                   bool* blocked,
                   std::string* error) override {
    *blocked = false;

    if (write_blocked_ || !write_error_.empty()) {
      *blocked = write_blocked_;
      *error = write_error_;
      return false;
    }

    packets_written_.push_back(std::string(packet, size));
    return true;
  }

  std::string read_error_;
  std::list<std::unique_ptr<QuicData>> packets_to_be_read_;

  std::string write_error_;
  bool write_blocked_ = false;
  std::vector<std::string> packets_written_;
};

TEST(QbonePacketExchangerTest,
     ReadAndDeliverPacketDeliversPacketToQboneClient) {
  StrictMock<MockVisitor> visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  StrictMock<MockQboneClient> client;

  std::string packet = "data";
  exchanger.AddPacketToBeRead(
      std::make_unique<QuicData>(packet.data(), packet.length()));
  EXPECT_CALL(client, ProcessPacketFromNetwork(StrEq("data")));

  EXPECT_TRUE(exchanger.ReadAndDeliverPacket(&client));
}

TEST(QbonePacketExchangerTest,
     ReadAndDeliverPacketNotifiesVisitorOnReadFailure) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;

  // Force read error.
  std::string io_error = "I/O error";
  exchanger.SetReadError(io_error);
  EXPECT_CALL(visitor, OnReadError(StrEq(io_error))).Times(1);

  EXPECT_FALSE(exchanger.ReadAndDeliverPacket(&client));
}

TEST(QbonePacketExchangerTest,
     ReadAndDeliverPacketDoesNotNotifyVisitorOnBlockedIO) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;

  // No more packets to read.
  EXPECT_FALSE(exchanger.ReadAndDeliverPacket(&client));
}

TEST(QbonePacketExchangerTest,
     WritePacketToNetworkWritesDirectlyToNetworkWhenNotBlocked) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;

  std::string packet = "data";
  exchanger.WritePacketToNetwork(packet.data(), packet.length());

  ASSERT_EQ(exchanger.packets_written().size(), 1);
  EXPECT_THAT(exchanger.packets_written()[0], StrEq(packet));
}

TEST(QbonePacketExchangerTest,
     WritePacketToNetworkQueuesPacketsAndProcessThemLater) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;

  // Force write to be blocked so that packets are queued.
  exchanger.ForceWriteFailure(true, "");
  std::vector<std::string> packets = {"packet0", "packet1"};
  for (int i = 0; i < packets.size(); i++) {
    exchanger.WritePacketToNetwork(packets[i].data(), packets[i].length());
  }

  // Nothing should have been written because of blockage.
  ASSERT_TRUE(exchanger.packets_written().empty());

  // Remove blockage and start proccessing queued packets.
  exchanger.ForceWriteFailure(false, "");
  exchanger.SetWritable();

  // Queued packets are processed.
  ASSERT_EQ(exchanger.packets_written().size(), 2);
  for (int i = 0; i < packets.size(); i++) {
    EXPECT_THAT(exchanger.packets_written()[i], StrEq(packets[i]));
  }
}

TEST(QbonePacketExchangerTest,
     SetWritableContinuesProcessingPacketIfPreviousCallBlocked) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;

  // Force write to be blocked so that packets are queued.
  exchanger.ForceWriteFailure(true, "");
  std::vector<std::string> packets = {"packet0", "packet1"};
  for (int i = 0; i < packets.size(); i++) {
    exchanger.WritePacketToNetwork(packets[i].data(), packets[i].length());
  }

  // Nothing should have been written because of blockage.
  ASSERT_TRUE(exchanger.packets_written().empty());

  // Start processing packets, but since writes are still blocked, nothing
  // should have been written.
  exchanger.SetWritable();
  ASSERT_TRUE(exchanger.packets_written().empty());

  // Remove blockage and start processing packets again.
  exchanger.ForceWriteFailure(false, "");
  exchanger.SetWritable();

  ASSERT_EQ(exchanger.packets_written().size(), 2);
  for (int i = 0; i < packets.size(); i++) {
    EXPECT_THAT(exchanger.packets_written()[i], StrEq(packets[i]));
  }
}

TEST(QbonePacketExchangerTest, WritePacketToNetworkDropsPacketIfQueueIfFull) {
  std::vector<std::string> packets = {"packet0", "packet1", "packet2"};
  size_t queue_size = packets.size() - 1;
  MockVisitor visitor;
  // exchanger has smaller queue than number of packets.
  FakeQbonePacketExchanger exchanger(&visitor, queue_size);
  MockQboneClient client;

  exchanger.ForceWriteFailure(true, "");
  for (int i = 0; i < packets.size(); i++) {
    exchanger.WritePacketToNetwork(packets[i].data(), packets[i].length());
  }

  // Blocked writes cause packets to be queued or dropped.
  ASSERT_TRUE(exchanger.packets_written().empty());

  exchanger.ForceWriteFailure(false, "");
  exchanger.SetWritable();

  ASSERT_EQ(exchanger.packets_written().size(), queue_size);
  for (int i = 0; i < queue_size; i++) {
    EXPECT_THAT(exchanger.packets_written()[i], StrEq(packets[i]));
  }
}

TEST(QbonePacketExchangerTest, WriteErrorsGetNotified) {
  MockVisitor visitor;
  FakeQbonePacketExchanger exchanger(&visitor, kMaxPendingPackets);
  MockQboneClient client;
  std::string packet = "data";

  // Write error is delivered to visitor during WritePacketToNetwork.
  std::string io_error = "I/O error";
  exchanger.ForceWriteFailure(false, io_error);
  EXPECT_CALL(visitor, OnWriteError(StrEq(io_error))).Times(1);
  exchanger.WritePacketToNetwork(packet.data(), packet.length());
  ASSERT_TRUE(exchanger.packets_written().empty());

  // Write error is delivered to visitor during SetWritable.
  exchanger.ForceWriteFailure(true, "");
  exchanger.WritePacketToNetwork(packet.data(), packet.length());

  std::string sys_error = "sys error";
  exchanger.ForceWriteFailure(false, sys_error);
  EXPECT_CALL(visitor, OnWriteError(StrEq(sys_error))).Times(1);
  exchanger.SetWritable();
  ASSERT_TRUE(exchanger.packets_written().empty());
}

}  // namespace
}  // namespace quic
