// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device_packet_exchanger.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/bonnet/mock_packet_exchanger_stats_interface.h"
#include "net/third_party/quiche/src/quic/qbone/mock_qbone_client.h"
#include "net/third_party/quiche/src/quic/qbone/platform/mock_kernel.h"

namespace quic {
namespace {

const size_t kMtu = 1000;
const size_t kMaxPendingPackets = 5;
const int kFd = 15;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrEq;
using ::testing::StrictMock;

class MockVisitor : public QbonePacketExchanger::Visitor {
 public:
  MOCK_METHOD1(OnReadError, void(const std::string&));
  MOCK_METHOD1(OnWriteError, void(const std::string&));
};

class TunDevicePacketExchangerTest : public QuicTest {
 protected:
  TunDevicePacketExchangerTest()
      : exchanger_(kFd,
                   kMtu,
                   &mock_kernel_,
                   &mock_visitor_,
                   kMaxPendingPackets,
                   &mock_stats_) {}

  ~TunDevicePacketExchangerTest() override {}

  MockKernel mock_kernel_;
  StrictMock<MockVisitor> mock_visitor_;
  StrictMock<MockQboneClient> mock_client_;
  StrictMock<MockPacketExchangerStatsInterface> mock_stats_;
  TunDevicePacketExchanger exchanger_;
};

TEST_F(TunDevicePacketExchangerTest, WritePacketReturnsFalseOnError) {
  std::string packet = "fake packet";
  EXPECT_CALL(mock_kernel_, write(kFd, _, packet.size()))
      .WillOnce(Invoke([](int fd, const void* buf, size_t count) {
        errno = ECOMM;
        return -1;
      }));

  EXPECT_CALL(mock_visitor_, OnWriteError(_));
  exchanger_.WritePacketToNetwork(packet.data(), packet.size());
}

TEST_F(TunDevicePacketExchangerTest,
       WritePacketReturnFalseAndBlockedOnBlockedTunnel) {
  std::string packet = "fake packet";
  EXPECT_CALL(mock_kernel_, write(kFd, _, packet.size()))
      .WillOnce(Invoke([](int fd, const void* buf, size_t count) {
        errno = EAGAIN;
        return -1;
      }));

  EXPECT_CALL(mock_stats_, OnWriteError(_)).Times(1);
  exchanger_.WritePacketToNetwork(packet.data(), packet.size());
}

TEST_F(TunDevicePacketExchangerTest, WritePacketReturnsTrueOnSuccessfulWrite) {
  std::string packet = "fake packet";
  EXPECT_CALL(mock_kernel_, write(kFd, _, packet.size()))
      .WillOnce(Invoke([packet](int fd, const void* buf, size_t count) {
        EXPECT_THAT(reinterpret_cast<const char*>(buf), StrEq(packet));
        return count;
      }));

  EXPECT_CALL(mock_stats_, OnPacketWritten(_)).Times(1);
  exchanger_.WritePacketToNetwork(packet.data(), packet.size());
}

TEST_F(TunDevicePacketExchangerTest, ReadPacketReturnsNullOnError) {
  EXPECT_CALL(mock_kernel_, read(kFd, _, kMtu))
      .WillOnce(Invoke([](int fd, void* buf, size_t count) {
        errno = ECOMM;
        return -1;
      }));
  EXPECT_CALL(mock_visitor_, OnReadError(_));
  exchanger_.ReadAndDeliverPacket(&mock_client_);
}

TEST_F(TunDevicePacketExchangerTest, ReadPacketReturnsNullOnBlockedRead) {
  EXPECT_CALL(mock_kernel_, read(kFd, _, kMtu))
      .WillOnce(Invoke([](int fd, void* buf, size_t count) {
        errno = EAGAIN;
        return -1;
      }));
  EXPECT_CALL(mock_stats_, OnReadError(_)).Times(1);
  EXPECT_FALSE(exchanger_.ReadAndDeliverPacket(&mock_client_));
}

TEST_F(TunDevicePacketExchangerTest,
       ReadPacketReturnsThePacketOnSuccessfulRead) {
  std::string packet = "fake_packet";
  EXPECT_CALL(mock_kernel_, read(kFd, _, kMtu))
      .WillOnce(Invoke([packet](int fd, void* buf, size_t count) {
        memcpy(buf, packet.data(), packet.size());
        return packet.size();
      }));
  EXPECT_CALL(mock_client_, ProcessPacketFromNetwork(StrEq(packet)));
  EXPECT_CALL(mock_stats_, OnPacketRead(_)).Times(1);
  EXPECT_TRUE(exchanger_.ReadAndDeliverPacket(&mock_client_));
}

}  // namespace
}  // namespace quic
