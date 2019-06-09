// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_NETWORK_SESSION_H_
#define NET_HTTP_HTTP_NETWORK_SESSION_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/bind.h"
#include "base/containers/flat_set.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/threading/thread_checker.h"
#include "build/buildflag.h"
#include "net/base/host_mapping_rules.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_stream_factory.h"
#include "net/net_buildflags.h"
#include "net/quic/quic_stream_factory.h"
#include "net/socket/connect_job.h"
#include "net/socket/next_proto.h"
#include "net/socket/websocket_endpoint_lock_manager.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/ssl/ssl_client_auth_cache.h"
#include "net/ssl/ssl_client_session_cache.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace base {
class Value;
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace quic {
class QuicClock;
}  // namespace quic

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class ClientSocketFactory;
class ClientSocketPool;
class ClientSocketPoolManager;
class CTVerifier;
class HostResolver;
class HttpAuthHandlerFactory;
class HttpNetworkSessionPeer;
class HttpResponseBodyDrainer;
class HttpServerProperties;
class HttpUserAgentSettings;
class NetLog;
#if BUILDFLAG(ENABLE_REPORTING)
class NetworkErrorLoggingService;
#endif
class NetworkQualityEstimator;
class ProxyDelegate;
class ProxyResolutionService;
class ProxyServer;
class QuicCryptoClientStreamFactory;
#if BUILDFLAG(ENABLE_REPORTING)
class ReportingService;
#endif
class SocketPerformanceWatcherFactory;
class SSLConfigService;
class TransportSecurityState;

// Specifies the maximum HPACK dynamic table size the server is allowed to set.
const uint32_t kSpdyMaxHeaderTableSize = 64 * 1024;

// Specifies the maximum concurrent streams server could send (via push).
const uint32_t kSpdyMaxConcurrentPushedStreams = 1000;

// This class holds session objects used by HttpNetworkTransaction objects.
class NET_EXPORT HttpNetworkSession {
 public:
  // Self-contained structure with all the simple configuration options
  // supported by the HttpNetworkSession.
  struct NET_EXPORT Params {
    Params();
    Params(const Params& other);
    ~Params();

    bool enable_server_push_cancellation;
    HostMappingRules host_mapping_rules;
    bool ignore_certificate_errors;
    uint16_t testing_fixed_http_port;
    uint16_t testing_fixed_https_port;
    bool enable_user_alternate_protocol_ports;

    // Use SPDY ping frames to test for connection health after idle.
    bool enable_spdy_ping_based_connection_checking;
    bool enable_http2;
    size_t spdy_session_max_recv_window_size;
    // HTTP/2 connection settings.
    // Unknown settings will still be sent to the server.
    // Might contain unknown setting identifiers from a predefined set that
    // servers are supposed to ignore, see
    // https://tools.ietf.org/html/draft-bishop-httpbis-grease-00.
    // The same setting will be sent on every connection to prevent the retry
    // logic from hiding broken servers.
    spdy::SettingsMap http2_settings;
    // If set, an HTTP/2 frame with a reserved frame type will be sent after
    // every HEADERS and SETTINGS frame.  See
    // https://tools.ietf.org/html/draft-bishop-httpbis-grease-00.
    // The same frame will be sent out on all connections to prevent the retry
    // logic from hiding broken servers.
    base::Optional<SpdySessionPool::GreasedHttp2Frame> greased_http2_frame;
    // Source of time for SPDY connections.
    SpdySessionPool::TimeFunc time_func;
    // Whether to enable HTTP/2 Alt-Svc entries.
    bool enable_http2_alternative_service;
    // Whether to enable Websocket over HTTP/2.
    bool enable_websocket_over_http2;

    // Enables 0-RTT support.
    bool enable_early_data;

    // Enables QUIC support.
    bool enable_quic;

    // If true, HTTPS URLs can be sent to QUIC proxies.
    bool enable_quic_proxies_for_https_urls;

    // QUIC runtime configuration options.

