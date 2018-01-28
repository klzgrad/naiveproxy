// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_STREAM_FACTORY_H_
#define NET_QUIC_CHROMIUM_QUIC_STREAM_FACTORY_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/cert/cert_database.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_stream_factory.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy/proxy_server.h"
#include "net/quic/chromium/network_connection.h"
#include "net/quic/chromium/quic_chromium_client_session.h"
#include "net/quic/chromium/quic_clock_skew_detector.h"
#include "net/quic/chromium/quic_http_stream.h"
#include "net/quic/core/quic_client_push_promise_index.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/ssl/ssl_config_service.h"

namespace base {
class Value;
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class ChannelIDService;
class ClientSocketFactory;
class CTVerifier;
class HostResolver;
class HttpServerProperties;
class NetLog;
class QuicClock;
class QuicAlarmFactory;
class QuicChromiumConnectionHelper;
class QuicCryptoClientStreamFactory;
class QuicRandom;
class QuicServerInfo;
class QuicStreamFactory;
class SocketPerformanceWatcherFactory;
class TransportSecurityState;
class BidirectionalStreamImpl;

namespace test {
class QuicStreamFactoryPeer;
}  // namespace test

// When a connection is idle for 30 seconds it will be closed.
const int kIdleConnectionTimeoutSeconds = 30;

// Result of a session migration attempt.
enum class MigrationResult {
  SUCCESS,         // Migration succeeded.
  NO_NEW_NETWORK,  // Migration failed since no new network was found.
  FAILURE          // Migration failed for other reasons.
};

enum QuicConnectionMigrationStatus {
  MIGRATION_STATUS_NO_MIGRATABLE_STREAMS,
  MIGRATION_STATUS_ALREADY_MIGRATED,
  MIGRATION_STATUS_INTERNAL_ERROR,
  MIGRATION_STATUS_TOO_MANY_CHANGES,
  MIGRATION_STATUS_SUCCESS,
  MIGRATION_STATUS_NON_MIGRATABLE_STREAM,
  MIGRATION_STATUS_DISABLED,
  MIGRATION_STATUS_NO_ALTERNATE_NETWORK,
  MIGRATION_STATUS_MAX
};

enum QuicPlatformNotification {
  NETWORK_CONNECTED,
  NETWORK_MADE_DEFAULT,
  NETWORK_DISCONNECTED,
  NETWORK_SOON_TO_DISCONNECT,
  NETWORK_IP_ADDRESS_CHANGED,
  NETWORK_NOTIFICATION_MAX
};

// Encapsulates a pending request for a QuicHttpStream.
// If the request is still pending when it is destroyed, it will
// cancel the request with the factory.
class NET_EXPORT_PRIVATE QuicStreamRequest {
 public:
  explicit QuicStreamRequest(QuicStreamFactory* factory);
  ~QuicStreamRequest();

  // |cert_verify_flags| is bitwise OR'd of CertVerifier::VerifyFlags and it is
  // passed to CertVerifier::Verify.
  // |destination| will be resolved and resulting IPEndPoint used to open a
  // QuicConnection.  This can be different than HostPortPair::FromURL(url).
  int Request(const HostPortPair& destination,
              QuicTransportVersion quic_version,
              PrivacyMode privacy_mode,
              int cert_verify_flags,
              const GURL& url,
              QuicStringPiece method,
              const NetLogWithSource& net_log,
              NetErrorDetails* net_error_details,
              const CompletionCallback& callback);

  void OnRequestComplete(int rv);

  // Helper method that calls |factory_|'s GetTimeDelayForWaitingJob(). It
  // returns the amount of time waiting job should be delayed.
  base::TimeDelta GetTimeDelayForWaitingJob() const;

  std::unique_ptr<HttpStream> CreateStream();

  std::unique_ptr<BidirectionalStreamImpl> CreateBidirectionalStreamImpl();

  // Sets |session_|.
  void SetSession(std::unique_ptr<QuicChromiumClientSession::Handle> session);

  NetErrorDetails* net_error_details() { return net_error_details_; }

  const QuicServerId& server_id() const { return server_id_; }

  const NetLogWithSource& net_log() const { return net_log_; }

 private:
  QuicStreamFactory* factory_;
  QuicServerId server_id_;
  NetLogWithSource net_log_;
  CompletionCallback callback_;
  NetErrorDetails* net_error_details_;  // Unowned.
  std::unique_ptr<QuicChromiumClientSession::Handle> session_;

