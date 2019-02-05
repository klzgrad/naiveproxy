// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_connectivity_probing_manager.h"

#include "base/stl_util.h"
#include "base/test/test_mock_time_task_runner.h"
#include "net/log/test_net_log.h"
#include "net/socket/socket_test_util.h"
#include "net/test/gtest_util.h"
#include "net/third_party/quic/test_tools/mock_clock.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Return;

namespace net {
namespace test {
namespace {

const IPEndPoint kIpEndPoint =
    IPEndPoint(IPAddress::IPv4AllZeros(), quic::test::kTestPort);

const NetworkChangeNotifier::NetworkHandle testNetworkHandle = 1;
const quic::QuicSocketAddress testPeerAddress =
    quic::QuicSocketAddress(quic::QuicSocketAddressImpl(kIpEndPoint));

}  // anonymous namespace

class MockQuicChromiumClientSession
    : public QuicConnectivityProbingManager::Delegate,
      public QuicChromiumPacketReader::Visitor {
 public:
  MockQuicChromiumClientSession()
      : successful_network_(NetworkChangeNotifier::kInvalidNetworkHandle) {}
  ~MockQuicChromiumClientSession() override {}

  // QuicChromiumPacketReader::Visitor interface.
  MOCK_METHOD2(OnReadError,
               void(int result, const DatagramClientSocket* socket));

  MOCK_METHOD3(OnPacket,
               bool(const quic::QuicReceivedPacket& packet,
                    const quic::QuicSocketAddress& local_address,
                    const quic::QuicSocketAddress& peer_address));

  MOCK_METHOD1(OnProbeNetworkFailed,
               void(NetworkChangeNotifier::NetworkHandle network));

  MOCK_METHOD2(OnSendConnectivityProbingPacket,
               bool(QuicChromiumPacketWriter* writer,
                    const quic::QuicSocketAddress& peer_address));

  void OnProbeNetworkSucceeded(
      NetworkChangeNotifier::NetworkHandle network,
      const quic::QuicSocketAddress& self_address,
      std::unique_ptr<DatagramClientSocket> socket,
      std::unique_ptr<QuicChromiumPacketWriter> writer,
      std::unique_ptr<QuicChromiumPacketReader> reader) override {
    successful_network_ = network;
  }

  NetworkChangeNotifier::NetworkHandle successful_network() {
    return successful_network_;
  }

 private:
  NetworkChangeNotifier::NetworkHandle successful_network_;

  DISALLOW_COPY_AND_ASSIGN(MockQuicChromiumClientSession);
};

class QuicConnectivityProbingManagerTest : public ::testing::Test {
 public:
  QuicConnectivityProbingManagerTest()
      : test_task_runner_(new base::TestMockTimeTaskRunner()),
        test_task_runner_context_(test_task_runner_),
        probing_manager_(&session_, test_task_runner_.get()),
        default_read_(new MockRead(SYNCHRONOUS, ERR_IO_PENDING, 0)),
        socket_data_(
            new SequencedSocketData(base::make_span(default_read_.get(), 1),
                                    base::span<MockWrite>())) {
    socket_factory_.AddSocketDataProvider(socket_data_.get());
    // Create a connected socket for probing.
    socket_ = socket_factory_.CreateDatagramClientSocket(
        DatagramSocket::DEFAULT_BIND, &net_log_, NetLogSource());
    EXPECT_THAT(socket_->Connect(kIpEndPoint), IsOk());
    IPEndPoint self_address;
    socket_->GetLocalAddress(&self_address);
    self_address_ =
        quic::QuicSocketAddress(quic::QuicSocketAddressImpl(self_address));
    // Create packet writer and reader for probing.
    writer_.reset(
        new QuicChromiumPacketWriter(socket_.get(), test_task_runner_.get()));
    reader_.reset(new QuicChromiumPacketReader(
        socket_.get(), &clock_, &session_, kQuicYieldAfterPacketsRead,
        quic::QuicTime::Delta::FromMilliseconds(
            kQuicYieldAfterDurationMilliseconds),
        bound_test_net_log_.bound()));
  }

 protected:
  // All tests will run inside the scope of |test_task_runner_|.
  scoped_refptr<base::TestMockTimeTaskRunner> test_task_runner_;
  base::TestMockTimeTaskRunner::ScopedContext test_task_runner_context_;
  MockQuicChromiumClientSession session_;
  QuicConnectivityProbingManager probing_manager_;

  std::unique_ptr<MockRead> default_read_;
  std::unique_ptr<SequencedSocketData> socket_data_;

  std::unique_ptr<DatagramClientSocket> socket_;
  std::unique_ptr<QuicChromiumPacketWriter> writer_;
  std::unique_ptr<QuicChromiumPacketReader> reader_;
  quic::QuicSocketAddress self_address_;

  quic::MockClock clock_;
  MockClientSocketFactory socket_factory_;
  TestNetLog net_log_;
  BoundTestNetLog bound_test_net_log_;

