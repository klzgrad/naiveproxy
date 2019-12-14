// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_time_wait_list_manager.h"

#include <cerrno>
#include <memory>
#include <ostream>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_session_visitor.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_time_wait_list_manager_peer.h"

using testing::_;
using testing::Args;
using testing::Assign;
using testing::DoAll;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::StrictMock;
using testing::Truly;

namespace quic {
namespace test {
namespace {

class FramerVisitorCapturingPublicReset : public NoOpFramerVisitor {
 public:
  FramerVisitorCapturingPublicReset(QuicConnectionId connection_id)
      : connection_id_(connection_id) {}
  ~FramerVisitorCapturingPublicReset() override = default;

  void OnPublicResetPacket(const QuicPublicResetPacket& public_reset) override {
    public_reset_packet_ = public_reset;
  }

  const QuicPublicResetPacket public_reset_packet() {
    return public_reset_packet_;
  }

  bool IsValidStatelessResetToken(QuicUint128 token) const override {
    return token == QuicUtils::GenerateStatelessResetToken(connection_id_);
  }

  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& packet) override {
    stateless_reset_packet_ = packet;
  }

  const QuicIetfStatelessResetPacket stateless_reset_packet() {
    return stateless_reset_packet_;
  }

 private:
  QuicPublicResetPacket public_reset_packet_;
  QuicIetfStatelessResetPacket stateless_reset_packet_;
  QuicConnectionId connection_id_;
};

class MockAlarmFactory;
class MockAlarm : public QuicAlarm {
 public:
  explicit MockAlarm(QuicArenaScopedPtr<Delegate> delegate,
                     int alarm_index,
                     MockAlarmFactory* factory)
      : QuicAlarm(std::move(delegate)),
        alarm_index_(alarm_index),
        factory_(factory) {}
  virtual ~MockAlarm() {}

  void SetImpl() override;
  void CancelImpl() override;

 private:
  int alarm_index_;
  MockAlarmFactory* factory_;
};

class MockAlarmFactory : public QuicAlarmFactory {
 public:
  ~MockAlarmFactory() override {}

  // Creates a new platform-specific alarm which will be configured to notify
  // |delegate| when the alarm fires. Returns an alarm allocated on the heap.
  // Caller takes ownership of the new alarm, which will not yet be "set" to
  // fire.
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override {
    return new MockAlarm(QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate),
                         alarm_index_++, this);
  }
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override {
    if (arena != nullptr) {
      return arena->New<MockAlarm>(std::move(delegate), alarm_index_++, this);
    }
    return QuicArenaScopedPtr<MockAlarm>(
        new MockAlarm(std::move(delegate), alarm_index_++, this));
  }
  MOCK_METHOD2(OnAlarmSet, void(int, QuicTime));
  MOCK_METHOD1(OnAlarmCancelled, void(int));

 private:
  int alarm_index_ = 0;
};

void MockAlarm::SetImpl() {
  factory_->OnAlarmSet(alarm_index_, deadline());
}

void MockAlarm::CancelImpl() {
  factory_->OnAlarmCancelled(alarm_index_);
}

class QuicTimeWaitListManagerTest : public QuicTest {
 protected:
  QuicTimeWaitListManagerTest()
      : time_wait_list_manager_(&writer_, &visitor_, &clock_, &alarm_factory_),
        connection_id_(TestConnectionId(45)),
        peer_address_(TestPeerIPAddress(), kTestPort),
        writer_is_blocked_(false) {}

  ~QuicTimeWaitListManagerTest() override = default;

  void SetUp() override {
    EXPECT_CALL(writer_, IsWriteBlocked())
        .WillRepeatedly(ReturnPointee(&writer_is_blocked_));
  }

  void AddConnectionId(QuicConnectionId connection_id,
                       QuicTimeWaitListManager::TimeWaitAction action) {
    AddConnectionId(connection_id, QuicVersionMax(), action, nullptr);
  }

  void AddStatelessConnectionId(QuicConnectionId connection_id) {
    std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
    termination_packets.push_back(std::unique_ptr<QuicEncryptedPacket>(
        new QuicEncryptedPacket(nullptr, 0, false)));
    time_wait_list_manager_.AddConnectionIdToTimeWait(
        connection_id, false, QuicTimeWaitListManager::SEND_TERMINATION_PACKETS,
        ENCRYPTION_INITIAL, &termination_packets);
  }