  DISALLOW_COPY_AND_ASSIGN(QuicStreamRequest);
};

// A factory for creating new QuicHttpStreams on top of a pool of
// QuicChromiumClientSessions.
class NET_EXPORT_PRIVATE QuicStreamFactory
    : public NetworkChangeNotifier::IPAddressObserver,
      public NetworkChangeNotifier::NetworkObserver,
      public SSLConfigService::Observer,
      public CertDatabase::Observer {
 public:
  // This class encompasses |destination| and |server_id|.
  // |destination| is a HostPortPair which is resolved
  // and a QuicConnection is made to the resulting IP address.
  // |server_id| identifies the origin of the request,
  // the crypto handshake advertises |server_id.host()| to the server,
  // and the certificate is also matched against |server_id.host()|.
  class NET_EXPORT_PRIVATE QuicSessionKey {
   public:
    QuicSessionKey() = default;
    QuicSessionKey(const HostPortPair& destination,
                   const QuicServerId& server_id);
    ~QuicSessionKey() = default;

    // Needed to be an element of std::set.
    bool operator<(const QuicSessionKey& other) const;
    bool operator==(const QuicSessionKey& other) const;

    const HostPortPair& destination() const { return destination_; }
    const QuicServerId& server_id() const { return server_id_; }

    // Returns the estimate of dynamically allocated memory in bytes.
    size_t EstimateMemoryUsage() const;

   private:
    HostPortPair destination_;
    QuicServerId server_id_;
  };

  QuicStreamFactory(
      NetLog* net_log,
      HostResolver* host_resolver,
      SSLConfigService* ssl_config_service,
      ClientSocketFactory* client_socket_factory,
      HttpServerProperties* http_server_properties,
      CertVerifier* cert_verifier,
      CTPolicyEnforcer* ct_policy_enforcer,
      ChannelIDService* channel_id_service,
      TransportSecurityState* transport_security_state,
      CTVerifier* cert_transparency_verifier,
      SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
      QuicCryptoClientStreamFactory* quic_crypto_client_stream_factory,
      QuicRandom* random_generator,
      QuicClock* clock,
      size_t max_packet_length,
      const std::string& user_agent_id,
      bool store_server_configs_in_properties,
      bool mark_quic_broken_when_network_blackholes,
      int idle_connection_timeout_seconds,
      int reduced_ping_timeout_seconds,
      bool connect_using_default_network,
      bool migrate_sessions_on_network_change,
      bool migrate_sessions_early,
      bool allow_server_migration,
      bool race_cert_verification,
      bool estimate_initial_rtt,
      const QuicTagVector& connection_options,
      const QuicTagVector& client_connection_options,
      bool enable_token_binding);
  ~QuicStreamFactory() override;

  // Returns true if there is an existing session for |server_id| or if the
  // request can be pooled to an existing session to the IP address of
  // |destination|.
  bool CanUseExistingSession(const QuicServerId& server_id,
                             const HostPortPair& destination);

  // Creates a new QuicHttpStream to |host_port_pair| which will be
  // owned by |request|.
  // If a matching session already exists, this method will return OK.  If no
  // matching session exists, this will return ERR_IO_PENDING and will invoke
  // OnRequestComplete asynchronously.
  int Create(const QuicServerId& server_id,
             const HostPortPair& destination,
             QuicTransportVersion quic_version,
             int cert_verify_flags,
             const GURL& url,
             QuicStringPiece method,
             const NetLogWithSource& net_log,
             QuicStreamRequest* request);

  // Called when the handshake for |session| is confirmed. If QUIC is disabled
  // currently disabled, then it closes the connection and returns true.
  bool OnHandshakeConfirmed(QuicChromiumClientSession* session);

  // Called when a TCP job completes for an origin that QUIC potentially
  // could be used for.
  void OnTcpJobCompleted(bool succeeded);

  // Called by a session when it becomes idle.
  void OnIdleSession(QuicChromiumClientSession* session);

  // Returns true if QUIC is know to be broken for |session|.
  bool IsQuicBroken(QuicChromiumClientSession* session);

  // Called by a session when it is going away and no more streams should be
  // created on it.
  void OnSessionGoingAway(QuicChromiumClientSession* session);

  // Called by a session after it shuts down.
  void OnSessionClosed(QuicChromiumClientSession* session);

  // Called by a session when it blackholes after the handshake is confirmed.
  void OnBlackholeAfterHandshakeConfirmed(QuicChromiumClientSession* session);

  // Cancels a pending request.
  void CancelRequest(QuicStreamRequest* request);

  // Closes all current sessions with specified network and QUIC error codes.
  void CloseAllSessions(int error, QuicErrorCode quic_error);

  std::unique_ptr<base::Value> QuicStreamFactoryInfoToValue() const;

  // Delete cached state objects in |crypto_config_|. If |origin_filter| is not
  // null, only objects on matching origins will be deleted.
  void ClearCachedStatesInCryptoConfig(
      const base::Callback<bool(const GURL&)>& origin_filter);

  // Helper method that configures a DatagramClientSocket. Socket is
  // bound to the default network if the |network| param is
  // NetworkChangeNotifier::kInvalidNetworkHandle.
  // Returns net_error code.
  int ConfigureSocket(DatagramClientSocket* socket,
                      IPEndPoint addr,
                      NetworkChangeNotifier::NetworkHandle network);

  // Finds an alternative to |old_network| from the platform's list of connected
  // networks. Returns NetworkChangeNotifier::kInvalidNetworkHandle if no
  // alternative is found.
  NetworkChangeNotifier::NetworkHandle FindAlternateNetwork(
      NetworkChangeNotifier::NetworkHandle old_network);

  // Method that initiates migration of active sessions to |new_network|.
  // If |new_network| is a valid network, sessions that can migrate are
  // migrated to |new_network|, and sessions not bound to |new_network|
  // are left unchanged. Sessions with non-migratable streams are closed
  // if |close_if_cannot_migrate| is true, and continue using their current
  // network otherwise.
  //
  // If |new_network| is NetworkChangeNotifier::kInvalidNetworkHandle,
  // there is no new network to migrate sessions onto, and all sessions are
  // closed.
  void MaybeMigrateOrCloseSessions(
      NetworkChangeNotifier::NetworkHandle new_network,
      bool close_if_cannot_migrate,
      const NetLogWithSource& net_log);

  // Method that attempts migration of |session| on write error with
  // |error_code| if |session| is active and if there is an alternate network
  // than the one to which |session| is currently bound.
  MigrationResult MaybeMigrateSingleSessionOnWriteError(
      QuicChromiumClientSession* session,
      int error_code);

  // Method that attempts migration of |session| on path degrading if |session|
  // is active and if there is an alternate network than the one to which
  // |session| is currently bound.
  MigrationResult MaybeMigrateSingleSessionOnPathDegrading(
      QuicChromiumClientSession* session);

  // Migrates |session| over to using |network|. If |network| is
  // kInvalidNetworkHandle, default network is used.
  MigrationResult MigrateSessionToNewNetwork(
      QuicChromiumClientSession* session,
      NetworkChangeNotifier::NetworkHandle network,
      bool close_session_on_error,
      const NetLogWithSource& net_log);

  // Migrates |session| over to using |peer_address|. Causes a PING frame
  // to be sent to the new peer address.
  void MigrateSessionToNewPeerAddress(QuicChromiumClientSession* session,
                                      IPEndPoint peer_address,
                                      const NetLogWithSource& net_log);

  // NetworkChangeNotifier::IPAddressObserver methods:

  // Called when local IP address changes. Must not be called if
  // |migrate_sessions_on_network_change_| is true.
  void OnIPAddressChanged() override;

  // NetworkChangeNotifier::NetworkObserver methods:
  void OnNetworkConnected(
      NetworkChangeNotifier::NetworkHandle network) override;
  void OnNetworkDisconnected(
      NetworkChangeNotifier::NetworkHandle network) override;
  void OnNetworkSoonToDisconnect(
      NetworkChangeNotifier::NetworkHandle network) override;
  void OnNetworkMadeDefault(
      NetworkChangeNotifier::NetworkHandle network) override;

  // SSLConfigService::Observer methods:

  // We perform the same flushing as described above when SSL settings change.
  void OnSSLConfigChanged() override;

  // CertDatabase::Observer methods:

  // We close all sessions when certificate database is changed.
  void OnCertDBChanged() override;

  bool require_confirmation() const { return require_confirmation_; }

  void set_require_confirmation(bool require_confirmation);

  // It returns the amount of time waiting job should be delayed.
  base::TimeDelta GetTimeDelayForWaitingJob(const QuicServerId& server_id);

  QuicChromiumConnectionHelper* helper() { return helper_.get(); }

  QuicAlarmFactory* alarm_factory() { return alarm_factory_.get(); }

  void set_server_push_delegate(ServerPushDelegate* push_delegate) {
    push_delegate_ = push_delegate;
  }

  bool migrate_sessions_on_network_change() const {
    return migrate_sessions_on_network_change_;
  }

  bool mark_quic_broken_when_network_blackholes() const {
    return mark_quic_broken_when_network_blackholes_;
  }

  // Dumps memory allocation stats. |parent_dump_absolute_name| is the name
  // used by the parent MemoryAllocatorDump in the memory dump hierarchy.
  void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                       const std::string& parent_absolute_name) const;

