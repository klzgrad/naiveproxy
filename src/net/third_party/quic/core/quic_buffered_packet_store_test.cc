// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_buffered_packet_store.h"

#include <list>

#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/test_tools/mock_clock.h"
#include "net/third_party/quic/test_tools/quic_buffered_packet_store_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

namespace quic {

typedef QuicBufferedPacketStore::BufferedPacket BufferedPacket;
typedef QuicBufferedPacketStore::EnqueuePacketResult EnqueuePacketResult;

static const size_t kDefaultMaxConnectionsInStore = 100;
static const size_t kMaxConnectionsWithoutCHLO =
    kDefaultMaxConnectionsInStore / 2;

namespace test {
namespace {

typedef QuicBufferedPacketStore::BufferedPacket BufferedPacket;
typedef QuicBufferedPacketStore::BufferedPacketList BufferedPacketList;

class QuicBufferedPacketStoreVisitor
    : public QuicBufferedPacketStore::VisitorInterface {
 public:
  QuicBufferedPacketStoreVisitor() {}

  ~QuicBufferedPacketStoreVisitor() override {}

  void OnExpiredPackets(QuicConnectionId connection_id,
                        BufferedPacketList early_arrived_packets) override {
    last_expired_packet_queue_ = std::move(early_arrived_packets);
  }

  // The packets queue for most recently expirect connection.
  BufferedPacketList last_expired_packet_queue_;
};

class QuicBufferedPacketStoreTest : public QuicTest {
 public:
  QuicBufferedPacketStoreTest()
      : store_(&visitor_, &clock_, &alarm_factory_),
        server_address_(QuicIpAddress::Any6(), 65535),
        client_address_(QuicIpAddress::Any6(), 65535),
        packet_content_("some encrypted content"),
        packet_time_(QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(42)),
        packet_(packet_content_.data(), packet_content_.size(), packet_time_) {}

 protected:
  QuicBufferedPacketStoreVisitor visitor_;
  MockClock clock_;
  MockAlarmFactory alarm_factory_;
  QuicBufferedPacketStore store_;
  QuicSocketAddress server_address_;
  QuicSocketAddress client_address_;
  QuicString packet_content_;
  QuicTime packet_time_;
  QuicReceivedPacket packet_;
};

TEST_F(QuicBufferedPacketStoreTest, SimpleEnqueueAndDeliverPacket) {
  QuicConnectionId connection_id = 1;
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id));
  auto packets = store_.DeliverPackets(connection_id);
  const std::list<BufferedPacket>& queue = packets.buffered_packets;
  ASSERT_EQ(1u, queue.size());
  // The alpn should be ignored for non-chlo packets.
  ASSERT_EQ("", packets.alpn);
  // Check content of the only packet in the queue.
  EXPECT_EQ(packet_content_, queue.front().packet->AsStringPiece());
  EXPECT_EQ(packet_time_, queue.front().packet->receipt_time());
  EXPECT_EQ(client_address_, queue.front().client_address);
  EXPECT_EQ(server_address_, queue.front().server_address);
  // No more packets on connection 1 should remain in the store.
  EXPECT_TRUE(store_.DeliverPackets(connection_id).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
}

TEST_F(QuicBufferedPacketStoreTest, DifferentPacketAddressOnOneConnection) {
  QuicSocketAddress addr_with_new_port(QuicIpAddress::Any4(), 256);
  QuicConnectionId connection_id = 1;
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       addr_with_new_port, false, "");
  std::list<BufferedPacket> queue =
      store_.DeliverPackets(connection_id).buffered_packets;
  ASSERT_EQ(2u, queue.size());
  // The address migration path should be preserved.
  EXPECT_EQ(client_address_, queue.front().client_address);
  EXPECT_EQ(addr_with_new_port, queue.back().client_address);
}

TEST_F(QuicBufferedPacketStoreTest,
       EnqueueAndDeliverMultiplePacketsOnMultipleConnections) {
  size_t num_connections = 10;
  for (QuicConnectionId connection_id = 1; connection_id <= num_connections;
       ++connection_id) {
    store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                         client_address_, false, "");
    store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                         client_address_, false, "");
  }

  // Deliver packets in reversed order.
  for (QuicConnectionId connection_id = num_connections; connection_id > 0;
       --connection_id) {
    std::list<BufferedPacket> queue =
        store_.DeliverPackets(connection_id).buffered_packets;
    ASSERT_EQ(2u, queue.size());
  }
}