  void AddConnectionId(
      QuicConnectionId connection_id,
      ParsedQuicVersion version,
      QuicTimeWaitListManager::TimeWaitAction action,
      std::vector<std::unique_ptr<QuicEncryptedPacket>>* packets) {
    time_wait_list_manager_.AddConnectionIdToTimeWait(
        connection_id, VersionHasIetfInvariantHeader(version.transport_version),
        action, ENCRYPTION_INITIAL, packets);
  }

  bool IsConnectionIdInTimeWait(QuicConnectionId connection_id) {
    return time_wait_list_manager_.IsConnectionIdInTimeWait(connection_id);
  }

  void ProcessPacket(QuicConnectionId connection_id) {
    time_wait_list_manager_.ProcessPacket(
        self_address_, peer_address_, connection_id, GOOGLE_QUIC_PACKET,
        std::make_unique<QuicPerPacketContext>());
  }

  QuicEncryptedPacket* ConstructEncryptedPacket(
      QuicConnectionId destination_connection_id,
      QuicConnectionId source_connection_id,
      uint64_t packet_number) {
    return quic::test::ConstructEncryptedPacket(destination_connection_id,
                                                source_connection_id, false,
                                                false, packet_number, "data");
  }

  MockClock clock_;
  MockAlarmFactory alarm_factory_;
  NiceMock<MockPacketWriter> writer_;
  StrictMock<MockQuicSessionVisitor> visitor_;
  QuicTimeWaitListManager time_wait_list_manager_;
  QuicConnectionId connection_id_;
  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;
  bool writer_is_blocked_;
};

bool ValidPublicResetPacketPredicate(
    QuicConnectionId expected_connection_id,
    const testing::tuple<const char*, int>& packet_buffer) {
  FramerVisitorCapturingPublicReset visitor(expected_connection_id);
  QuicFramer framer(AllSupportedVersions(), QuicTime::Zero(),
                    Perspective::IS_CLIENT, kQuicDefaultConnectionIdLength);
  framer.set_visitor(&visitor);
  QuicEncryptedPacket encrypted(testing::get<0>(packet_buffer),
                                testing::get<1>(packet_buffer));
  framer.ProcessPacket(encrypted);
  QuicPublicResetPacket packet = visitor.public_reset_packet();
  bool public_reset_is_valid =
      expected_connection_id == packet.connection_id &&
      TestPeerIPAddress() == packet.client_address.host() &&
      kTestPort == packet.client_address.port();

  QuicIetfStatelessResetPacket stateless_reset =
      visitor.stateless_reset_packet();

  QuicUint128 expected_stateless_reset_token =
      QuicUtils::GenerateStatelessResetToken(expected_connection_id);

  bool stateless_reset_is_valid =
      stateless_reset.stateless_reset_token == expected_stateless_reset_token;

  return public_reset_is_valid || stateless_reset_is_valid;
}

Matcher<const testing::tuple<const char*, int>> PublicResetPacketEq(
    QuicConnectionId connection_id) {
  return Truly(
      [connection_id](const testing::tuple<const char*, int> packet_buffer) {
        return ValidPublicResetPacketPredicate(connection_id, packet_buffer);
      });
}

TEST_F(QuicTimeWaitListManagerTest, CheckConnectionIdInTimeWait) {
  EXPECT_FALSE(IsConnectionIdInTimeWait(connection_id_));
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddConnectionId(connection_id_, QuicTimeWaitListManager::DO_NOTHING);
  EXPECT_EQ(1u, time_wait_list_manager_.num_connections());
  EXPECT_TRUE(IsConnectionIdInTimeWait(connection_id_));
}

TEST_F(QuicTimeWaitListManagerTest, CheckStatelessConnectionIdInTimeWait) {
  EXPECT_FALSE(IsConnectionIdInTimeWait(connection_id_));
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddStatelessConnectionId(connection_id_);
  EXPECT_EQ(1u, time_wait_list_manager_.num_connections());
  EXPECT_TRUE(IsConnectionIdInTimeWait(connection_id_));
}

TEST_F(QuicTimeWaitListManagerTest, SendVersionNegotiationPacket) {
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/false,
          /*use_length_prefix=*/false, AllSupportedVersions()));
  EXPECT_CALL(writer_, WritePacket(_, packet->length(), self_address_.host(),
                                   peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  time_wait_list_manager_.SendVersionNegotiationPacket(
      connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/false,
      /*use_length_prefix=*/false, AllSupportedVersions(), self_address_,
      peer_address_, std::make_unique<QuicPerPacketContext>());
  EXPECT_EQ(0u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest,
       SendIetfVersionNegotiationPacketWithoutLengthPrefix) {
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/true,
          /*use_length_prefix=*/false, AllSupportedVersions()));
  EXPECT_CALL(writer_, WritePacket(_, packet->length(), self_address_.host(),
                                   peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  time_wait_list_manager_.SendVersionNegotiationPacket(
      connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/true,
      /*use_length_prefix=*/false, AllSupportedVersions(), self_address_,
      peer_address_, std::make_unique<QuicPerPacketContext>());
  EXPECT_EQ(0u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest, SendIetfVersionNegotiationPacket) {
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/true,
          /*use_length_prefix=*/true, AllSupportedVersions()));
  EXPECT_CALL(writer_, WritePacket(_, packet->length(), self_address_.host(),
                                   peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  time_wait_list_manager_.SendVersionNegotiationPacket(
      connection_id_, EmptyQuicConnectionId(), /*ietf_quic=*/true,
      /*use_length_prefix=*/true, AllSupportedVersions(), self_address_,
      peer_address_, std::make_unique<QuicPerPacketContext>());
  EXPECT_EQ(0u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest,
       SendIetfVersionNegotiationPacketWithClientConnectionId) {
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, TestConnectionId(0x33), /*ietf_quic=*/true,
          /*use_length_prefix=*/true, AllSupportedVersions()));
  EXPECT_CALL(writer_, WritePacket(_, packet->length(), self_address_.host(),
                                   peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  time_wait_list_manager_.SendVersionNegotiationPacket(
      connection_id_, TestConnectionId(0x33), /*ietf_quic=*/true,
      /*use_length_prefix=*/true, AllSupportedVersions(), self_address_,
      peer_address_, std::make_unique<QuicPerPacketContext>());
  EXPECT_EQ(0u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest, SendConnectionClose) {
  const size_t kConnectionCloseLength = 100;
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  termination_packets.push_back(
      std::unique_ptr<QuicEncryptedPacket>(new QuicEncryptedPacket(
          new char[kConnectionCloseLength], kConnectionCloseLength, true)));
  AddConnectionId(connection_id_, QuicVersionMax(),
                  QuicTimeWaitListManager::SEND_TERMINATION_PACKETS,
                  &termination_packets);
  EXPECT_CALL(writer_, WritePacket(_, kConnectionCloseLength,
                                   self_address_.host(), peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  ProcessPacket(connection_id_);
}

TEST_F(QuicTimeWaitListManagerTest, SendTwoConnectionCloses) {
  const size_t kConnectionCloseLength = 100;
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  termination_packets.push_back(
      std::unique_ptr<QuicEncryptedPacket>(new QuicEncryptedPacket(
          new char[kConnectionCloseLength], kConnectionCloseLength, true)));
  termination_packets.push_back(
      std::unique_ptr<QuicEncryptedPacket>(new QuicEncryptedPacket(
          new char[kConnectionCloseLength], kConnectionCloseLength, true)));
  AddConnectionId(connection_id_, QuicVersionMax(),
                  QuicTimeWaitListManager::SEND_TERMINATION_PACKETS,
                  &termination_packets);
  EXPECT_CALL(writer_, WritePacket(_, kConnectionCloseLength,
                                   self_address_.host(), peer_address_, _))
      .Times(2)
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  ProcessPacket(connection_id_);
}

TEST_F(QuicTimeWaitListManagerTest, SendPublicReset) {
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddConnectionId(connection_id_,
                  QuicTimeWaitListManager::SEND_STATELESS_RESET);
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(connection_id_)))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));

  ProcessPacket(connection_id_);
}

TEST_F(QuicTimeWaitListManagerTest, SendPublicResetWithExponentialBackOff) {
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddConnectionId(connection_id_,
                  QuicTimeWaitListManager::SEND_STATELESS_RESET);
  EXPECT_EQ(1u, time_wait_list_manager_.num_connections());
  for (int packet_number = 1; packet_number < 101; ++packet_number) {
    if ((packet_number & (packet_number - 1)) == 0) {
      EXPECT_CALL(writer_, WritePacket(_, _, _, _, _))
          .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));
    }
    ProcessPacket(connection_id_);
    // Send public reset with exponential back off.
    if ((packet_number & (packet_number - 1)) == 0) {
      EXPECT_TRUE(QuicTimeWaitListManagerPeer::ShouldSendResponse(
          &time_wait_list_manager_, packet_number));
    } else {
      EXPECT_FALSE(QuicTimeWaitListManagerPeer::ShouldSendResponse(
          &time_wait_list_manager_, packet_number));
    }
  }
}

TEST_F(QuicTimeWaitListManagerTest, NoPublicResetForStatelessConnections) {
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddStatelessConnectionId(connection_id_);

  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  ProcessPacket(connection_id_);
}

TEST_F(QuicTimeWaitListManagerTest, CleanUpOldConnectionIds) {
  const size_t kConnectionIdCount = 100;
  const size_t kOldConnectionIdCount = 31;

  // Add connection_ids such that their expiry time is time_wait_period_.
  for (uint64_t conn_id = 1; conn_id <= kOldConnectionIdCount; ++conn_id) {
    QuicConnectionId connection_id = TestConnectionId(conn_id);
    EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id));
    AddConnectionId(connection_id, QuicTimeWaitListManager::DO_NOTHING);
  }
  EXPECT_EQ(kOldConnectionIdCount, time_wait_list_manager_.num_connections());

  // Add remaining connection_ids such that their add time is
  // 2 * time_wait_period_.
  const QuicTime::Delta time_wait_period =
      QuicTimeWaitListManagerPeer::time_wait_period(&time_wait_list_manager_);
  clock_.AdvanceTime(time_wait_period);
  for (uint64_t conn_id = kOldConnectionIdCount + 1;
       conn_id <= kConnectionIdCount; ++conn_id) {
    QuicConnectionId connection_id = TestConnectionId(conn_id);
    EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id));
    AddConnectionId(connection_id, QuicTimeWaitListManager::DO_NOTHING);
  }
  EXPECT_EQ(kConnectionIdCount, time_wait_list_manager_.num_connections());

  QuicTime::Delta offset = QuicTime::Delta::FromMicroseconds(39);
  // Now set the current time as time_wait_period + offset usecs.
  clock_.AdvanceTime(offset);
  // After all the old connection_ids are cleaned up, check the next alarm
  // interval.
  QuicTime next_alarm_time = clock_.Now() + time_wait_period - offset;
  EXPECT_CALL(alarm_factory_, OnAlarmSet(_, next_alarm_time));

  time_wait_list_manager_.CleanUpOldConnectionIds();
  for (uint64_t conn_id = 1; conn_id <= kConnectionIdCount; ++conn_id) {
    QuicConnectionId connection_id = TestConnectionId(conn_id);
    EXPECT_EQ(conn_id > kOldConnectionIdCount,
              IsConnectionIdInTimeWait(connection_id))
        << "kOldConnectionIdCount: " << kOldConnectionIdCount
        << " connection_id: " << connection_id;
  }
  EXPECT_EQ(kConnectionIdCount - kOldConnectionIdCount,
            time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest, SendQueuedPackets) {
  QuicConnectionId connection_id = TestConnectionId(1);
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id));
  AddConnectionId(connection_id, QuicTimeWaitListManager::SEND_STATELESS_RESET);
  std::unique_ptr<QuicEncryptedPacket> packet(ConstructEncryptedPacket(
      connection_id, EmptyQuicConnectionId(), /*packet_number=*/234));
  // Let first write through.
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(connection_id)))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, packet->length())));
  ProcessPacket(connection_id);

  // write block for the next packet.
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(connection_id)))
      .WillOnce(DoAll(Assign(&writer_is_blocked_, true),
                      Return(WriteResult(WRITE_STATUS_BLOCKED, EAGAIN))));
  EXPECT_CALL(visitor_, OnWriteBlocked(&time_wait_list_manager_));
  ProcessPacket(connection_id);
  // 3rd packet. No public reset should be sent;
  ProcessPacket(connection_id);

  // write packet should not be called since we are write blocked but the
  // should be queued.
  QuicConnectionId other_connection_id = TestConnectionId(2);
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(other_connection_id));
  AddConnectionId(other_connection_id,
                  QuicTimeWaitListManager::SEND_STATELESS_RESET);
  std::unique_ptr<QuicEncryptedPacket> other_packet(ConstructEncryptedPacket(
      other_connection_id, EmptyQuicConnectionId(), /*packet_number=*/23423));
  EXPECT_CALL(writer_, WritePacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(visitor_, OnWriteBlocked(&time_wait_list_manager_));
  ProcessPacket(other_connection_id);
  EXPECT_EQ(2u, time_wait_list_manager_.num_connections());

  // Now expect all the write blocked public reset packets to be sent again.
  writer_is_blocked_ = false;
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(connection_id)))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, packet->length())));
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(other_connection_id)))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, packet->length())));
  time_wait_list_manager_.OnBlockedWriterCanWrite();
}

