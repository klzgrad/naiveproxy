// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A server side dispatcher which dispatches a given client's data to their
// stream.

#ifndef QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_
#define QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/frames/quic_rst_stream_frame.h"
#include "quiche/quic/core/frames/quic_stop_sending_frame.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_blocked_writer_interface.h"
#include "quiche/quic/core/quic_blocked_writer_list.h"
#include "quiche/quic/core/quic_buffered_packet_store.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_process_packet_interface.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time_wait_list_manager.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_version_manager.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {
namespace test {
class QuicDispatcherPeer;
}  // namespace test

class QuicConfig;
class QuicCryptoServerConfig;

class QUICHE_EXPORT QuicDispatcher
    : public QuicTimeWaitListManager::Visitor,
      public ProcessPacketInterface,
      public QuicBufferedPacketStore::VisitorInterface {
 public:
  QuicDispatcher(
      const QuicConfig* config, const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      uint8_t expected_server_connection_id_length,
      ConnectionIdGeneratorInterface& connection_id_generator);
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
                          QuicErrorCode error, const std::string& error_details,
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

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Try to add the new connection ID to the session map. Returns true on
  // success.
  bool TryAddNewConnectionId(
      const QuicConnectionId& server_connection_id,
      const QuicConnectionId& new_connection_id) override;

  // QuicSession::Visitor interface implementation (via inheritance of
  // QuicTimeWaitListManager::Visitor):
  // Remove the retired connection ID from the session map.
  void OnConnectionIdRetired(
      const QuicConnectionId& server_connection_id) override;

  void OnServerPreferredAddressAvailable(
      const QuicSocketAddress& /*server_preferred_address*/) override {
    QUICHE_DCHECK(false);
  }

  // QuicTimeWaitListManager::Visitor interface implementation
  // Called whenever the time wait list manager adds a new connection to the
  // time-wait list.
  void OnConnectionAddedToTimeWaitList(
      QuicConnectionId server_connection_id) override;

  using ReferenceCountedSessionMap =
      absl::flat_hash_map<QuicConnectionId, std::shared_ptr<QuicSession>,
                          QuicConnectionIdHash>;

  size_t NumSessions() const;

  // Deletes all sessions on the closed session list and clears the list.
  virtual void DeleteSessions();

  // Clear recent_stateless_reset_addresses_.
  void ClearStatelessResetAddresses();

  using ConnectionIdMap =
      absl::flat_hash_map<QuicConnectionId, QuicConnectionId,
                          QuicConnectionIdHash>;

  // QuicBufferedPacketStore::VisitorInterface implementation.
  void OnExpiredPackets(QuicConnectionId server_connection_id,
                        QuicBufferedPacketStore::BufferedPacketList
                            early_arrived_packets) override;
  void OnPathDegrading() override {}

  // Create connections for previously buffered CHLOs as many as allowed.
  virtual void ProcessBufferedChlos(size_t max_connections_to_create);

  // Return true if there is CHLO buffered.
  virtual bool HasChlosBuffered() const;

  // Start accepting new ConnectionIds.
  void StartAcceptingNewConnections();

  // Stop accepting new ConnectionIds, either as a part of the lame
  // duck process or because explicitly configured.
  void StopAcceptingNewConnections();

  // Apply an operation for each session.
  void PerformActionOnActiveSessions(
      quiche::UnretainedCallback<void(QuicSession*)> operation) const;

  // Get a snapshot of all sessions.
  std::vector<std::shared_ptr<QuicSession>> GetSessionsSnapshot() const;

  bool accept_new_connections() const { return accept_new_connections_; }

  uint64_t num_packets_received() const { return num_packets_received_; }

 protected:
  // Creates a QUIC session based on the given information.
  // |alpn| is the selected ALPN from |parsed_chlo.alpns|.
  virtual std::unique_ptr<QuicSession> CreateQuicSession(
      QuicConnectionId server_connection_id,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, absl::string_view alpn,
      const ParsedQuicVersion& version, const ParsedClientHello& parsed_chlo,
      ConnectionIdGeneratorInterface& connection_id_generator) = 0;

  // Tries to validate and dispatch packet based on available information.
  // Returns true if packet is dropped or successfully dispatched (e.g.,
  // processed by existing session, processed by time wait list, etc.),
  // otherwise, returns false and the packet needs further processing.
  virtual bool MaybeDispatchPacket(const ReceivedPacketInfo& packet_info);

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

  // Extra validity checks after the full Client Hello is parsed, this allows
  // subclasses to reject a connection based on sni or alpn.
  // Only called if ValidityChecks returns kFateProcess.
  virtual QuicPacketFate ValidityChecksOnFullChlo(
      const ReceivedPacketInfo& /*packet_info*/,
      const ParsedClientHello& /*parsed_chlo*/) const {
    return kFateProcess;
  }

  // Create and return the time wait list manager for this dispatcher, which
  // will be owned by the dispatcher as time_wait_list_manager_
  virtual QuicTimeWaitListManager* CreateQuicTimeWaitListManager();

  // Buffers packet until it can be delivered to a connection.
  void BufferEarlyPacket(const ReceivedPacketInfo& packet_info);

  // Called when |packet_info| is the last received packet of the client hello.
  // |parsed_chlo| is the parsed version of the client hello. Creates a new
  // connection and delivers any buffered packets for that connection id.
  void ProcessChlo(ParsedClientHello parsed_chlo,
                   ReceivedPacketInfo* packet_info);

  QuicTimeWaitListManager* time_wait_list_manager() {
    return time_wait_list_manager_.get();
  }

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

  const QuicCryptoServerStreamBase::Helper* session_helper() const {
    return session_helper_.get();
  }

  QuicAlarmFactory* alarm_factory() { return alarm_factory_.get(); }

  QuicPacketWriter* writer() { return writer_.get(); }

  // Returns true if a session should be created for a connection with an
  // unknown version.
  virtual bool ShouldCreateSessionForUnknownVersion(
      const ReceivedPacketInfo& packet_info);

  void SetLastError(QuicErrorCode error);

  // Called by MaybeDispatchPacket when current packet cannot be dispatched.
  // Used by subclasses to conduct specific logic to dispatch packet. Returns
  // true if packet is successfully dispatched.
  virtual bool OnFailedToDispatchPacket(const ReceivedPacketInfo& packet_info);

  bool HasBufferedPackets(QuicConnectionId server_connection_id);

  // Called when BufferEarlyPacket() fail to buffer the packet.
  virtual void OnBufferPacketFailure(
      QuicBufferedPacketStore::EnqueuePacketResult result,
      QuicConnectionId server_connection_id);

  // Removes the session from the write blocked list, and adds the ConnectionId
  // to the time-wait list.  The caller needs to manually remove the session
  // from the map after that.
  void CleanUpSession(QuicConnectionId server_connection_id,
                      QuicConnection* connection, QuicErrorCode error,
                      const std::string& error_details,
                      ConnectionCloseSource source);

  // Called to terminate a connection statelessly. Depending on |format|, either
  // 1) send connection close with |error_code| and |error_details| and add
  // connection to time wait list or 2) directly add connection to time wait
  // list with |action|.
  // |self_address| and |peer_address| are passed to
  // |OnStatelessConnectionCloseSent| when a connection close is sent.
  void StatelesslyTerminateConnection(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      QuicConnectionId server_connection_id, PacketHeaderFormat format,
      bool version_flag, bool use_length_prefix, ParsedQuicVersion version,
      QuicErrorCode error_code, const std::string& error_details,
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

  // Called by |StatelesslyTerminateConnection| when a connection close packet
  // is generated.
  virtual void OnStatelessConnectionCloseGenerated(
      const QuicSocketAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/,
      QuicConnectionId /*connection_id*/, ParsedQuicVersion /*version*/,
      QuicErrorCode /*error_code*/, const std::string& /*error_details*/) {}

  // Selects the preferred ALPN from a vector of ALPNs.
  // This runs through the list of ALPNs provided by the client and picks the
  // first one it supports. If no supported versions are found, the first
  // element of the vector is returned.
  std::string SelectAlpn(const std::vector<std::string>& alpns);

  // Sends public/stateless reset packets with no version and unknown
  // connection ID according to the packet's size.
  virtual void MaybeResetPacketsWithNoVersion(
      const quic::ReceivedPacketInfo& packet_info);

  // Called on packets with unsupported versions.
  virtual void MaybeSendVersionNegotiationPacket(
      const ReceivedPacketInfo& packet_info);

  virtual ConnectionIdGeneratorInterface& ConnectionIdGenerator() {
    return connection_id_generator_;
  }

 private:
  friend class test::QuicDispatcherPeer;

  // TODO(fayang): Consider to rename this function to
  // ProcessValidatedPacketWithUnknownConnectionId.
  void ProcessHeader(ReceivedPacketInfo* packet_info);

  struct ExtractChloResult {
    // If set, a full client hello has been successfully parsed.
    std::optional<ParsedClientHello> parsed_chlo;
    // If set, the TLS alert that will cause a connection close.
    // Always empty for Google QUIC.
    std::optional<uint8_t> tls_alert;
  };

  // Try to extract information(sni, alpns, ...) if the full Client Hello has
  // been parsed.
  //
  // Returns the parsed client hello in ExtractChloResult.parsed_chlo, if the
  // full Client Hello has been successfully parsed.
  //
  // Returns the TLS alert in ExtractChloResult.tls_alert, if the extraction of
  // Client Hello failed due to that alert.
  //
  // Otherwise returns a default-constructed ExtractChloResult and either buffer
  // or (rarely) drop the packet.
  ExtractChloResult TryExtractChloOrBufferEarlyPacket(
      const ReceivedPacketInfo& packet_info);

  // Deliver |packets| to |session| for further processing.
  void DeliverPacketsToSession(
      const std::list<QuicBufferedPacketStore::BufferedPacket>& packets,
      QuicSession* session);

  // Returns true if |version| is a supported protocol version.
  bool IsSupportedVersion(const ParsedQuicVersion version);

  // Returns true if a server connection ID length is below all the minima
  // required by various parameters.
  bool IsServerConnectionIdTooShort(QuicConnectionId connection_id) const;

  // Core CHLO processing logic.
  std::shared_ptr<QuicSession> CreateSessionFromChlo(
      QuicConnectionId original_connection_id,
      const ParsedClientHello& parsed_chlo, ParsedQuicVersion version,
      QuicSocketAddress self_address, QuicSocketAddress peer_address,
      ConnectionIdGeneratorInterface* connection_id_generator);

  const QuicConfig* config_;

  const QuicCryptoServerConfig* crypto_config_;

  // The cache for most recently compressed certs.
  QuicCompressedCertsCache compressed_certs_cache_;

  // The list of connections waiting to write.
  QuicBlockedWriterList write_blocked_list_;

  ReferenceCountedSessionMap reference_counted_session_map_;

  // Entity that manages connection_ids in time wait state.
  std::unique_ptr<QuicTimeWaitListManager> time_wait_list_manager_;

  // The list of closed but not-yet-deleted sessions.
  std::vector<std::shared_ptr<QuicSession>> closed_session_list_;

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

  // Number of unique session in session map.
  size_t num_sessions_in_session_map_ = 0;

  // Total number of packets received.
  uint64_t num_packets_received_ = 0;

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

  // Records client addresses that have been recently reset.
  absl::flat_hash_set<QuicSocketAddress, QuicSocketAddressHash>
      recent_stateless_reset_addresses_;

  // An alarm which clear recent_stateless_reset_addresses_.
  std::unique_ptr<QuicAlarm> clear_stateless_reset_addresses_alarm_;

  // If true, change expected_server_connection_id_length_ to be the received
  // destination connection ID length of all IETF long headers.
  bool should_update_expected_server_connection_id_length_;

  ConnectionIdGeneratorInterface& connection_id_generator_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DISPATCHER_H_