TEST_F(QuicBufferedPacketStoreTest,
       FailToBufferTooManyPacketsOnExistingConnection) {
  // Tests that for one connection, only limited number of packets can be
  // buffered.
  size_t num_packets = kDefaultMaxUndecryptablePackets + 1;
  QuicConnectionId connection_id = 1;
  // Arrived CHLO packet shouldn't affect how many non-CHLO pacekts store can
  // keep.
  EXPECT_EQ(QuicBufferedPacketStore::SUCCESS,
            store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                                 client_address_, true, ""));
  for (size_t i = 1; i <= num_packets; ++i) {
    // Only first |kDefaultMaxUndecryptablePackets packets| will be buffered.
    EnqueuePacketResult result =
        store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                             client_address_, false, "");
    if (i <= kDefaultMaxUndecryptablePackets) {
      EXPECT_EQ(EnqueuePacketResult::SUCCESS, result);
    } else {
      EXPECT_EQ(EnqueuePacketResult::TOO_MANY_PACKETS, result);
    }
  }

  // Only first |kDefaultMaxUndecryptablePackets| non-CHLO packets and CHLO are
  // buffered.
  EXPECT_EQ(kDefaultMaxUndecryptablePackets + 1,
            store_.DeliverPackets(connection_id).buffered_packets.size());
}

TEST_F(QuicBufferedPacketStoreTest, ReachNonChloConnectionUpperLimit) {
  // Tests that store can only keep early arrived packets for limited number of
  // connections.
  const size_t kNumConnections = kMaxConnectionsWithoutCHLO + 1;
  for (size_t connection_id = 1; connection_id <= kNumConnections;
       ++connection_id) {
    EnqueuePacketResult result =
        store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                             client_address_, false, "");
    if (connection_id <= kMaxConnectionsWithoutCHLO) {
      EXPECT_EQ(EnqueuePacketResult::SUCCESS, result);
    } else {
      EXPECT_EQ(EnqueuePacketResult::TOO_MANY_CONNECTIONS, result);
    }
  }
  // Store only keeps early arrived packets upto |kNumConnections| connections.
  for (size_t connection_id = 1; connection_id <= kNumConnections;
       ++connection_id) {
    std::list<BufferedPacket> queue =
        store_.DeliverPackets(connection_id).buffered_packets;
    if (connection_id <= kMaxConnectionsWithoutCHLO) {
      EXPECT_EQ(1u, queue.size());
    } else {
      EXPECT_EQ(0u, queue.size());
    }
  }
}

TEST_F(QuicBufferedPacketStoreTest,
       FullStoreFailToBufferDataPacketOnNewConnection) {
  // Send enough CHLOs so that store gets full before number of connections
  // without CHLO reaches its upper limit.
  size_t num_chlos =
      kDefaultMaxConnectionsInStore - kMaxConnectionsWithoutCHLO + 1;
  for (size_t connection_id = 1; connection_id <= num_chlos; ++connection_id) {
    EXPECT_EQ(EnqueuePacketResult::SUCCESS,
              store_.EnqueuePacket(connection_id, false, packet_,
                                   server_address_, client_address_, true, ""));
  }

  // Send data packets on another |kMaxConnectionsWithoutCHLO| connections.
  // Store should only be able to buffer till it's full.
  for (size_t conn_id = num_chlos + 1;
       conn_id <= (kDefaultMaxConnectionsInStore + 1); ++conn_id) {
    EnqueuePacketResult result = store_.EnqueuePacket(
        conn_id, false, packet_, server_address_, client_address_, true, "");
    if (conn_id <= kDefaultMaxConnectionsInStore) {
      EXPECT_EQ(EnqueuePacketResult::SUCCESS, result);
    } else {
      EXPECT_EQ(EnqueuePacketResult::TOO_MANY_CONNECTIONS, result);
    }
  }
}