TEST_F(QuicTimeWaitListManagerTest, AddConnectionIdTwice) {
  // Add connection_ids such that their expiry time is time_wait_period_.
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  AddConnectionId(connection_id_, QuicTimeWaitListManager::DO_NOTHING);
  EXPECT_TRUE(IsConnectionIdInTimeWait(connection_id_));
  const size_t kConnectionCloseLength = 100;
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  termination_packets.push_back(
      std::unique_ptr<QuicEncryptedPacket>(new QuicEncryptedPacket(
          new char[kConnectionCloseLength], kConnectionCloseLength, true)));
  AddConnectionId(connection_id_, QuicVersionMax(),
                  QuicTimeWaitListManager::SEND_TERMINATION_PACKETS,
                  &termination_packets);
  EXPECT_TRUE(IsConnectionIdInTimeWait(connection_id_));
  EXPECT_EQ(1u, time_wait_list_manager_.num_connections());

  EXPECT_CALL(writer_, WritePacket(_, kConnectionCloseLength,
                                   self_address_.host(), peer_address_, _))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 1)));

  ProcessPacket(connection_id_);

  const QuicTime::Delta time_wait_period =
      QuicTimeWaitListManagerPeer::time_wait_period(&time_wait_list_manager_);

  QuicTime::Delta offset = QuicTime::Delta::FromMicroseconds(39);
  clock_.AdvanceTime(offset + time_wait_period);
  // Now set the current time as time_wait_period + offset usecs.
  QuicTime next_alarm_time = clock_.Now() + time_wait_period;
  EXPECT_CALL(alarm_factory_, OnAlarmSet(_, next_alarm_time));

  time_wait_list_manager_.CleanUpOldConnectionIds();
  EXPECT_FALSE(IsConnectionIdInTimeWait(connection_id_));
  EXPECT_EQ(0u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest, ConnectionIdsOrderedByTime) {
  // Simple randomization: the values of connection_ids are randomly swapped.
  // If the container is broken, the test will be 50% flaky.
  const uint64_t conn_id1 = QuicRandom::GetInstance()->RandUint64() % 2;
  const QuicConnectionId connection_id1 = TestConnectionId(conn_id1);
  const QuicConnectionId connection_id2 = TestConnectionId(1 - conn_id1);

  // 1 will hash lower than 2, but we add it later. They should come out in the
  // add order, not hash order.
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id1));
  AddConnectionId(connection_id1, QuicTimeWaitListManager::DO_NOTHING);
  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(10));
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id2));
  AddConnectionId(connection_id2, QuicTimeWaitListManager::DO_NOTHING);
  EXPECT_EQ(2u, time_wait_list_manager_.num_connections());

  const QuicTime::Delta time_wait_period =
      QuicTimeWaitListManagerPeer::time_wait_period(&time_wait_list_manager_);
  clock_.AdvanceTime(time_wait_period - QuicTime::Delta::FromMicroseconds(9));

  EXPECT_CALL(alarm_factory_, OnAlarmSet(_, _));

  time_wait_list_manager_.CleanUpOldConnectionIds();
  EXPECT_FALSE(IsConnectionIdInTimeWait(connection_id1));
  EXPECT_TRUE(IsConnectionIdInTimeWait(connection_id2));
  EXPECT_EQ(1u, time_wait_list_manager_.num_connections());
}