 private:
  class Job;
  class CertVerifierJob;
  friend class test::QuicStreamFactoryPeer;

  typedef std::map<QuicServerId, QuicChromiumClientSession*> SessionMap;
  typedef std::map<QuicChromiumClientSession*, QuicSessionKey> SessionIdMap;
  typedef std::set<QuicSessionKey> AliasSet;
  typedef std::map<QuicChromiumClientSession*, AliasSet> SessionAliasMap;
  typedef std::set<QuicChromiumClientSession*> SessionSet;
  typedef std::map<IPEndPoint, SessionSet> IPAliasMap;
  typedef std::map<QuicChromiumClientSession*, IPEndPoint> SessionPeerIPMap;
  typedef std::map<QuicServerId, std::unique_ptr<Job>> JobMap;
  typedef std::map<QuicServerId, std::unique_ptr<CertVerifierJob>>
      CertVerifierJobMap;

  bool OnResolution(const QuicSessionKey& key, const AddressList& address_list);
  void OnJobComplete(Job* job, int rv);
  void OnCertVerifyJobComplete(CertVerifierJob* job, int rv);
  bool HasActiveSession(const QuicServerId& server_id) const;
  bool HasActiveJob(const QuicServerId& server_id) const;
  bool HasActiveCertVerifierJob(const QuicServerId& server_id) const;
  int CreateSession(const QuicSessionKey& key,
                    const QuicTransportVersion& quic_version,
                    int cert_verify_flags,
                    bool require_confirmation,
                    const AddressList& address_list,
                    base::TimeTicks dns_resolution_start_time,
                    base::TimeTicks dns_resolution_end_time,
                    const NetLogWithSource& net_log,
                    QuicChromiumClientSession** session);
  void ActivateSession(const QuicSessionKey& key,
                       QuicChromiumClientSession* session);