TEST_F(QuicBufferedPacketStoreTest, EnqueueChloOnTooManyDifferentConnections) {
  // Buffer data packets on different connections upto limit.
  for (QuicConnectionId conn_id = 1; conn_id <= kMaxConnectionsWithoutCHLO;
       ++conn_id) {
    EXPECT_EQ(EnqueuePacketResult::SUCCESS,
              store_.EnqueuePacket(conn_id, false, packet_, server_address_,
                                   client_address_, false, ""));
  }

  // Buffer CHLOs on other connections till store is full.
  for (size_t i = kMaxConnectionsWithoutCHLO + 1;
       i <= kDefaultMaxConnectionsInStore + 1; ++i) {
    EnqueuePacketResult rs = store_.EnqueuePacket(
        /*connection_id=*/i, false, packet_, server_address_, client_address_,
        true, "");
    if (i <= kDefaultMaxConnectionsInStore) {
      EXPECT_EQ(EnqueuePacketResult::SUCCESS, rs);
      EXPECT_TRUE(store_.HasChloForConnection(/*connection_id=*/i));
    } else {
      // Last CHLO can't be buffered because store is full.
      EXPECT_EQ(EnqueuePacketResult::TOO_MANY_CONNECTIONS, rs);
      EXPECT_FALSE(store_.HasChloForConnection(/*connection_id=*/i));
    }
  }

  // But buffering a CHLO belonging to a connection already has data packet
  // buffered in the store should success. This is the connection should be
  // delivered at last.
  EXPECT_EQ(EnqueuePacketResult::SUCCESS,
            store_.EnqueuePacket(/*connection_id=*/1, false, packet_,
                                 server_address_, client_address_, true, ""));
  EXPECT_TRUE(store_.HasChloForConnection(/*connection_id=*/1));

  QuicConnectionId delivered_conn_id;
  for (size_t i = 0;
       i < kDefaultMaxConnectionsInStore - kMaxConnectionsWithoutCHLO + 1;
       ++i) {
    if (i < kDefaultMaxConnectionsInStore - kMaxConnectionsWithoutCHLO) {
      // Only CHLO is buffered.
      EXPECT_EQ(1u, store_.DeliverPacketsForNextConnection(&delivered_conn_id)
                        .buffered_packets.size());
      EXPECT_EQ(i + kMaxConnectionsWithoutCHLO + 1, delivered_conn_id);
    } else {
      EXPECT_EQ(2u, store_.DeliverPacketsForNextConnection(&delivered_conn_id)
                        .buffered_packets.size());
      EXPECT_EQ(1u, delivered_conn_id);
    }
  }
  EXPECT_FALSE(store_.HasChlosBuffered());
}

// Tests that store expires long-staying connections appropriately for
// connections both with and without CHLOs.
TEST_F(QuicBufferedPacketStoreTest, PacketQueueExpiredBeforeDelivery) {
  QuicConnectionId connection_id = 1;
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  EXPECT_EQ(EnqueuePacketResult::SUCCESS,
            store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                                 client_address_, true, ""));
  QuicConnectionId connection_id2 = 2;
  EXPECT_EQ(EnqueuePacketResult::SUCCESS,
            store_.EnqueuePacket(connection_id2, false, packet_,
                                 server_address_, client_address_, false, ""));

  // CHLO on connection 3 arrives 1ms later.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
  QuicConnectionId connection_id3 = 3;
  // Use different client address to differetiate packets from different
  // connections.
  QuicSocketAddress another_client_address(QuicIpAddress::Any4(), 255);
  store_.EnqueuePacket(connection_id3, false, packet_, server_address_,
                       another_client_address, true, "");

  // Advance clock to the time when connection 1 and 2 expires.
  clock_.AdvanceTime(
      QuicBufferedPacketStorePeer::expiration_alarm(&store_)->deadline() -
      clock_.ApproximateNow());
  ASSERT_GE(clock_.ApproximateNow(),
            QuicBufferedPacketStorePeer::expiration_alarm(&store_)->deadline());
  // Fire alarm to remove long-staying connection 1 and 2 packets.
  alarm_factory_.FireAlarm(
      QuicBufferedPacketStorePeer::expiration_alarm(&store_));
  EXPECT_EQ(1u, visitor_.last_expired_packet_queue_.buffered_packets.size());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id2));

  // Try to deliver packets, but packet queue has been removed so no
  // packets can be returned.
  ASSERT_EQ(0u, store_.DeliverPackets(connection_id).buffered_packets.size());
  ASSERT_EQ(0u, store_.DeliverPackets(connection_id2).buffered_packets.size());
  QuicConnectionId delivered_conn_id;
  auto queue = store_.DeliverPacketsForNextConnection(&delivered_conn_id)
                   .buffered_packets;
  // Connection 3 is the next to be delivered as connection 1 already expired.
  EXPECT_EQ(connection_id3, delivered_conn_id);
  ASSERT_EQ(1u, queue.size());
  // Packets in connection 3 should use another client address.
  EXPECT_EQ(another_client_address, queue.front().client_address);

  // Test the alarm is reset by enqueueing 2 packets for 4th connection and wait
  // for them to expire.
  QuicConnectionId connection_id4 = 4;
  store_.EnqueuePacket(connection_id4, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id4, false, packet_, server_address_,
                       client_address_, false, "");
  clock_.AdvanceTime(
      QuicBufferedPacketStorePeer::expiration_alarm(&store_)->deadline() -
      clock_.ApproximateNow());
  alarm_factory_.FireAlarm(
      QuicBufferedPacketStorePeer::expiration_alarm(&store_));
  // |last_expired_packet_queue_| should be updated.
  EXPECT_EQ(2u, visitor_.last_expired_packet_queue_.buffered_packets.size());
}