TEST_F(QuicTimeWaitListManagerTest, MaxConnectionsTest) {
  // Basically, shut off time-based eviction.
  SetQuicFlag(FLAGS_quic_time_wait_list_seconds, 10000000000);
  SetQuicFlag(FLAGS_quic_time_wait_list_max_connections, 5);

  uint64_t current_conn_id = 0;
  const int64_t kMaxConnections =
      GetQuicFlag(FLAGS_quic_time_wait_list_max_connections);
  // Add exactly the maximum number of connections
  for (int64_t i = 0; i < kMaxConnections; ++i) {
    ++current_conn_id;
    QuicConnectionId current_connection_id = TestConnectionId(current_conn_id);
    EXPECT_FALSE(IsConnectionIdInTimeWait(current_connection_id));
    EXPECT_CALL(visitor_,
                OnConnectionAddedToTimeWaitList(current_connection_id));
    AddConnectionId(current_connection_id, QuicTimeWaitListManager::DO_NOTHING);
    EXPECT_EQ(current_conn_id, time_wait_list_manager_.num_connections());
    EXPECT_TRUE(IsConnectionIdInTimeWait(current_connection_id));
  }

  // Now keep adding.  Since we're already at the max, every new connection-id
  // will evict the oldest one.
  for (int64_t i = 0; i < kMaxConnections; ++i) {
    ++current_conn_id;
    QuicConnectionId current_connection_id = TestConnectionId(current_conn_id);
    const QuicConnectionId id_to_evict =
        TestConnectionId(current_conn_id - kMaxConnections);
    EXPECT_TRUE(IsConnectionIdInTimeWait(id_to_evict));
    EXPECT_FALSE(IsConnectionIdInTimeWait(current_connection_id));
    EXPECT_CALL(visitor_,
                OnConnectionAddedToTimeWaitList(current_connection_id));
    AddConnectionId(current_connection_id, QuicTimeWaitListManager::DO_NOTHING);
    EXPECT_EQ(static_cast<size_t>(kMaxConnections),
              time_wait_list_manager_.num_connections());
    EXPECT_FALSE(IsConnectionIdInTimeWait(id_to_evict));
    EXPECT_TRUE(IsConnectionIdInTimeWait(current_connection_id));
  }
}