    // Versions of QUIC which may be used.
    quic::ParsedQuicVersionVector quic_supported_versions;
    // User agent description to send in the QUIC handshake.
    std::string quic_user_agent_id;
    // Limit on the size of QUIC packets.
    size_t quic_max_packet_length;
    // Maximum number of server configs that are to be stored in
    // HttpServerProperties, instead of the disk cache.
    size_t quic_max_server_configs_stored_in_properties;
    // QUIC will be used for all connections in this set.
    std::set<HostPortPair> origins_to_force_quic_on;
    // Set of QUIC tags to send in the handshake's connection options.
    quic::QuicTagVector quic_connection_options;
    // Set of QUIC tags to send in the handshake's connection options that only
    // affect the client.
    quic::QuicTagVector quic_client_connection_options;
    // Enables experimental optimization for receiving data in UDPSocket.
    bool quic_enable_socket_recv_optimization;

    // Active QUIC experiments

    // Marks a QUIC server broken when a connection blackholes after the
    // handshake is confirmed.
    bool mark_quic_broken_when_network_blackholes;
    // Retry requests which fail with QUIC_PROTOCOL_ERROR, and mark QUIC
    // broken if the retry succeeds.
    bool retry_without_alt_svc_on_quic_errors;
    // If true, alt-svc headers advertising QUIC in IETF format will be
    // supported.
    bool support_ietf_format_quic_altsvc;
    // If true, all QUIC sessions are closed when any local IP address changes.
    bool quic_close_sessions_on_ip_change;
    // If true, all QUIC sessions are marked as goaway when any local IP address
    // changes.
    bool quic_goaway_sessions_on_ip_change;
    // Specifies QUIC idle connection state lifetime.
    int quic_idle_connection_timeout_seconds;
    // Specifies the reduced ping timeout subsequent connections should use when
    // a connection was timed out with open streams.
    int quic_reduced_ping_timeout_seconds;
    // Maximum time that a session can have no retransmittable packets on the
    // wire. Set to zero if not specified and no retransmittable PING will be
    // sent to peer when the wire has no retransmittable packets.
    int quic_retransmittable_on_wire_timeout_milliseconds;
    // Maximum time the session can be alive before crypto handshake is
    // finished.
    int quic_max_time_before_crypto_handshake_seconds;
    // Maximum idle time before the crypto handshake has completed.
    int quic_max_idle_time_before_crypto_handshake_seconds;
    // If true, connection migration v2 will be used to migrate existing
    // sessions to network when the platform indicates that the default network
    // is changing.
    bool quic_migrate_sessions_on_network_change_v2;
    // If true, connection migration v2 may be used to migrate active QUIC
    // sessions to alternative network if current network connectivity is poor.
    bool quic_migrate_sessions_early_v2;
    // If true, a new connection may be kicked off on an alternate network when
    // a connection fails on the default network before handshake is confirmed.
    bool quic_retry_on_alternate_network_before_handshake;
    // If true, an idle session will be migrated within the idle migration
    // period.
    bool quic_migrate_idle_sessions;
    // A session can be migrated if its idle time is within this period.
    base::TimeDelta quic_idle_session_migration_period;
    // Maximum time the session could be on the non-default network before
    // migrates back to default network. Defaults to
    // kMaxTimeOnNonDefaultNetwork.
    base::TimeDelta quic_max_time_on_non_default_network;
    // Maximum number of migrations to the non-default network on write error
    // per network for each session.
    int quic_max_migrations_to_non_default_network_on_write_error;
    // Maximum number of migrations to the non-default network on path
    // degrading per network for each session.
    int quic_max_migrations_to_non_default_network_on_path_degrading;
    // If true, allows migration of QUIC connections to a server-specified
    // alternate server address.
    bool quic_allow_server_migration;
    // If true, allows QUIC to use alternative services with a different
    // hostname from the origin.
    bool quic_allow_remote_alt_svc;
    // If true, the quic stream factory may race connection from stale dns
    // result with the original dns resolution
    bool quic_race_stale_dns_on_connection;
    // If true, the quic session may mark itself as GOAWAY on path degrading.
    bool quic_go_away_on_path_degrading;
    // If true, bidirectional streams over QUIC will be disabled.
    bool quic_disable_bidirectional_streams;
    // If true, race cert verification with host resolution.
    bool quic_race_cert_verification;
    // If true, estimate the initial RTT for QUIC connections based on network.
    bool quic_estimate_initial_rtt;
    // If true, client headers will include HTTP/2 stream dependency info
    // derived from the request priority.
    bool quic_headers_include_h2_stream_dependency;
    // The initial rtt that will be used in crypto handshake if no cached
    // smoothed rtt is present.
    int quic_initial_rtt_for_handshake_milliseconds;