  // Method that initiates migration of |session| if |session| is
  // active and if there is an alternate network than the one to which
  // |session| is currently bound.
  MigrationResult MaybeMigrateSingleSession(QuicChromiumClientSession* session,
                                            bool close_session_on_error,
                                            const NetLogWithSource& net_log);

  void ConfigureInitialRttEstimate(const QuicServerId& server_id,
                                   QuicConfig* config);

  // Returns |srtt| in micro seconds from ServerNetworkStats. Returns 0 if there
  // is no |http_server_properties_| or if |http_server_properties_| doesn't
  // have ServerNetworkStats for the given |server_id|.
  int64_t GetServerNetworkStatsSmoothedRttInMicroseconds(
      const QuicServerId& server_id) const;

  // Returns |srtt| from ServerNetworkStats. Returns null if there
  // is no |http_server_properties_| or if |http_server_properties_| doesn't
  // have ServerNetworkStats for the given |server_id|.
  const base::TimeDelta* GetServerNetworkStatsSmoothedRtt(
      const QuicServerId& server_id) const;

  // Helper methods.
  bool WasQuicRecentlyBroken(const QuicServerId& server_id) const;

  bool CryptoConfigCacheIsEmpty(const QuicServerId& server_id);

  // Starts an asynchronous job for cert verification if
  // |race_cert_verification_| is enabled and if there are cached certs for the
  // given |server_id|.
  QuicAsyncStatus StartCertVerifyJob(const QuicServerId& server_id,
                                     int cert_verify_flags,
                                     const NetLogWithSource& net_log);

  // Initializes the cached state associated with |server_id| in
  // |crypto_config_| with the information in |server_info|. Populates
  // |connection_id| with the next server designated connection id,
  // if any, and otherwise leaves it unchanged.
  void InitializeCachedStateInCryptoConfig(
      const QuicServerId& server_id,
      const std::unique_ptr<QuicServerInfo>& server_info,
      QuicConnectionId* connection_id);

  void ProcessGoingAwaySession(QuicChromiumClientSession* session,
                               const QuicServerId& server_id,
                               bool was_session_active);

