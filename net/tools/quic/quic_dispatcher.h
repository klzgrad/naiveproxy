// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A server side dispatcher which dispatches a given client's data to their
// stream.

#ifndef NET_TOOLS_QUIC_QUIC_DISPATCHER_H_
#define NET_TOOLS_QUIC_QUIC_DISPATCHER_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_blocked_writer_interface.h"
#include "net/quic/core/quic_buffered_packet_store.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_version_manager.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_socket_address.h"

#include "net/tools/quic/quic_process_packet_interface.h"
#include "net/tools/quic/quic_time_wait_list_manager.h"
#include "net/tools/quic/stateless_rejector.h"

namespace net {
namespace test {
class QuicDispatcherPeer;
}  // namespace test

class QuicConfig;
class QuicCryptoServerConfig;

class QuicDispatcher : public QuicTimeWaitListManager::Visitor,
                       public ProcessPacketInterface,
                       public QuicFramerVisitorInterface,
                       public QuicBufferedPacketStore::VisitorInterface {
 public:
  // Ideally we'd have a linked_hash_set: the  boolean is unused.
  typedef QuicLinkedHashMap<QuicBlockedWriterInterface*, bool> WriteBlockedList;

  QuicDispatcher(const QuicConfig& config,
                 const QuicCryptoServerConfig* crypto_config,
                 QuicVersionManager* version_manager,
                 std::unique_ptr<QuicConnectionHelperInterface> helper,
                 std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
                 std::unique_ptr<QuicAlarmFactory> alarm_factory);

  ~QuicDispatcher() override;

  // Takes ownership of |writer|.
  void InitializeWithWriter(QuicPacketWriter* writer);

  // Process the incoming packet by creating a new session, passing it to
  // an existing session, or passing it to the time wait list.
  void ProcessPacket(const QuicSocketAddress& server_address,
                     const QuicSocketAddress& client_address,
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
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Queues the blocked writer for later resumption.
  void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Collects reset error code received on streams.
  void OnRstStreamReceived(const QuicRstStreamFrame& frame) override;

  // QuicTimeWaitListManager::Visitor interface implementation
  // Called whenever the time wait list manager adds a new connection to the
  // time-wait list.
  void OnConnectionAddedToTimeWaitList(QuicConnectionId connection_id) override;

  using SessionMap =
      QuicUnorderedMap<QuicConnectionId, std::unique_ptr<QuicSession>>;

  const SessionMap& session_map() const { return session_map_; }

  // Deletes all sessions on the closed session list and clears the list.
  virtual void DeleteSessions();

  // The largest packet number we expect to receive with a connection
  // ID for a connection that is not established yet.  The current design will
  // send a handshake and then up to 50 or so data packets, and then it may
  // resend the handshake packet up to 10 times.  (Retransmitted packets are
  // sent with unique packet numbers.)
  static const QuicPacketNumber kMaxReasonableInitialPacketNumber = 100;
  static_assert(kMaxReasonableInitialPacketNumber >=
                    kInitialCongestionWindow + 10,
                "kMaxReasonableInitialPacketNumber is unreasonably small "
                "relative to kInitialCongestionWindow.");

  // QuicFramerVisitorInterface implementation. Not expected to be called
  // outside of this class.
  void OnPacket() override;
  // Called when the public header has been parsed.
  bool OnUnauthenticatedPublicHeader(
      const QuicPacketPublicHeader& header) override;
  // Called when the private header has been parsed of a data packet that is
  // destined for the time wait manager.
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
  void OnError(QuicFramer* framer) override;
  bool OnProtocolVersionMismatch(
      QuicTransportVersion received_version) override;

  // The following methods should never get called because
  // OnUnauthenticatedPublicHeader() or OnUnauthenticatedHeader() (whichever
  // was called last), will return false and prevent a subsequent invocation
  // of these methods. Thus, the payload of the packet is never processed in
  // the dispatcher.
  void OnPublicResetPacket(const QuicPublicResetPacket& packet) override;
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) override;
  void OnDecryptedPacket(EncryptionLevel level) override;
  bool OnPacketHeader(const QuicPacketHeader& header) override;
  bool OnStreamFrame(const QuicStreamFrame& frame) override;
  bool OnAckFrame(const QuicAckFrame& frame) override;
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override;
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override;
  bool OnPingFrame(const QuicPingFrame& frame) override;
  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override;
  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override;
  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override;
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override;
  bool OnBlockedFrame(const QuicBlockedFrame& frame) override;
  void OnPacketComplete() override;

  // QuicBufferedPacketStore::VisitorInterface implementation.
  void OnExpiredPackets(QuicConnectionId connection_id,
                        QuicBufferedPacketStore::BufferedPacketList
                            early_arrived_packets) override;

  // Create connections for previously buffered CHLOs as many as allowed.
  virtual void ProcessBufferedChlos(size_t max_connections_to_create);

  // Return true if there is CHLO buffered.
  virtual bool HasChlosBuffered() const;

 protected:
  virtual QuicSession* CreateQuicSession(
      QuicConnectionId connection_id,
      const QuicSocketAddress& client_address,
      QuicStringPiece alpn) = 0;

  // Called when a connection is rejected statelessly.
  virtual void OnConnectionRejectedStatelessly();

  // Called when a connection is closed statelessly.
  virtual void OnConnectionClosedStatelessly(QuicErrorCode error);

  // Returns true if cheap stateless rejection should be attempted.
  virtual bool ShouldAttemptCheapStatelessRejection();

  // Values to be returned by ValidityChecks() to indicate what should be done
  // with a packet.  Fates with greater values are considered to be higher
  // priority, in that if one validity check indicates a lower-valued fate and
  // another validity check indicates a higher-valued fate, the higher-valued
  // fate should be obeyed.
  enum QuicPacketFate {
    // Process the packet normally, which is usually to establish a connection.
    kFateProcess,
    // Put the connection ID into time-wait state and send a public reset.
    kFateTimeWait,
    // Buffer the packet.
    kFateBuffer,
    // Drop the packet (ignore and give no response).
    kFateDrop,
  };

  // This method is called by OnUnauthenticatedHeader on packets not associated
  // with a known connection ID.  It applies validity checks and returns a
  // QuicPacketFate to tell what should be done with the packet.
  virtual QuicPacketFate ValidityChecks(const QuicPacketHeader& header);

  // Create and return the time wait list manager for this dispatcher, which
  // will be owned by the dispatcher as time_wait_list_manager_
  virtual QuicTimeWaitListManager* CreateQuicTimeWaitListManager();

  // Called when |connection_id| doesn't have an open connection yet, to buffer
  // |current_packet_| until it can be delivered to the connection.
  void BufferEarlyPacket(QuicConnectionId connection_id);

  // Called when |current_packet_| is a CHLO packet. Creates a new connection
  // and delivers any buffered packets for that connection id.
  void ProcessChlo();

  // Returns client address used for stateless rejector to generate and validate
  // source address token.
  virtual const QuicSocketAddress GetClientAddress() const;

  QuicTimeWaitListManager* time_wait_list_manager() {
    return time_wait_list_manager_.get();
  }

  const QuicTransportVersionVector& GetSupportedTransportVersions();

  QuicConnectionId current_connection_id() { return current_connection_id_; }
  const QuicSocketAddress& current_server_address() {
    return current_server_address_;
  }
  const QuicSocketAddress& current_client_address() {
    return current_client_address_;
  }
  const QuicReceivedPacket& current_packet() { return *current_packet_; }

  const QuicConfig& config() const { return config_; }

  const QuicCryptoServerConfig* crypto_config() const { return crypto_config_; }

  QuicCompressedCertsCache* compressed_certs_cache() {
    return &compressed_certs_cache_;
  }

  QuicFramer* framer() { return &framer_; }

  QuicConnectionHelperInterface* helper() { return helper_.get(); }

  QuicCryptoServerStream::Helper* session_helper() {
    return session_helper_.get();
  }

  QuicAlarmFactory* alarm_factory() { return alarm_factory_.get(); }

  QuicPacketWriter* writer() { return writer_.get(); }

  // Creates per-connection packet writers out of the QuicDispatcher's shared
  // QuicPacketWriter. The per-connection writers' IsWriteBlocked() state must
  // always be the same as the shared writer's IsWriteBlocked(), or else the
  // QuicDispatcher::OnCanWrite logic will not work. (This will hopefully be
  // cleaned up for bug 16950226.)
  virtual QuicPacketWriter* CreatePerConnectionWriter();

  // Returns true if a session should be created for a connection with an
  // unknown version identified by |version_label|.
  virtual bool ShouldCreateSessionForUnknownVersion(
      QuicVersionLabel version_label);

  void SetLastError(QuicErrorCode error);

  // Called when the public header has been parsed and the session has been
  // looked up, and the session was not found in the active list of sessions.
  // Returns false if processing should stop after this call.
  virtual bool OnUnauthenticatedUnknownPublicHeader(
      const QuicPacketPublicHeader& header);

  // Called when a new connection starts to be handled by this dispatcher.
  // Either this connection is created or its packets is buffered while waiting
  // for CHLO. Returns true if a new connection should be created or its packets
  // should be buffered, false otherwise.
  virtual bool ShouldCreateOrBufferPacketForConnection(
      QuicConnectionId connection_id);

  bool HasBufferedPackets(QuicConnectionId connection_id);

  // Called when BufferEarlyPacket() fail to buffer the packet.
  virtual void OnBufferPacketFailure(
      QuicBufferedPacketStore::EnqueuePacketResult result,
      QuicConnectionId connection_id);

  // Removes the session from the session map and write blocked list, and adds
  // the ConnectionId to the time-wait list.  If |session_closed_statelessly| is
  // true, any future packets for the ConnectionId will be black-holed.
  virtual void CleanUpSession(SessionMap::iterator it,
                              QuicConnection* connection,
                              bool session_closed_statelessly);

  void StopAcceptingNewConnections();

  // Return true if the blocked writer should be added to blocked list.
  virtual bool ShouldAddToBlockedList();

 private:
  friend class test::QuicDispatcherPeer;
  friend class StatelessRejectorProcessDoneCallback;

  typedef QuicUnorderedSet<QuicConnectionId> QuicConnectionIdSet;

  bool HandlePacketForTimeWait(const QuicPacketPublicHeader& header);

  // Attempts to reject the connection statelessly, if stateless rejects are
  // possible and if the current packet contains a CHLO message.  Determines a
  // fate which describes what subsequent processing should be performed on the
  // packets, like ValidityChecks, and invokes ProcessUnauthenticatedHeaderFate.
  void MaybeRejectStatelessly(QuicConnectionId connection_id,
                              QuicTransportVersion version);

  // Deliver |packets| to |session| for further processing.
  void DeliverPacketsToSession(
      const std::list<QuicBufferedPacketStore::BufferedPacket>& packets,
      QuicSession* session);

  // Perform the appropriate actions on the current packet based on |fate| -
  // either process, buffer, or drop it.
  void ProcessUnauthenticatedHeaderFate(QuicPacketFate fate,
                                        QuicConnectionId connection_id);

  // Invoked when StatelessRejector::Process completes. |first_version| is the
  // version of the packet which initiated the stateless reject.
  void OnStatelessRejectorProcessDone(
      std::unique_ptr<StatelessRejector> rejector,
      const QuicSocketAddress& current_client_address,
      const QuicSocketAddress& current_server_address,
      std::unique_ptr<QuicReceivedPacket> current_packet,
      QuicTransportVersion first_version);

  // Examine the state of the rejector and decide what to do with the current
  // packet.
  void ProcessStatelessRejectorState(
      std::unique_ptr<StatelessRejector> rejector,
      QuicTransportVersion first_version);

  void set_new_sessions_allowed_per_event_loop(
      int16_t new_sessions_allowed_per_event_loop) {
    new_sessions_allowed_per_event_loop_ = new_sessions_allowed_per_event_loop;
  }

  const QuicConfig& config_;

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
  std::unique_ptr<QuicCryptoServerStream::Helper> session_helper_;

  // Creates alarms.
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;

  // An alarm which deletes closed sessions.
  std::unique_ptr<QuicAlarm> delete_sessions_alarm_;

  // The writer to write to the socket with.
  std::unique_ptr<QuicPacketWriter> writer_;

  // Packets which are buffered until a connection can be created to handle
  // them.
  QuicBufferedPacketStore buffered_packets_;

  // Set of connection IDs for which asynchronous CHLO processing is in
  // progress, making it necessary to buffer any other packets which arrive on
  // that connection until CHLO processing is complete.
  QuicConnectionIdSet temporarily_buffered_connections_;

  // Information about the packet currently being handled.
  QuicSocketAddress current_client_address_;
  QuicSocketAddress current_server_address_;
  const QuicReceivedPacket* current_packet_;
  // If |current_packet_| is a CHLO packet, the extracted alpn.
  std::string current_alpn_;
  QuicConnectionId current_connection_id_;

  // Used to get the supported versions based on flag. Does not own.
  QuicVersionManager* version_manager_;

  QuicFramer framer_;

  // The last error set by SetLastError(), which is called by
  // framer_visitor_->OnError().
  QuicErrorCode last_error_;

  // A backward counter of how many new sessions can be create within current
  // event loop. When reaches 0, it means can't create sessions for now.
  int16_t new_sessions_allowed_per_event_loop_;

  // True if this dispatcher is not draining.
  bool accept_new_connections_;

  DISALLOW_COPY_AND_ASSIGN(QuicDispatcher);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_DISPATCHER_H_