    // If non-empty, QUIC will only be spoken to hosts in this list.
    base::flat_set<std::string> quic_host_whitelist;

    // Enable HTTP/0.9 for HTTP/HTTPS on ports other than the default one for
    // each protocol.
    bool http_09_on_non_default_ports_enabled;

    // If true, idle sockets won't be closed when memory pressure happens.
    bool disable_idle_sockets_close_on_memory_pressure;
  };

  // Structure with pointers to the dependencies of the HttpNetworkSession.
  // These objects must all outlive the HttpNetworkSession.
  struct NET_EXPORT Context {
    Context();
    Context(const Context& other);
    ~Context();

    ClientSocketFactory* client_socket_factory;
    HostResolver* host_resolver;
    CertVerifier* cert_verifier;
    TransportSecurityState* transport_security_state;
    CTVerifier* cert_transparency_verifier;
    CTPolicyEnforcer* ct_policy_enforcer;
    ProxyResolutionService* proxy_resolution_service;
    ProxyDelegate* proxy_delegate;
    const HttpUserAgentSettings* http_user_agent_settings;
    SSLConfigService* ssl_config_service;
    HttpAuthHandlerFactory* http_auth_handler_factory;
    HttpServerProperties* http_server_properties;
    NetLog* net_log;
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory;
    NetworkQualityEstimator* network_quality_estimator;
#if BUILDFLAG(ENABLE_REPORTING)
    ReportingService* reporting_service;
    NetworkErrorLoggingService* network_error_logging_service;
#endif

    // Source of time for QUIC connections.
    quic::QuicClock* quic_clock;
    // Source of entropy for QUIC connections.
    quic::QuicRandom* quic_random;
    // Optional factory to use for creating QuicCryptoClientStreams.
    QuicCryptoClientStreamFactory* quic_crypto_client_stream_factory;
  };

  enum SocketPoolType {
    NORMAL_SOCKET_POOL,
    WEBSOCKET_SOCKET_POOL,
    NUM_SOCKET_POOL_TYPES
  };

  HttpNetworkSession(const Params& params, const Context& context);
  ~HttpNetworkSession();

  HttpAuthCache* http_auth_cache() { return &http_auth_cache_; }
  SSLClientAuthCache* ssl_client_auth_cache() {
    return &ssl_client_auth_cache_;
  }

  void AddResponseDrainer(std::unique_ptr<HttpResponseBodyDrainer> drainer);

  // Removes the drainer from the session. Does not dispose of it.
  void RemoveResponseDrainer(HttpResponseBodyDrainer* drainer);

  // Returns the socket pool of the given type for use with the specified
  // ProxyServer. Use ProxyServer::Direct() to get the pool for use with direct
  // connections.
  ClientSocketPool* GetSocketPool(SocketPoolType pool_type,
                                  const ProxyServer& proxy_server);

  CertVerifier* cert_verifier() { return cert_verifier_; }
  ProxyResolutionService* proxy_resolution_service() {
      return proxy_resolution_service_;
  }
  SSLConfigService* ssl_config_service() { return ssl_config_service_; }
  WebSocketEndpointLockManager* websocket_endpoint_lock_manager() {
    return &websocket_endpoint_lock_manager_;
  }
  SpdySessionPool* spdy_session_pool() { return &spdy_session_pool_; }
  QuicStreamFactory* quic_stream_factory() { return &quic_stream_factory_; }
  HttpAuthHandlerFactory* http_auth_handler_factory() {
    return http_auth_handler_factory_;
  }
  HttpServerProperties* http_server_properties() {
    return http_server_properties_;
  }
  HttpStreamFactory* http_stream_factory() {
    return http_stream_factory_.get();
  }
  NetLog* net_log() {
    return net_log_;
  }
  HostResolver* host_resolver() { return host_resolver_; }
#if BUILDFLAG(ENABLE_REPORTING)
  ReportingService* reporting_service() const { return reporting_service_; }
  NetworkErrorLoggingService* network_error_logging_service() const {
    return network_error_logging_service_;
  }
#endif

