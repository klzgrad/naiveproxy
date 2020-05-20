// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A server side dispatcher which dispatches a given client's data to their
// stream.

#ifndef QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_
#define QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_blocked_writer_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_buffered_packet_store.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream_base.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_process_packet_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_time_wait_list_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
class QuicDispatcherPeer;
}  // namespace test

class QuicConfig;
class QuicCryptoServerConfig;

class QUIC_NO_EXPORT QuicDispatcher
    : public QuicTimeWaitListManager::Visitor,
      public ProcessPacketInterface,
      public QuicBufferedPacketStore::VisitorInterface {
 public:
  // Ideally we'd have a linked_hash_set: the  boolean is unused.
  typedef QuicLinkedHashMap<QuicBlockedWriterInterface*, bool> WriteBlockedList;

  QuicDispatcher(
      const QuicConfig* config,
      const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      uint8_t expected_server_connection_id_length);
  QuicDispatcher(const QuicDispatcher&) = delete;
  QuicDispatcher& operator=(const QuicDispatcher&) = delete;

  ~QuicDispatcher() override;

  // Takes ownership of |writer|.
  void InitializeWithWriter(QuicPacketWriter* writer);

  // Process the incoming packet by creating a new session, passing it to
  // an existing session, or passing it to the time wait list.
  void ProcessPacket(const QuicSocketAddress& self_address,
                     const QuicSocketAddress& peer_address,
                     const QuicReceivedPacket& packet) override;

  // Called when the socket becomes writable to allow queued writes to happen.
  virtual void OnCanWrite();

  // Returns true if there's anything in the blocked writer list.
  virtual bool HasPendingWrites() const;

  // Sends ConnectionClose frames to all connected clients.
  void Shutdown();

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Ensure that the closed connection is cleaned up asynchronously.
  void OnConnectionClosed(QuicConnectionId server_connection_id,
                          QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource source) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Queues the blocked writer for later resumption.
  void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Collects reset error code received on streams.
  void OnRstStreamReceived(const QuicRstStreamFrame& frame) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Collects reset error code received on streams.
  void OnStopSendingReceived(const QuicStopSendingFrame& frame) override;

  // QuicTimeWaitListManager::Visitor interface implementation
  // Called whenever the time wait list manager adds a new connection to the
  // time-wait list.
  void OnConnectionAddedToTimeWaitList(
      QuicConnectionId server_connection_id) override;

  using SessionMap = QuicUnorderedMap<QuicConnectionId,
                                      std::unique_ptr<QuicSession>,
                                      QuicConnectionIdHash>;

  const SessionMap& session_map() const { return session_map_; }

  // Deletes all sessions on the closed session list and clears the list.
  virtual void DeleteSessions();

  using ConnectionIdMap = QuicUnorderedMap<QuicConnectionId,
                                           QuicConnectionId,
                                           QuicConnectionIdHash>;

  // The largest packet number we expect to receive with a connection
  // ID for a connection that is not established yet.  The current design will
  // send a handshake and then up to 50 or so data packets, and then it may
  // resend the handshake packet up to 10 times.  (Retransmitted packets are
  // sent with unique packet numbers.)
  static const uint64_t kMaxReasonableInitialPacketNumber = 100;
  static_assert(kMaxReasonableInitialPacketNumber >=
                    kInitialCongestionWindow + 10,
                "kMaxReasonableInitialPacketNumber is unreasonably small "
                "relative to kInitialCongestionWindow.");


  // QuicBufferedPacketStore::VisitorInterface implementation.
  void OnExpiredPackets(QuicConnectionId server_connection_id,
                        QuicBufferedPacketStore::BufferedPacketList
                            early_arrived_packets) override;

  // Create connections for previously buffered CHLOs as many as allowed.
  virtual void ProcessBufferedChlos(size_t max_connections_to_create);

  // Return true if there is CHLO buffered.
  virtual bool HasChlosBuffered() const;

  // Start accepting new ConnectionIds.
  void StartAcceptingNewConnections();

  // Stop accepting new ConnectionIds, either as a part of the lame
  // duck process or because explicitly configured.
  void StopAcceptingNewConnections();

  bool accept_new_connections() const { return accept_new_connections_; }

 protected:
  virtual std::unique_ptr<QuicSession> CreateQuicSession(
      QuicConnectionId server_connection_id,
      const QuicSocketAddress& peer_address,
      quiche::QuicheStringPiece alpn,
      const ParsedQuicVersion& version) = 0;

  // Tries to validate and dispatch packet based on available information.
  // Returns true if packet is dropped or successfully dispatched (e.g.,
  // processed by existing session, processed by time wait list, etc.),
  // otherwise, returns false and the packet needs further processing.
  virtual bool MaybeDispatchPacket(const ReceivedPacketInfo& packet_info);

  // Generate a connection ID with a length that is expected by the dispatcher.
  // Note that this MUST produce a deterministic result (calling this method
  // with two connection IDs that are equal must produce the same result).
  virtual QuicConnectionId GenerateNewServerConnectionId(
      ParsedQuicVersion version,
      QuicConnectionId connection_id) const;

  // Values to be returned by ValidityChecks() to indicate what should be done
  // with a packet. Fates with greater values are considered to be higher
  // priority. ValidityChecks should return fate based on the priority order
  // (i.e., returns higher priority fate first)
  enum QuicPacketFate {
    // Process the packet normally, which is usually to establish a connection.
    kFateProcess,
    // Put the connection ID into time-wait state and send a public reset.
    kFateTimeWait,
    // Drop the packet.
    kFateDrop,
  };

  // This method is called by ProcessHeader on packets not associated with a
  // known connection ID.  It applies validity checks and returns a
  // QuicPacketFate to tell what should be done with the packet.
  // TODO(fayang): Merge ValidityChecks into MaybeDispatchPacket.
  virtual QuicPacketFate ValidityChecks(const ReceivedPacketInfo& packet_info);

  // Create and return the time wait list manager for this dispatcher, which
  // will be owned by the dispatcher as time_wait_list_manager_
  virtual QuicTimeWaitListManager* CreateQuicTimeWaitListManager();

  // Buffers packet until it can be delivered to a connection.
  void BufferEarlyPacket(const ReceivedPacketInfo& packet_info);

  // Called when |packet_info| is a CHLO packet. Creates a new connection and
  // delivers any buffered packets for that connection id.
  void ProcessChlo(const std::string& alpn, ReceivedPacketInfo* packet_info);

  // Return true if dispatcher wants to destroy session outside of
  // OnConnectionClosed() call stack.
  virtual bool ShouldDestroySessionAsynchronously();

  QuicTimeWaitListManager* time_wait_list_manager() {
    return time_wait_list_manager_.get();
  }

  const QuicTransportVersionVector& GetSupportedTransportVersions();

  const ParsedQuicVersionVector& GetSupportedVersions();

  const QuicConfig& config() const { return *config_; }

  const QuicCryptoServerConfig* crypto_config() const { return crypto_config_; }

  QuicCompressedCertsCache* compressed_certs_cache() {
    return &compressed_certs_cache_;
  }

  QuicConnectionHelperInterface* helper() { return helper_.get(); }

  QuicCryptoServerStreamBase::Helper* session_helper() {
    return session_helper_.get();
  }

  QuicAlarmFactory* alarm_factory() { return alarm_factory_.get(); }

  QuicPacketWriter* writer() { return writer_.get(); }

  // Returns true if a session should be created for a connection with an
  // unknown version identified by |version_label|.
  virtual bool ShouldCreateSessionForUnknownVersion(
      QuicVersionLabel version_label);

  void SetLastError(QuicErrorCode error);

  // Called by MaybeDispatchPacket when current packet cannot be dispatched.
  // Used by subclasses to conduct specific logic to dispatch packet. Returns
  // true if packet is successfully dispatched.
  virtual bool OnFailedToDispatchPacket(const ReceivedPacketInfo& packet_info);

  // Called when a new connection starts to be handled by this dispatcher.
  // Either this connection is created or its packets is buffered while waiting
  // for CHLO. Returns true if a new connection should be created or its packets
  // should be buffered, false otherwise.
  virtual bool ShouldCreateOrBufferPacketForConnection(
      const ReceivedPacketInfo& packet_info);

  bool HasBufferedPackets(QuicConnectionId server_connection_id);

  // Called when BufferEarlyPacket() fail to buffer the packet.
  virtual void OnBufferPacketFailure(
      QuicBufferedPacketStore::EnqueuePacketResult result,
      QuicConnectionId server_connection_id);

  // Removes the session from the session map and write blocked list, and adds
  // the ConnectionId to the time-wait list.
  virtual void CleanUpSession(SessionMap::iterator it,
                              QuicConnection* connection,
                              ConnectionCloseSource source);

  // Called to terminate a connection statelessly. Depending on |format|, either
  // 1) send connection close with |error_code| and |error_details| and add
  // connection to time wait list or 2) directly add connection to time wait
  // list with |action|.
  void StatelesslyTerminateConnection(
      QuicConnectionId server_connection_id,
      PacketHeaderFormat format,
      bool version_flag,
      bool use_length_prefix,
      ParsedQuicVersion version,
      QuicErrorCode error_code,
      const std::string& error_details,
      QuicTimeWaitListManager::TimeWaitAction action);

  // Save/Restore per packet context.
  virtual std::unique_ptr<QuicPerPacketContext> GetPerPacketContext() const;
  virtual void RestorePerPacketContext(
      std::unique_ptr<QuicPerPacketContext> /*context*/) {}

  // If true, our framer will change its expected connection ID length
  // to the received destination connection ID length of all IETF long headers.
  void SetShouldUpdateExpectedServerConnectionIdLength(
      bool should_update_expected_server_connection_id_length) {
    should_update_expected_server_connection_id_length_ =
        should_update_expected_server_connection_id_length;
  }

  // If true, the dispatcher will allow incoming initial packets that have
  // destination connection IDs shorter than 64 bits.
  void SetAllowShortInitialServerConnectionIds(
      bool allow_short_initial_server_connection_ids) {
    allow_short_initial_server_connection_ids_ =
        allow_short_initial_server_connection_ids;
  }

  // Called if a packet from an unseen connection is reset or rejected.
  virtual void OnNewConnectionRejected() {}

 private:
  friend class test::QuicDispatcherPeer;

  typedef QuicUnorderedSet<QuicConnectionId, QuicConnectionIdHash>
      QuicConnectionIdSet;

  // TODO(fayang): Consider to rename this function to
  // ProcessValidatedPacketWithUnknownConnectionId.
  void ProcessHeader(ReceivedPacketInfo* packet_info);

  // Deliver |packets| to |session| for further processing.
  void DeliverPacketsToSession(
      const std::list<QuicBufferedPacketStore::BufferedPacket>& packets,
      QuicSession* session);

  // If the connection ID length is different from what the dispatcher expects,
  // replace the connection ID with a random one of the right length,
  // and save it to make sure the mapping is persistent.
  QuicConnectionId MaybeReplaceServerConnectionId(
      QuicConnectionId server_connection_id,
      ParsedQuicVersion version);

  // Returns true if |version| is a supported protocol version.
  bool IsSupportedVersion(const ParsedQuicVersion version);

  // Sends public/stateless reset packets with no version and unknown
  // connection ID according to the packet's size.
  void MaybeResetPacketsWithNoVersion(const ReceivedPacketInfo& packet_info);

  const QuicConfig* config_;

  const QuicCryptoServerConfig* crypto_config_;

  // The cache for most recently compressed certs.
  QuicCompressedCertsCache compressed_certs_cache_;

  // The list of connections waiting to write.
  WriteBlockedList write_blocked_list_;

  SessionMap session_map_;

  // Entity that manages connection_ids in time wait state.
  std::unique_ptr<QuicTimeWaitListManager> time_wait_list_manager_;

  // The list of closed but not-yet-deleted sessions.
  std::vector<std::unique_ptr<QuicSession>> closed_session_list_;

  // The helper used for all connections.
  std::unique_ptr<QuicConnectionHelperInterface> helper_;

  // The helper used for all sessions.
  std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper_;

  // Creates alarms.
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;

  // An alarm which deletes closed sessions.
  std::unique_ptr<QuicAlarm> delete_sessions_alarm_;

  // The writer to write to the socket with.
  std::unique_ptr<QuicPacketWriter> writer_;

  // Packets which are buffered until a connection can be created to handle
  // them.
  QuicBufferedPacketStore buffered_packets_;

  // Used to get the supported versions based on flag. Does not own.
  QuicVersionManager* version_manager_;

  // The last error set by SetLastError().
  // TODO(fayang): consider removing last_error_.
  QuicErrorCode last_error_;

  // A backward counter of how many new sessions can be create within current
  // event loop. When reaches 0, it means can't create sessions for now.
  int16_t new_sessions_allowed_per_event_loop_;

  // True if this dispatcher is accepting new ConnectionIds (new client
  // connections), false otherwise.
  bool accept_new_connections_;

  // If false, the dispatcher follows the IETF spec and rejects packets with
  // invalid destination connection IDs lengths below 64 bits.
  // If true they are allowed.
  bool allow_short_initial_server_connection_ids_;

  // IETF short headers contain a destination connection ID but do not
  // encode its length. This variable contains the length we expect to read.
  // This is also used to signal an error when a long header packet with
  // different destination connection ID length is received when
  // should_update_expected_server_connection_id_length_ is false and packet's
  // version does not allow variable length connection ID.
  uint8_t expected_server_connection_id_length_;

  // If true, change expected_server_connection_id_length_ to be the received
  // destination connection ID length of all IETF long headers.
  bool should_update_expected_server_connection_id_length_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_