  // Internal method that migrates |session| over to using
  // |peer_address| and |network|. If |network| is
  // kInvalidNetworkHandle, default network is used. If the migration
  // fails and |close_session_on_error| is true, connection is closed.
  MigrationResult MigrateSessionInner(
      QuicChromiumClientSession* session,
      IPEndPoint peer_address,
      NetworkChangeNotifier::NetworkHandle network,
      bool close_session_on_error,
      const NetLogWithSource& net_log);

  bool require_confirmation_;
  NetLog* net_log_;
  HostResolver* host_resolver_;
  ClientSocketFactory* client_socket_factory_;
  HttpServerProperties* http_server_properties_;
  ServerPushDelegate* push_delegate_;
  TransportSecurityState* transport_security_state_;
  CTVerifier* cert_transparency_verifier_;
  QuicCryptoClientStreamFactory* quic_crypto_client_stream_factory_;
  QuicRandom* random_generator_;  // Unowned.
  QuicClock* clock_;              // Unowned.
  const size_t max_packet_length_;
  QuicClockSkewDetector clock_skew_detector_;

  // Factory which is used to create socket performance watcher. A new watcher
  // is created for every QUIC connection.
  // |socket_performance_watcher_factory_| may be null.
  SocketPerformanceWatcherFactory* socket_performance_watcher_factory_;

  // The helper used for all connections.
  std::unique_ptr<QuicChromiumConnectionHelper> helper_;

  // The alarm factory used for all connections.
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;

  // Contains owning pointers to all sessions that currently exist.
  SessionIdMap all_sessions_;
  // Contains non-owning pointers to currently active session
  // (not going away session, once they're implemented).
  SessionMap active_sessions_;
  // Map from session to set of aliases that this session is known by.
  SessionAliasMap session_aliases_;
  // Map from IP address to sessions which are connected to this address.
  IPAliasMap ip_aliases_;
  // Map from session to its original peer IP address.
  SessionPeerIPMap session_peer_ip_;

  // Origins which have gone away recently.
  AliasSet gone_away_aliases_;

  const QuicConfig config_;
  QuicCryptoClientConfig crypto_config_;

  JobMap active_jobs_;

  // Map of QuicServerId to owning CertVerifierJob.
  CertVerifierJobMap active_cert_verifier_jobs_;

  // True if QUIC should be marked as broken when a connection blackholes after
  // the handshake is confirmed.
  bool mark_quic_broken_when_network_blackholes_;

  // Set if QUIC server configs should be stored in HttpServerProperties.
  bool store_server_configs_in_properties_;

  // PING timeout for connections.
  QuicTime::Delta ping_timeout_;
  QuicTime::Delta reduced_ping_timeout_;

  base::TimeTicks most_recent_path_degrading_timestamp_;
  base::TimeTicks most_recent_network_disconnected_timestamp_;

  int most_recent_write_error_;
  base::TimeTicks most_recent_write_error_timestamp_;

  // If more than |yield_after_packets_| packets have been read or more than
  // |yield_after_duration_| time has passed, then
  // QuicChromiumPacketReader::StartReading() yields by doing a PostTask().
  int yield_after_packets_;
  QuicTime::Delta yield_after_duration_;

  // Set if sockets should explicitly use default network to connect and
  // NetworkHandle is supported.
  const bool connect_using_default_network_;

  // Set if migration should be attempted on active sessions when primary
  // interface changes.
  const bool migrate_sessions_on_network_change_;

  // Set if early migration should be attempted when the connection
  // experiences poor connectivity.
  const bool migrate_sessions_early_;

  // If set, allows migration of connection to server-specified alternate
  // server address.
  const bool allow_server_migration_;

  // Set if cert verification is to be raced with host resolution.
  bool race_cert_verification_;

  // If true, estimate the initial RTT based on network type.
  bool estimate_initial_rtt;

  // Local address of socket that was created in CreateSession.
  IPEndPoint local_address_;
  // True if we need to check HttpServerProperties if QUIC was supported last
  // time.
  bool need_to_check_persisted_supports_quic_;

  NetworkConnection network_connection_;

  int num_push_streams_created_;

  QuicClientPushPromiseIndex push_promise_index_;

  base::TaskRunner* task_runner_;

  const scoped_refptr<SSLConfigService> ssl_config_service_;

  base::WeakPtrFactory<QuicStreamFactory> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuicStreamFactory);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_STREAM_FACTORY_H_