  // Creates a Value summary of the state of the socket pools.
  std::unique_ptr<base::Value> SocketPoolInfoToValue() const;

  // Creates a Value summary of the state of the SPDY sessions.
  std::unique_ptr<base::Value> SpdySessionPoolInfoToValue() const;

  // Creates a Value summary of the state of the QUIC sessions and
  // configuration.
  std::unique_ptr<base::Value> QuicInfoToValue() const;

  void CloseAllConnections();
  void CloseIdleConnections();

  // Returns the original Params used to construct this session.
  const Params& params() const { return params_; }
  // Returns the original Context used to construct this session.
  const Context& context() const { return context_; }

  bool IsProtocolEnabled(NextProto protocol) const;

  void SetServerPushDelegate(std::unique_ptr<ServerPushDelegate> push_delegate);

  // Populates |*alpn_protos| with protocols to be used with ALPN.
  void GetAlpnProtos(NextProtoVector* alpn_protos) const;

  // Populates |server_config| and |proxy_config| based on this session and
  // |request|.
  void GetSSLConfig(const HttpRequestInfo& request,
                    SSLConfig* server_config,
                    SSLConfig* proxy_config) const;

  // Dumps memory allocation stats. |parent_dump_absolute_name| is the name
  // used by the parent MemoryAllocatorDump in the memory dump hierarchy.
  void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                       const std::string& parent_absolute_name) const;

  // Evaluates if QUIC is enabled for new streams.
  bool IsQuicEnabled() const;

  // Disable QUIC for new streams.
  void DisableQuic();

  // Clear the SSL session cache.
  void ClearSSLSessionCache();

  // Returns a CommonConnectJobParams that references the NetworkSession's
  // components. If |for_websockets| is true, the Params'
  // |websocket_endpoint_lock_manager| field will be populated. Otherwise, it
  // will be nullptr.
  CommonConnectJobParams CreateCommonConnectJobParams(
      bool for_websockets = false);

 private:
  friend class HttpNetworkSessionPeer;

  ClientSocketPoolManager* GetSocketPoolManager(SocketPoolType pool_type);

  // Flush sockets on low memory notifications callback.
  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

  NetLog* const net_log_;
  HttpServerProperties* const http_server_properties_;
  CertVerifier* const cert_verifier_;
  HttpAuthHandlerFactory* const http_auth_handler_factory_;
  HostResolver* const host_resolver_;

#if BUILDFLAG(ENABLE_REPORTING)
  ReportingService* const reporting_service_;
  NetworkErrorLoggingService* const network_error_logging_service_;
#endif
  ProxyResolutionService* const proxy_resolution_service_;
  SSLConfigService* const ssl_config_service_;

  HttpAuthCache http_auth_cache_;
  SSLClientAuthCache ssl_client_auth_cache_;
  SSLClientSessionCache ssl_client_session_cache_;
  SSLClientSessionCache ssl_client_session_cache_privacy_mode_;
  WebSocketEndpointLockManager websocket_endpoint_lock_manager_;
  std::unique_ptr<ClientSocketPoolManager> normal_socket_pool_manager_;
  std::unique_ptr<ClientSocketPoolManager> websocket_socket_pool_manager_;
  std::unique_ptr<ServerPushDelegate> push_delegate_;
  QuicStreamFactory quic_stream_factory_;
  SpdySessionPool spdy_session_pool_;
  std::unique_ptr<HttpStreamFactory> http_stream_factory_;
  std::map<HttpResponseBodyDrainer*, std::unique_ptr<HttpResponseBodyDrainer>>
      response_drainers_;
  NextProtoVector next_protos_;

  Params params_;
  Context context_;

  std::unique_ptr<base::MemoryPressureListener> memory_pressure_listener_;

  THREAD_CHECKER(thread_checker_);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_NETWORK_SESSION_H_