TEST_F(QuicTimeWaitListManagerTest, ZeroMaxConnections) {
  // Basically, shut off time-based eviction.
  SetQuicFlag(FLAGS_quic_time_wait_list_seconds, 10000000000);
  // Keep time wait list empty.
  SetQuicFlag(FLAGS_quic_time_wait_list_max_connections, 0);

  uint64_t current_conn_id = 0;
  // Add exactly the maximum number of connections
  for (int64_t i = 0; i < 10; ++i) {
    ++current_conn_id;
    QuicConnectionId current_connection_id = TestConnectionId(current_conn_id);
    EXPECT_FALSE(IsConnectionIdInTimeWait(current_connection_id));
    EXPECT_CALL(visitor_,
                OnConnectionAddedToTimeWaitList(current_connection_id));
    AddConnectionId(current_connection_id, QuicTimeWaitListManager::DO_NOTHING);
    // Verify time wait list always has 1 connection.
    EXPECT_EQ(1u, time_wait_list_manager_.num_connections());
    EXPECT_TRUE(IsConnectionIdInTimeWait(current_connection_id));
  }
}

// Regression test for b/116200989.
TEST_F(QuicTimeWaitListManagerTest,
       SendStatelessResetInResponseToShortHeaders) {
  // This test mimics a scenario where an ENCRYPTION_INITIAL connection close is
  // added as termination packet for an IETF connection ID. However, a short
  // header packet is received later.
  const size_t kConnectionCloseLength = 100;
  EXPECT_CALL(visitor_, OnConnectionAddedToTimeWaitList(connection_id_));
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  termination_packets.push_back(
      std::unique_ptr<QuicEncryptedPacket>(new QuicEncryptedPacket(
          new char[kConnectionCloseLength], kConnectionCloseLength, true)));
  // Add an ENCRYPTION_INITIAL termination packet.
  time_wait_list_manager_.AddConnectionIdToTimeWait(
      connection_id_, /*ietf_quic=*/true,
      QuicTimeWaitListManager::SEND_TERMINATION_PACKETS, ENCRYPTION_INITIAL,
      &termination_packets);

  // Termination packet is not encrypted, instead, send stateless reset.
  EXPECT_CALL(writer_,
              WritePacket(_, _, self_address_.host(), peer_address_, _))
      .With(Args<0, 1>(PublicResetPacketEq(connection_id_)))
      .WillOnce(Return(WriteResult(WRITE_STATUS_OK, 0)));
  // Processes IETF short header packet.
  time_wait_list_manager_.ProcessPacket(
      self_address_, peer_address_, connection_id_,
      IETF_QUIC_SHORT_HEADER_PACKET, std::make_unique<QuicPerPacketContext>());
}

}  // namespace
}  // namespace test
}  // namespace quic
