// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Handles packets for connection_ids in time wait state by discarding the
// packet and sending the peers termination packets with exponential backoff.

#ifndef QUICHE_QUIC_CORE_QUIC_TIME_WAIT_LIST_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_TIME_WAIT_LIST_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_blocked_writer_interface.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_reference_counted.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

namespace test {
class QuicDispatcherPeer;
class QuicTimeWaitListManagerPeer;
}  // namespace test

// TimeWaitConnectionInfo comprises information of a connection which is in the
// time wait list.
struct QUICHE_EXPORT TimeWaitConnectionInfo {
  TimeWaitConnectionInfo(
      bool ietf_quic,
      std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets,
      std::vector<QuicConnectionId> active_connection_ids);
  TimeWaitConnectionInfo(
      bool ietf_quic,
      std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets,
      std::vector<QuicConnectionId> active_connection_ids,
      QuicTime::Delta srtt);

  TimeWaitConnectionInfo(const TimeWaitConnectionInfo& other) = delete;
  TimeWaitConnectionInfo(TimeWaitConnectionInfo&& other) = default;

  ~TimeWaitConnectionInfo() = default;

  bool ietf_quic;
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  std::vector<QuicConnectionId> active_connection_ids;
  QuicTime::Delta srtt;
};

// Maintains a list of all connection_ids that have been recently closed. A
// connection_id lives in this state for time_wait_period_. All packets received
// for connection_ids in this state are handed over to the
// QuicTimeWaitListManager by the QuicDispatcher.  Decides whether to send a
// public reset packet, a copy of the previously sent connection close packet,
// or nothing to the peer which sent a packet with the connection_id in time
// wait state.  After the connection_id expires its time wait period, a new
// connection/session will be created if a packet is received for this
// connection_id.
class QUICHE_EXPORT QuicTimeWaitListManager
    : public QuicBlockedWriterInterface {
 public:
  // Specifies what the time wait list manager should do when processing packets
  // of a time wait connection.
  enum TimeWaitAction : uint8_t {
    // Send specified termination packets, error if termination packet is
    // unavailable.
    SEND_TERMINATION_PACKETS,
    // The same as SEND_TERMINATION_PACKETS except that the corresponding
    // termination packets are provided by the connection.
    SEND_CONNECTION_CLOSE_PACKETS,
    // Send stateless reset (public reset for GQUIC).
    SEND_STATELESS_RESET,

    DO_NOTHING,
  };

  class QUICHE_EXPORT Visitor : public QuicSession::Visitor {
   public:
    void OnPathDegrading() override {}
  };

  // writer - the entity that writes to the socket. (Owned by the caller)
  // visitor - the entity that manages blocked writers. (Owned by the caller)
  // clock - provide a clock (Owned by the caller)
  // alarm_factory - used to run clean up alarms. (Owned by the caller)
  QuicTimeWaitListManager(QuicPacketWriter* writer, Visitor* visitor,
                          const QuicClock* clock,
                          QuicAlarmFactory* alarm_factory);
  QuicTimeWaitListManager(const QuicTimeWaitListManager&) = delete;
  QuicTimeWaitListManager& operator=(const QuicTimeWaitListManager&) = delete;
  ~QuicTimeWaitListManager() override;

  // Adds the connection IDs in info to time wait state for time_wait_period_.
  // If |info|.termination_packets are provided, copies of these packets will be
  // sent when a packet with one of these connection IDs is processed. Any
  // termination packets will be move from |info|.termination_packets and will
  // become owned by the manager. |action| specifies what the time wait list
  // manager should do when processing packets of the connection.
  virtual void AddConnectionIdToTimeWait(TimeWaitAction action,
                                         TimeWaitConnectionInfo info);

  // Returns true if the connection_id is in time wait state, false otherwise.
  // Packets received for this connection_id should not lead to creation of new
  // QuicSessions.
  bool IsConnectionIdInTimeWait(QuicConnectionId connection_id) const;

  // Called when a packet is received for a connection_id that is in time wait
  // state. Sends a public reset packet to the peer which sent this
  // connection_id. Sending of the public reset packet is throttled by using
  // exponential back off. QUICHE_DCHECKs for the connection_id to be in time
  // wait state. virtual to override in tests.
  // TODO(fayang): change ProcessPacket and SendPublicReset to take
  // ReceivedPacketInfo.
  virtual void ProcessPacket(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
      PacketHeaderFormat header_format, size_t received_packet_length,
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
  size_t num_connections() const {
    return use_old_connection_id_map_ ? connection_id_map_.size()
                                      : num_connections_;
  }
  bool has_connections() const;

  // Sends a version negotiation packet for |server_connection_id| and
  // |client_connection_id| announcing support for |supported_versions| to
  // |peer_address| from |self_address|.
  virtual void SendVersionNegotiationPacket(
      QuicConnectionId server_connection_id,
      QuicConnectionId client_connection_id, bool ietf_quic,
      bool use_length_prefix, const ParsedQuicVersionVector& supported_versions,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      std::unique_ptr<QuicPerPacketContext> packet_context);

  // Creates a public reset packet and sends it or queues it to be sent later.
  virtual void SendPublicReset(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
      bool ietf_quic, size_t received_packet_length,
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
  virtual StatelessResetToken GetStatelessResetToken(
      QuicConnectionId connection_id) const;

  // Internal structure to store pending termination packets.
  class QUICHE_EXPORT QueuedPacket {
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

  const quiche::QuicheCircularDeque<std::unique_ptr<QueuedPacket>>&
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

  // Called when a packet is received for a connection in this time wait list.
  virtual void OnPacketReceivedForKnownConnection(
      int /*num_packets*/, QuicTime::Delta /*delta*/,
      QuicTime::Delta /*srtt*/) const {}

  std::unique_ptr<QuicEncryptedPacket> BuildIetfStatelessResetPacket(
      QuicConnectionId connection_id, size_t received_packet_length);

  // A map from a recently closed connection_id to the number of packets
  // received after the termination of the connection bound to the
  // connection_id.
  struct QUICHE_EXPORT ConnectionIdData {
    ConnectionIdData(int num_packets, QuicTime time_added,
                     TimeWaitAction action, TimeWaitConnectionInfo info);

    ConnectionIdData(const ConnectionIdData& other) = delete;
    ConnectionIdData(ConnectionIdData&& other);

    ~ConnectionIdData();

    int num_packets;
    QuicTime time_added;
    TimeWaitAction action;
    TimeWaitConnectionInfo info;
  };

  // TODO(haoyuewang): Merge RefCountedConnectionIdData & ConnectionIdData once
  // there is no need for ConnectionIdData to be movable. Also removes the
  // underscore in the constructor parameter names.
  struct RefCountedConnectionIdData : public ConnectionIdData,
                                      public quiche::QuicheReferenceCounted {
    RefCountedConnectionIdData(int _num_packets, QuicTime _time_added,
                               TimeWaitAction _action,
                               TimeWaitConnectionInfo _info,
                               size_t& _num_connections)
        : ConnectionIdData(_num_packets, _time_added, _action,
                           std::move(_info)),
          num_connections(_num_connections) {
      ++num_connections;
    }

    ~RefCountedConnectionIdData() {
      QUIC_BUG_IF(bad_num_connections, num_connections == 0);
      --num_connections;
    }

   private:
    // Reference to the total number of connections counter in the time-wait
    // list.
    size_t& num_connections;
  };

  // Returns the time when the first connection was added to the time-wait list.
  QuicTime GetOldestConnectionTime() const;

  const bool use_old_connection_id_map_ =
      !GetQuicRestartFlag(quic_use_one_map_in_time_wait_list);
  size_t num_connections_ = 0;

  // QuicheLinkedHashMap allows lookup by ConnectionId
  // and traversal in add order.
  quiche::QuicheLinkedHashMap<
      QuicConnectionId,
      quiche::QuicheReferenceCountedPointer<RefCountedConnectionIdData>,
      QuicConnectionIdHash>
      connection_id_data_map_;

  using ConnectionIdMap =
      quiche::QuicheLinkedHashMap<QuicConnectionId, ConnectionIdData,
                                  QuicConnectionIdHash>;
  // Do not use find/emplace/erase on this map directly. Use
  // FindConnectionIdDataInMap, AddConnectionIdDateToMap,
  // RemoveConnectionDataFromMap instead.
  ConnectionIdMap connection_id_map_;

  // A connection can have multiple unretired ConnectionIds when it is closed.
  // These Ids have the same ConnectionIdData entry in connection_id_map_. To
  // find the entry, look up the cannoical ConnectionId in
  // indirect_connection_id_map_ first, and look up connection_id_map_ with the
  // cannoical ConnectionId.
  absl::flat_hash_map<QuicConnectionId, QuicConnectionId, QuicConnectionIdHash>
      indirect_connection_id_map_;

  // Find data for the given connection_id. Returns nullptr if not found.
  absl::Nullable<ConnectionIdData*> FindConnectionIdData(
      const QuicConnectionId& connection_id);
  // Find an iterator for the given connection_id. Returns
  // connection_id_map_.end() if none found.
  ConnectionIdMap::iterator FindConnectionIdDataInMap(
      const QuicConnectionId& connection_id);
  // Inserts a ConnectionIdData entry to connection_id_map_.
  void AddConnectionIdDataToMap(const QuicConnectionId& canonical_connection_id,
                                int num_packets, TimeWaitAction action,
                                TimeWaitConnectionInfo info);
  // Removes a ConnectionIdData entry in connection_id_map_.
  void RemoveConnectionDataFromMap(ConnectionIdMap::iterator it);

  // Pending termination packets that need to be sent out to the peer when we
  // are given a chance to write by the dispatcher.
  quiche::QuicheCircularDeque<std::unique_ptr<QueuedPacket>>
      pending_packets_queue_;

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
