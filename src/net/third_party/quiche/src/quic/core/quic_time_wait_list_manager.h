// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Handles packets for connection_ids in time wait state by discarding the
// packet and sending the peers termination packets with exponential backoff.

#ifndef QUICHE_QUIC_CORE_QUIC_TIME_WAIT_LIST_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_TIME_WAIT_LIST_MANAGER_H_

#include <cstddef>
#include <memory>

#include "net/third_party/quiche/src/quic/core/quic_blocked_writer_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

namespace test {
class QuicDispatcherPeer;
class QuicTimeWaitListManagerPeer;
}  // namespace test

// Maintains a list of all connection_ids that have been recently closed. A
// connection_id lives in this state for time_wait_period_. All packets received
// for connection_ids in this state are handed over to the
// QuicTimeWaitListManager by the QuicDispatcher.  Decides whether to send a
// public reset packet, a copy of the previously sent connection close packet,
// or nothing to the peer which sent a packet with the connection_id in time
// wait state.  After the connection_id expires its time wait period, a new
// connection/session will be created if a packet is received for this
// connection_id.
class QUIC_NO_EXPORT QuicTimeWaitListManager
    : public QuicBlockedWriterInterface {
 public:
  // Specifies what the time wait list manager should do when processing packets
  // of a time wait connection.
  enum TimeWaitAction : uint8_t {
    // Send specified termination packets, error if termination packet is
    // unavailable.
    SEND_TERMINATION_PACKETS,
    // Send stateless reset (public reset for GQUIC).
    SEND_STATELESS_RESET,

    DO_NOTHING,
  };

  class QUIC_NO_EXPORT Visitor : public QuicSession::Visitor {
   public:
    // Called after the given connection is added to the time-wait list.
    virtual void OnConnectionAddedToTimeWaitList(
        QuicConnectionId connection_id) = 0;
  };

  // writer - the entity that writes to the socket. (Owned by the caller)
  // visitor - the entity that manages blocked writers. (Owned by the caller)
  // clock - provide a clock (Owned by the caller)
  // alarm_factory - used to run clean up alarms. (Owned by the caller)
  QuicTimeWaitListManager(QuicPacketWriter* writer,
                          Visitor* visitor,
                          const QuicClock* clock,
                          QuicAlarmFactory* alarm_factory);
  QuicTimeWaitListManager(const QuicTimeWaitListManager&) = delete;
  QuicTimeWaitListManager& operator=(const QuicTimeWaitListManager&) = delete;
  ~QuicTimeWaitListManager() override;

  // Adds the given connection_id to time wait state for time_wait_period_.
  // If |termination_packets| are provided, copies of these packets will be sent
  // when a packet with this connection ID is processed. Any termination packets
  // will be move from |termination_packets| and will become owned by the
  // manager. |action| specifies what the time wait list manager should do when
  // processing packets of the connection.
  virtual void AddConnectionIdToTimeWait(
      QuicConnectionId connection_id,
      bool ietf_quic,
      TimeWaitAction action,
      EncryptionLevel encryption_level,
      std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets);

  // Returns true if the connection_id is in time wait state, false otherwise.
  // Packets received for this connection_id should not lead to creation of new
  // QuicSessions.
  bool IsConnectionIdInTimeWait(QuicConnectionId connection_id) const;

  // Called when a packet is received for a connection_id that is in time wait
  // state. Sends a public reset packet to the peer which sent this
  // connection_id. Sending of the public reset packet is throttled by using
  // exponential back off. DCHECKs for the connection_id to be in time wait
  // state. virtual to override in tests.
  virtual void ProcessPacket(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      QuicConnectionId connection_id,
      PacketHeaderFormat header_format,
      std::unique_ptr<QuicPerPacketContext> packet_context);

  // Called by the dispatcher when the underlying socket becomes writable again,
  // since we might need to send pending public reset packets which we didn't
  // send because the underlying socket was write blocked.
  void OnBlockedWriterCanWrite() override;

  bool IsWriterBlocked() const override {
    return writer_ != nullptr && writer_->IsWriteBlocked();
  }

  // Used to delete connection_id entries that have outlived their time wait
  // period.
  void CleanUpOldConnectionIds();

  // If necessary, trims the oldest connections from the time-wait list until
  // the size is under the configured maximum.
  void TrimTimeWaitListIfNeeded();

  // The number of connections on the time-wait list.
  size_t num_connections() const { return connection_id_map_.size(); }

  // Sends a version negotiation packet for |server_connection_id| and
  // |client_connection_id| announcing support for |supported_versions| to
  // |peer_address| from |self_address|.
  virtual void SendVersionNegotiationPacket(
      QuicConnectionId server_connection_id,
      QuicConnectionId client_connection_id,
      bool ietf_quic,
      bool use_length_prefix,
      const ParsedQuicVersionVector& supported_versions,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      std::unique_ptr<QuicPerPacketContext> packet_context);

  // Creates a public reset packet and sends it or queues it to be sent later.
  virtual void SendPublicReset(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      QuicConnectionId connection_id,
      bool ietf_quic,
      std::unique_ptr<QuicPerPacketContext> packet_context);

  // Called to send |packet|.
  virtual void SendPacket(const QuicSocketAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          const QuicEncryptedPacket& packet);

  // Return a non-owning pointer to the packet writer.
  QuicPacketWriter* writer() { return writer_; }

 protected:
  virtual std::unique_ptr<QuicEncryptedPacket> BuildPublicReset(
      const QuicPublicResetPacket& packet);

  virtual void GetEndpointId(std::string* /*endpoint_id*/) {}

  // Returns a stateless reset token which will be included in the public reset
  // packet.
  virtual QuicUint128 GetStatelessResetToken(
      QuicConnectionId connection_id) const;

  // Internal structure to store pending termination packets.
  class QUIC_NO_EXPORT QueuedPacket {
   public:
    QueuedPacket(const QuicSocketAddress& self_address,
                 const QuicSocketAddress& peer_address,
                 std::unique_ptr<QuicEncryptedPacket> packet)
        : self_address_(self_address),
          peer_address_(peer_address),
          packet_(std::move(packet)) {}
    QueuedPacket(const QueuedPacket&) = delete;
    QueuedPacket& operator=(const QueuedPacket&) = delete;

    const QuicSocketAddress& self_address() const { return self_address_; }
    const QuicSocketAddress& peer_address() const { return peer_address_; }
    QuicEncryptedPacket* packet() { return packet_.get(); }

   private:
    // Server address on which a packet was received for a connection_id in
    // time wait state.
    const QuicSocketAddress self_address_;
    // Address of the peer to send this packet to.
    const QuicSocketAddress peer_address_;
    // The pending termination packet that is to be sent to the peer.
    std::unique_ptr<QuicEncryptedPacket> packet_;
  };

  // Called right after |packet| is serialized. Either sends the packet and
  // deletes it or makes pending_packets_queue_ the owner of the packet.
  // Subclasses overriding this method should call this class's base
  // implementation at the end of the override.
  // Return true if |packet| is sent, false if it is queued.
  virtual bool SendOrQueuePacket(std::unique_ptr<QueuedPacket> packet,
                                 const QuicPerPacketContext* packet_context);

  const QuicCircularDeque<std::unique_ptr<QueuedPacket>>&
  pending_packets_queue() const {
    return pending_packets_queue_;
  }

 private:
  friend class test::QuicDispatcherPeer;
  friend class test::QuicTimeWaitListManagerPeer;

  // Decides if a packet should be sent for this connection_id based on the
  // number of received packets.
  bool ShouldSendResponse(int received_packet_count);

  // Sends the packet out. Returns true if the packet was successfully consumed.
  // If the writer got blocked and did not buffer the packet, we'll need to keep
  // the packet and retry sending. In case of all other errors we drop the
  // packet.
  bool WriteToWire(QueuedPacket* packet);

  // Register the alarm server to wake up at appropriate time.
  void SetConnectionIdCleanUpAlarm();

  // Removes the oldest connection from the time-wait list if it was added prior
  // to "expiration_time".  To unconditionally remove the oldest connection, use
  // a QuicTime::Delta:Infinity().  This function modifies the
  // connection_id_map_.  If you plan to call this function in a loop, any
  // iterators that you hold before the call to this function may be invalid
  // afterward.  Returns true if the oldest connection was expired.  Returns
  // false if the map is empty or the oldest connection has not expired.
  bool MaybeExpireOldestConnection(QuicTime expiration_time);

  std::unique_ptr<QuicEncryptedPacket> BuildIetfStatelessResetPacket(
      QuicConnectionId connection_id);

  // A map from a recently closed connection_id to the number of packets
  // received after the termination of the connection bound to the
  // connection_id.
  struct QUIC_NO_EXPORT ConnectionIdData {
    ConnectionIdData(int num_packets,
                     bool ietf_quic,
                     QuicTime time_added,
                     TimeWaitAction action);

    ConnectionIdData(const ConnectionIdData& other) = delete;
    ConnectionIdData(ConnectionIdData&& other);

    ~ConnectionIdData();

    int num_packets;
    bool ietf_quic;
    QuicTime time_added;
    // Encryption level of termination_packets.
    EncryptionLevel encryption_level;
    // These packets may contain CONNECTION_CLOSE frames, or SREJ messages.
    std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
    TimeWaitAction action;
  };

  // QuicLinkedHashMap allows lookup by ConnectionId and traversal in add order.
  typedef QuicLinkedHashMap<QuicConnectionId,
                            ConnectionIdData,
                            QuicConnectionIdHash>
      ConnectionIdMap;
  ConnectionIdMap connection_id_map_;

  // Pending termination packets that need to be sent out to the peer when we
  // are given a chance to write by the dispatcher.
  QuicCircularDeque<std::unique_ptr<QueuedPacket>> pending_packets_queue_;

  // Time period for which connection_ids should remain in time wait state.
  const QuicTime::Delta time_wait_period_;

  // Alarm to clean up connection_ids that have out lived their duration in
  // time wait state.
  std::unique_ptr<QuicAlarm> connection_id_clean_up_alarm_;

  // Clock to efficiently measure approximate time.
  const QuicClock* clock_;

  // Interface that writes given buffer to the socket.
  QuicPacketWriter* writer_;

  // Interface that manages blocked writers.
  Visitor* visitor_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TIME_WAIT_LIST_MANAGER_H_