TEST_F(QuicBufferedPacketStoreTest, SimpleDiscardPackets) {
  QuicConnectionId connection_id = 1;

  // Enqueue some packets
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());

  // Dicard the packets
  store_.DiscardPackets(connection_id);

  // No packets on connection 1 should remain in the store
  EXPECT_TRUE(store_.DeliverPackets(connection_id).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());

  // Check idempotency
  store_.DiscardPackets(connection_id);
  EXPECT_TRUE(store_.DeliverPackets(connection_id).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());
}

TEST_F(QuicBufferedPacketStoreTest, DiscardWithCHLOs) {
  QuicConnectionId connection_id = 1;

  // Enqueue some packets, which include a CHLO
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, true, "");
  store_.EnqueuePacket(connection_id, false, packet_, server_address_,
                       client_address_, false, "");
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id));
  EXPECT_TRUE(store_.HasChlosBuffered());

  // Dicard the packets
  store_.DiscardPackets(connection_id);

  // No packets on connection 1 should remain in the store
  EXPECT_TRUE(store_.DeliverPackets(connection_id).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());

  // Check idempotency
  store_.DiscardPackets(connection_id);
  EXPECT_TRUE(store_.DeliverPackets(connection_id).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());
}

TEST_F(QuicBufferedPacketStoreTest, MultipleDiscardPackets) {
  QuicConnectionId connection_id_1 = 1;
  QuicConnectionId connection_id_2 = 2;

  // Enqueue some packets for two connection IDs
  store_.EnqueuePacket(connection_id_1, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id_1, false, packet_, server_address_,
                       client_address_, false, "");
  store_.EnqueuePacket(connection_id_2, false, packet_, server_address_,
                       client_address_, true, "h3");
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id_1));
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id_2));
  EXPECT_TRUE(store_.HasChlosBuffered());

  // Discard the packets for connection 1
  store_.DiscardPackets(connection_id_1);

  // No packets on connection 1 should remain in the store
  EXPECT_TRUE(store_.DeliverPackets(connection_id_1).buffered_packets.empty());
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id_1));
  EXPECT_TRUE(store_.HasChlosBuffered());

  // Packets on connection 2 should remain
  EXPECT_TRUE(store_.HasBufferedPackets(connection_id_2));
  auto packets = store_.DeliverPackets(connection_id_2);
  EXPECT_EQ(1u, packets.buffered_packets.size());
  EXPECT_EQ("h3", packets.alpn);
  EXPECT_TRUE(store_.HasChlosBuffered());

  // Discard the packets for connection 2
  store_.DiscardPackets(connection_id_2);
  EXPECT_FALSE(store_.HasChlosBuffered());
}

TEST_F(QuicBufferedPacketStoreTest, DiscardPacketsEmpty) {
  // Check that DiscardPackets on an unknown connection ID is safe and does
  // nothing.
  QuicConnectionId connection_id = 11235;
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());
  store_.DiscardPackets(connection_id);
  EXPECT_FALSE(store_.HasBufferedPackets(connection_id));
  EXPECT_FALSE(store_.HasChlosBuffered());
}

}  // namespace
}  // namespace test
}  // namespace quic