  DISALLOW_COPY_AND_ASSIGN(QuicConnectivityProbingManagerTest);
};

TEST_F(QuicConnectivityProbingManagerTest, ReceiveProbingResponseOnSamePath) {
  int initial_timeout_ms = 100;

  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  probing_manager_.StartProbing(
      testNetworkHandle, testPeerAddress, std::move(socket_),
      std::move(writer_), std::move(reader_),
      base::TimeDelta::FromMilliseconds(initial_timeout_ms),
      bound_test_net_log_.bound());
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, timeout the first connectivity probing
  // packet, introduce another probing packet to sent out with timeout set to
  // 2 * initial_timeout_ms.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));

  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, should be no-op.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Notify the manager a connectivity probing packet is received from
  // testPeerAddress to |self_address_|, manager should decalre probing as
  // successful, notify delegate and will no longer send connectivity probing
  // packet for this probing.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  probing_manager_.OnConnectivityProbingReceived(self_address_,
                                                 testPeerAddress);
  EXPECT_EQ(session_.successful_network(), testNetworkHandle);
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Verify there's nothing to send.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(0u, test_task_runner_->GetPendingTaskCount());
}

TEST_F(QuicConnectivityProbingManagerTest,
       ReceiveProbingResponseOnDifferentPath) {
  int initial_timeout_ms = 100;

  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  probing_manager_.StartProbing(
      testNetworkHandle, testPeerAddress, std::move(socket_),
      std::move(writer_), std::move(reader_),
      base::TimeDelta::FromMilliseconds(initial_timeout_ms),
      bound_test_net_log_.bound());
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, timeout the first connectivity probing
  // packet, introduce another probing packet to sent out with timeout set to
  // 2 * initial_timeout_ms.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, should be no-op.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Notify the manager a connectivity probing packet is received from
  // testPeerAddress to a different self address, manager should ignore the
  // probing response and continue waiting.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  probing_manager_.OnConnectivityProbingReceived(quic::QuicSocketAddress(),
                                                 testPeerAddress);
  EXPECT_NE(session_.successful_network(), testNetworkHandle);
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward another initial_timeout_ms, another probing packet will be
  // sent.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Finally receive the probing response on the same path.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  probing_manager_.OnConnectivityProbingReceived(self_address_,
                                                 testPeerAddress);
  EXPECT_EQ(session_.successful_network(), testNetworkHandle);
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Verify there's nothing to send.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->RunUntilIdle();
}

TEST_F(QuicConnectivityProbingManagerTest, RetryProbingWithExponentailBackoff) {
  int initial_timeout_ms = 100;

  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  probing_manager_.StartProbing(
      testNetworkHandle, testPeerAddress, std::move(socket_),
      std::move(writer_), std::move(reader_),
      base::TimeDelta::FromMilliseconds(initial_timeout_ms),
      bound_test_net_log_.bound());
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // For expential backoff, this will try to resend: 100ms, 200ms, 400ms, 800ms,
  // 1600ms.
  for (int retry_count = 0; retry_count < 4; retry_count++) {
    EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
        .WillOnce(Return(true));
    int timeout_ms = (1 << retry_count) * initial_timeout_ms;
    test_task_runner_->FastForwardBy(
        base::TimeDelta::FromMilliseconds(timeout_ms));
    EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());
  }

  // Move forward another 1600ms, expect probing manager will no longer send any
  // connectivity probing packet but declare probing as failed .
  EXPECT_CALL(session_, OnProbeNetworkFailed(testNetworkHandle)).Times(1);
  int timeout_ms = (1 << 4) * initial_timeout_ms;
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(timeout_ms));
  EXPECT_EQ(0u, test_task_runner_->GetPendingTaskCount());
}

TEST_F(QuicConnectivityProbingManagerTest, CancelProbing) {
  int initial_timeout_ms = 100;

  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  probing_manager_.StartProbing(
      testNetworkHandle, testPeerAddress, std::move(socket_),
      std::move(writer_), std::move(reader_),
      base::TimeDelta::FromMilliseconds(initial_timeout_ms),
      bound_test_net_log_.bound());
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, timeout the first connectivity probing
  // packet, introduce another probing packet to sent out with timeout set to
  // 2 * initial_timeout_ms.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, should be no-op.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Request cancel probing, manager will no longer send connectivity probing
  // packet for this probing.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, _)).Times(0);
  EXPECT_CALL(session_, OnProbeNetworkFailed(_)).Times(0);
  probing_manager_.CancelProbing(testNetworkHandle);
  test_task_runner_->RunUntilIdle();
}

TEST_F(QuicConnectivityProbingManagerTest, ProbingWriterError) {
  int initial_timeout_ms = 100;

  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  QuicChromiumPacketWriter* writer_ptr = writer_.get();
  probing_manager_.StartProbing(
      testNetworkHandle, testPeerAddress, std::move(socket_),
      std::move(writer_), std::move(reader_),
      base::TimeDelta::FromMilliseconds(initial_timeout_ms),
      bound_test_net_log_.bound());
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, timeout the first connectivity probing
  // packet, introduce another probing packet to sent out with timeout set to
  // 2 * initial_timeout_ms.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .WillOnce(Return(true));
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Fast forward initial_timeout_ms, should be no-op.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, testPeerAddress))
      .Times(0);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(1u, test_task_runner_->GetPendingTaskCount());

  // Probing packet writer received an write error, notifies manager to handle
  // write error. Manager will notify session of the probe failure, cancel
  // probing to prevent future connectivity probing packet to be sent.
  EXPECT_CALL(session_, OnSendConnectivityProbingPacket(_, _)).Times(0);
  EXPECT_CALL(session_, OnProbeNetworkFailed(testNetworkHandle)).Times(1);
  writer_ptr->OnWriteComplete(ERR_CONNECTION_CLOSED);
  test_task_runner_->FastForwardBy(
      base::TimeDelta::FromMilliseconds(initial_timeout_ms));
  EXPECT_EQ(0u, test_task_runner_->GetPendingTaskCount());
}

}  // namespace test
}  // namespace net
