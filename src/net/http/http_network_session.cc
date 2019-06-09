// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_network_session.h"

#include <inttypes.h>

#include <utility>

#include "base/atomic_sequence_num.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/values.h"
#include "net/base/features.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_response_body_drainer.h"
#include "net/http/http_stream_factory.h"
#include "net/http/url_security_manager.h"
#include "net/proxy_resolution/proxy_resolution_service.h"
#include "net/quic/platform/impl/quic_chromium_clock.h"
#include "net/quic/quic_crypto_client_stream_factory.h"
#include "net/quic/quic_stream_factory.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_pool_manager_impl.h"
#include "net/socket/next_proto.h"
#include "net/socket/ssl_client_socket.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace net {

namespace {

SSLClientSocketContext CreateClientSocketContext(
    const HttpNetworkSession::Context& context,
    SSLClientSessionCache* ssl_client_session_cache) {
  return SSLClientSocketContext(
      context.cert_verifier, context.transport_security_state,
      context.cert_transparency_verifier, context.ct_policy_enforcer,
      ssl_client_session_cache);
}

}  // unnamed namespace

// The maximum receive window sizes for HTTP/2 sessions and streams.
const int32_t kSpdySessionMaxRecvWindowSize = 15 * 1024 * 1024;  // 15 MB
const int32_t kSpdyStreamMaxRecvWindowSize = 6 * 1024 * 1024;    //  6 MB

namespace {

// Keep all HTTP2 parameters in |http2_settings|, even the ones that are not
// implemented, to be sent to the server.
// Set default values for settings that |http2_settings| does not specify.
spdy::SettingsMap AddDefaultHttp2Settings(spdy::SettingsMap http2_settings) {
  // Set default values only if |http2_settings| does not have
  // a value set for given setting.
  auto it = http2_settings.find(spdy::SETTINGS_HEADER_TABLE_SIZE);
  if (it == http2_settings.end())
    http2_settings[spdy::SETTINGS_HEADER_TABLE_SIZE] = kSpdyMaxHeaderTableSize;

  it = http2_settings.find(spdy::SETTINGS_MAX_CONCURRENT_STREAMS);
  if (it == http2_settings.end())
    http2_settings[spdy::SETTINGS_MAX_CONCURRENT_STREAMS] =
        kSpdyMaxConcurrentPushedStreams;

  it = http2_settings.find(spdy::SETTINGS_INITIAL_WINDOW_SIZE);
  if (it == http2_settings.end())
    http2_settings[spdy::SETTINGS_INITIAL_WINDOW_SIZE] =
        kSpdyStreamMaxRecvWindowSize;

  return http2_settings;
}

}  // unnamed namespace

HttpNetworkSession::Params::Params()
    : enable_server_push_cancellation(false),
      ignore_certificate_errors(false),
      testing_fixed_http_port(0),
      testing_fixed_https_port(0),
      enable_user_alternate_protocol_ports(false),
      enable_spdy_ping_based_connection_checking(true),
      enable_http2(true),
      spdy_session_max_recv_window_size(kSpdySessionMaxRecvWindowSize),
      time_func(&base::TimeTicks::Now),
      enable_http2_alternative_service(false),
      enable_websocket_over_http2(false),
      enable_quic(false),
      enable_quic_proxies_for_https_urls(false),
      quic_max_packet_length(quic::kDefaultMaxPacketSize),
      quic_max_server_configs_stored_in_properties(0u),
      quic_enable_socket_recv_optimization(false),
      mark_quic_broken_when_network_blackholes(false),
      retry_without_alt_svc_on_quic_errors(true),
      support_ietf_format_quic_altsvc(false),
      quic_close_sessions_on_ip_change(false),
      quic_goaway_sessions_on_ip_change(false),
      quic_idle_connection_timeout_seconds(kIdleConnectionTimeoutSeconds),
      quic_reduced_ping_timeout_seconds(quic::kPingTimeoutSecs),
      quic_retransmittable_on_wire_timeout_milliseconds(0),
      quic_max_time_before_crypto_handshake_seconds(
          quic::kMaxTimeForCryptoHandshakeSecs),
      quic_max_idle_time_before_crypto_handshake_seconds(
          quic::kInitialIdleTimeoutSecs),
      quic_migrate_sessions_on_network_change_v2(false),
      quic_migrate_sessions_early_v2(false),
      quic_retry_on_alternate_network_before_handshake(false),
      quic_migrate_idle_sessions(false),
      quic_idle_session_migration_period(base::TimeDelta::FromSeconds(
          kDefaultIdleSessionMigrationPeriodSeconds)),
      quic_max_time_on_non_default_network(
          base::TimeDelta::FromSeconds(kMaxTimeOnNonDefaultNetworkSecs)),
      quic_max_migrations_to_non_default_network_on_write_error(
          kMaxMigrationsToNonDefaultNetworkOnWriteError),
      quic_max_migrations_to_non_default_network_on_path_degrading(
          kMaxMigrationsToNonDefaultNetworkOnPathDegrading),
      quic_allow_server_migration(false),
      quic_allow_remote_alt_svc(true),
      quic_race_stale_dns_on_connection(false),
      quic_go_away_on_path_degrading(false),
      quic_disable_bidirectional_streams(false),
      quic_race_cert_verification(false),
      quic_estimate_initial_rtt(false),
      quic_headers_include_h2_stream_dependency(false),
      quic_initial_rtt_for_handshake_milliseconds(0),
      http_09_on_non_default_ports_enabled(false),
      disable_idle_sockets_close_on_memory_pressure(false) {
  quic_supported_versions.push_back(quic::ParsedQuicVersion(
      quic::PROTOCOL_QUIC_CRYPTO, quic::QUIC_VERSION_46));
  enable_early_data =
      base::FeatureList::IsEnabled(features::kEnableTLS13EarlyData);
}

HttpNetworkSession::Params::Params(const Params& other) = default;

HttpNetworkSession::Params::~Params() = default;

HttpNetworkSession::Context::Context()
    : client_socket_factory(nullptr),
      host_resolver(nullptr),
      cert_verifier(nullptr),
      transport_security_state(nullptr),
      cert_transparency_verifier(nullptr),
      ct_policy_enforcer(nullptr),
      proxy_resolution_service(nullptr),
      proxy_delegate(nullptr),
      http_user_agent_settings(nullptr),
      ssl_config_service(nullptr),
      http_auth_handler_factory(nullptr),
      net_log(nullptr),
      socket_performance_watcher_factory(nullptr),
      network_quality_estimator(nullptr),
#if BUILDFLAG(ENABLE_REPORTING)
      reporting_service(nullptr),
      network_error_logging_service(nullptr),
#endif
      quic_clock(nullptr),
      quic_random(nullptr),
      quic_crypto_client_stream_factory(
          QuicCryptoClientStreamFactory::GetDefaultFactory()) {
}

HttpNetworkSession::Context::Context(const Context& other) = default;

HttpNetworkSession::Context::~Context() = default;

// TODO(mbelshe): Move the socket factories into HttpStreamFactory.
HttpNetworkSession::HttpNetworkSession(const Params& params,
                                       const Context& context)
    : net_log_(context.net_log),
      http_server_properties_(context.http_server_properties),
      cert_verifier_(context.cert_verifier),
      http_auth_handler_factory_(context.http_auth_handler_factory),
      host_resolver_(context.host_resolver),
#if BUILDFLAG(ENABLE_REPORTING)
      reporting_service_(context.reporting_service),
      network_error_logging_service_(context.network_error_logging_service),
#endif
      proxy_resolution_service_(context.proxy_resolution_service),
      ssl_config_service_(context.ssl_config_service),
      ssl_client_session_cache_(SSLClientSessionCache::Config()),
      ssl_client_session_cache_privacy_mode_(SSLClientSessionCache::Config()),
      push_delegate_(nullptr),
      quic_stream_factory_(
          context.net_log,
          context.host_resolver,
          context.ssl_config_service,
          context.client_socket_factory
              ? context.client_socket_factory
              : ClientSocketFactory::GetDefaultFactory(),
          context.http_server_properties,
          context.cert_verifier,
          context.ct_policy_enforcer,
          context.transport_security_state,
          context.cert_transparency_verifier,
          context.socket_performance_watcher_factory,
          context.quic_crypto_client_stream_factory,
          context.quic_random ? context.quic_random
                              : quic::QuicRandom::GetInstance(),
          context.quic_clock ? context.quic_clock
                             : quic::QuicChromiumClock::GetInstance(),
          params.quic_max_packet_length,
          params.quic_user_agent_id,
          params.quic_max_server_configs_stored_in_properties > 0,
          params.quic_close_sessions_on_ip_change,
          params.quic_goaway_sessions_on_ip_change,
          params.mark_quic_broken_when_network_blackholes,
          params.quic_idle_connection_timeout_seconds,
          params.quic_reduced_ping_timeout_seconds,
          params.quic_retransmittable_on_wire_timeout_milliseconds,
          params.quic_max_time_before_crypto_handshake_seconds,
          params.quic_max_idle_time_before_crypto_handshake_seconds,
          params.quic_migrate_sessions_on_network_change_v2,
          params.quic_migrate_sessions_early_v2,
          params.quic_retry_on_alternate_network_before_handshake,
          params.quic_migrate_idle_sessions,
          params.quic_idle_session_migration_period,
          params.quic_max_time_on_non_default_network,
          params.quic_max_migrations_to_non_default_network_on_write_error,
          params.quic_max_migrations_to_non_default_network_on_path_degrading,
          params.quic_allow_server_migration,
          params.quic_race_stale_dns_on_connection,
          params.quic_go_away_on_path_degrading,
          params.quic_race_cert_verification,
          params.quic_estimate_initial_rtt,
          params.quic_headers_include_h2_stream_dependency,
          params.quic_connection_options,
          params.quic_client_connection_options,
          params.quic_enable_socket_recv_optimization,
          params.quic_initial_rtt_for_handshake_milliseconds),
      spdy_session_pool_(context.host_resolver,
                         context.ssl_config_service,
                         context.http_server_properties,
                         context.transport_security_state,
                         params.quic_supported_versions,
                         params.enable_spdy_ping_based_connection_checking,
                         params.support_ietf_format_quic_altsvc,
                         params.spdy_session_max_recv_window_size,
                         AddDefaultHttp2Settings(params.http2_settings),
                         params.greased_http2_frame,
                         params.time_func,
                         context.network_quality_estimator),
      http_stream_factory_(std::make_unique<HttpStreamFactory>(this)),
      params_(params),
      context_(context) {
  DCHECK(proxy_resolution_service_);
  DCHECK(ssl_config_service_);
  CHECK(http_server_properties_);

  normal_socket_pool_manager_ = std::make_unique<ClientSocketPoolManagerImpl>(
      CreateCommonConnectJobParams(false /* for_websockets */),
      CreateCommonConnectJobParams(true /* for_websockets */),
      context_.ssl_config_service, NORMAL_SOCKET_POOL);
  websocket_socket_pool_manager_ =
      std::make_unique<ClientSocketPoolManagerImpl>(
          CreateCommonConnectJobParams(false /* for_websockets */),
          CreateCommonConnectJobParams(true /* for_websockets */),
          context_.ssl_config_service, WEBSOCKET_SOCKET_POOL);

  if (params_.enable_http2)
    next_protos_.push_back(kProtoHTTP2);

  next_protos_.push_back(kProtoHTTP11);

  http_server_properties_->SetMaxServerConfigsStoredInProperties(
      params.quic_max_server_configs_stored_in_properties);

  if (!params_.disable_idle_sockets_close_on_memory_pressure) {
    memory_pressure_listener_.reset(
        new base::MemoryPressureListener(base::BindRepeating(
            &HttpNetworkSession::OnMemoryPressure, base::Unretained(this))));
  }
}

HttpNetworkSession::~HttpNetworkSession() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  response_drainers_.clear();
  // TODO(bnc): CloseAllSessions() is also called in SpdySessionPool destructor,
  // one of the two calls should be removed.
  spdy_session_pool_.CloseAllSessions();
}

void HttpNetworkSession::AddResponseDrainer(
    std::unique_ptr<HttpResponseBodyDrainer> drainer) {
  DCHECK(!base::ContainsKey(response_drainers_, drainer.get()));
  HttpResponseBodyDrainer* drainer_ptr = drainer.get();
  response_drainers_[drainer_ptr] = std::move(drainer);
}

void HttpNetworkSession::RemoveResponseDrainer(
    HttpResponseBodyDrainer* drainer) {
  DCHECK(base::ContainsKey(response_drainers_, drainer));
  response_drainers_[drainer].release();
  response_drainers_.erase(drainer);
}

ClientSocketPool* HttpNetworkSession::GetSocketPool(
    SocketPoolType pool_type,
    const ProxyServer& proxy_server) {
  return GetSocketPoolManager(pool_type)->GetSocketPool(proxy_server);
}

std::unique_ptr<base::Value> HttpNetworkSession::SocketPoolInfoToValue() const {
  // TODO(yutak): Should merge values from normal pools and WebSocket pools.
  return normal_socket_pool_manager_->SocketPoolInfoToValue();
}

std::unique_ptr<base::Value> HttpNetworkSession::SpdySessionPoolInfoToValue()
    const {
  return spdy_session_pool_.SpdySessionPoolInfoToValue();
}

std::unique_ptr<base::Value> HttpNetworkSession::QuicInfoToValue() const {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->Set("sessions", quic_stream_factory_.QuicStreamFactoryInfoToValue());
  dict->SetBoolean("quic_enabled", IsQuicEnabled());

  auto connection_options(std::make_unique<base::ListValue>());
  for (const auto& option : params_.quic_connection_options)
    connection_options->AppendString(quic::QuicTagToString(option));
  dict->Set("connection_options", std::move(connection_options));

  auto supported_versions(std::make_unique<base::ListValue>());
  for (const auto& version : params_.quic_supported_versions)
    supported_versions->AppendString(ParsedQuicVersionToString(version));
  dict->Set("supported_versions", std::move(supported_versions));

  auto origins_to_force_quic_on(std::make_unique<base::ListValue>());
  for (const auto& origin : params_.origins_to_force_quic_on)
    origins_to_force_quic_on->AppendString(origin.ToString());
  dict->Set("origins_to_force_quic_on", std::move(origins_to_force_quic_on));

  dict->SetInteger("max_packet_length", params_.quic_max_packet_length);
  dict->SetInteger("max_server_configs_stored_in_properties",
                   params_.quic_max_server_configs_stored_in_properties);
  dict->SetInteger("idle_connection_timeout_seconds",
                   params_.quic_idle_connection_timeout_seconds);
  dict->SetInteger("reduced_ping_timeout_seconds",
                   params_.quic_reduced_ping_timeout_seconds);
  dict->SetBoolean("mark_quic_broken_when_network_blackholes",
                   params_.mark_quic_broken_when_network_blackholes);
  dict->SetBoolean("retry_without_alt_svc_on_quic_errors",
                   params_.retry_without_alt_svc_on_quic_errors);
  dict->SetBoolean("race_cert_verification",
                   params_.quic_race_cert_verification);
  dict->SetBoolean("disable_bidirectional_streams",
                   params_.quic_disable_bidirectional_streams);
  dict->SetBoolean("close_sessions_on_ip_change",
                   params_.quic_close_sessions_on_ip_change);
  dict->SetBoolean("goaway_sessions_on_ip_change",
                   params_.quic_goaway_sessions_on_ip_change);
  dict->SetBoolean("migrate_sessions_on_network_change_v2",
                   params_.quic_migrate_sessions_on_network_change_v2);
  dict->SetBoolean("migrate_sessions_early_v2",
                   params_.quic_migrate_sessions_early_v2);
  dict->SetInteger("retransmittable_on_wire_timeout_milliseconds",
                   params_.quic_retransmittable_on_wire_timeout_milliseconds);
  dict->SetBoolean("retry_on_alternate_network_before_handshake",
                   params_.quic_retry_on_alternate_network_before_handshake);
  dict->SetBoolean("migrate_idle_sessions", params_.quic_migrate_idle_sessions);
  dict->SetInteger("idle_session_migration_period_seconds",
                   params_.quic_idle_session_migration_period.InSeconds());
  dict->SetInteger("max_time_on_non_default_network_seconds",
                   params_.quic_max_time_on_non_default_network.InSeconds());
  dict->SetInteger(
      "max_num_migrations_to_non_default_network_on_write_error",
      params_.quic_max_migrations_to_non_default_network_on_write_error);
  dict->SetInteger(
      "max_num_migrations_to_non_default_network_on_path_degrading",
      params_.quic_max_migrations_to_non_default_network_on_path_degrading);
  dict->SetBoolean("allow_server_migration",
                   params_.quic_allow_server_migration);
  dict->SetBoolean("race_stale_dns_on_connection",
                   params_.quic_race_stale_dns_on_connection);
  dict->SetBoolean("go_away_on_path_degrading",
                   params_.quic_go_away_on_path_degrading);
  dict->SetBoolean("estimate_initial_rtt", params_.quic_estimate_initial_rtt);
  dict->SetBoolean("server_push_cancellation",
                   params_.enable_server_push_cancellation);
  dict->SetInteger("initial_rtt_for_handshake_milliseconds",
                   params_.quic_initial_rtt_for_handshake_milliseconds);

  return std::move(dict);
}

void HttpNetworkSession::CloseAllConnections() {
  normal_socket_pool_manager_->FlushSocketPoolsWithError(ERR_ABORTED);
  websocket_socket_pool_manager_->FlushSocketPoolsWithError(ERR_ABORTED);
  spdy_session_pool_.CloseCurrentSessions(ERR_ABORTED);
  quic_stream_factory_.CloseAllSessions(ERR_ABORTED, quic::QUIC_INTERNAL_ERROR);
}

void HttpNetworkSession::CloseIdleConnections() {
  normal_socket_pool_manager_->CloseIdleSockets();
  websocket_socket_pool_manager_->CloseIdleSockets();
  spdy_session_pool_.CloseCurrentIdleSessions();
}

bool HttpNetworkSession::IsProtocolEnabled(NextProto protocol) const {
  switch (protocol) {
    case kProtoUnknown:
      NOTREACHED();
      return false;
    case kProtoHTTP11:
      return true;
    case kProtoHTTP2:
      return params_.enable_http2;
    case kProtoQUIC:
      return IsQuicEnabled();
  }
  NOTREACHED();
  return false;
}

void HttpNetworkSession::SetServerPushDelegate(
    std::unique_ptr<ServerPushDelegate> push_delegate) {
  DCHECK(push_delegate);
  if (!params_.enable_server_push_cancellation || push_delegate_)
    return;

  push_delegate_ = std::move(push_delegate);
  spdy_session_pool_.set_server_push_delegate(push_delegate_.get());
  quic_stream_factory_.set_server_push_delegate(push_delegate_.get());
}

void HttpNetworkSession::GetAlpnProtos(NextProtoVector* alpn_protos) const {
  *alpn_protos = next_protos_;
}

void HttpNetworkSession::GetSSLConfig(const HttpRequestInfo& request,
                                      SSLConfig* server_config,
                                      SSLConfig* proxy_config) const {
  ssl_config_service_->GetSSLConfig(server_config);
  GetAlpnProtos(&server_config->alpn_protos);
  server_config->ignore_certificate_errors = params_.ignore_certificate_errors;
  *proxy_config = *server_config;
  server_config->early_data_enabled = params_.enable_early_data;
}

void HttpNetworkSession::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {
  std::string name = base::StringPrintf("net/http_network_session_0x%" PRIxPTR,
                                        reinterpret_cast<uintptr_t>(this));
  base::trace_event::MemoryAllocatorDump* http_network_session_dump =
      pmd->GetAllocatorDump(name);
  if (http_network_session_dump == nullptr) {
    http_network_session_dump = pmd->CreateAllocatorDump(name);
    normal_socket_pool_manager_->DumpMemoryStats(
        pmd, http_network_session_dump->absolute_name());
    spdy_session_pool_.DumpMemoryStats(
        pmd, http_network_session_dump->absolute_name());
    if (http_stream_factory_) {
      http_stream_factory_->DumpMemoryStats(
          pmd, http_network_session_dump->absolute_name());
    }
    quic_stream_factory_.DumpMemoryStats(
        pmd, http_network_session_dump->absolute_name());
    ssl_client_session_cache_.DumpMemoryStats(pmd, name);
  }

  // Create an empty row under parent's dump so size can be attributed correctly
  // if |this| is shared between URLRequestContexts.
  base::trace_event::MemoryAllocatorDump* empty_row_dump =
      pmd->CreateAllocatorDump(base::StringPrintf(
          "%s/http_network_session", parent_absolute_name.c_str()));
  pmd->AddOwnershipEdge(empty_row_dump->guid(),
                        http_network_session_dump->guid());
}

bool HttpNetworkSession::IsQuicEnabled() const {
  return params_.enable_quic;
}

void HttpNetworkSession::DisableQuic() {
  params_.enable_quic = false;
}

void HttpNetworkSession::ClearSSLSessionCache() {
  ssl_client_session_cache_.Flush();
  ssl_client_session_cache_privacy_mode_.Flush();
}

CommonConnectJobParams HttpNetworkSession::CreateCommonConnectJobParams(
    bool for_websockets) {
  // Use null websocket_endpoint_lock_manager, which is only set for WebSockets,
  // and only when not using a proxy.
  return CommonConnectJobParams(
      context_.client_socket_factory ? context_.client_socket_factory
                                     : ClientSocketFactory::GetDefaultFactory(),
      context_.host_resolver, &http_auth_cache_,
      context_.http_auth_handler_factory, &spdy_session_pool_,
      &params_.quic_supported_versions, &quic_stream_factory_,
      context_.proxy_delegate, context_.http_user_agent_settings,
      CreateClientSocketContext(context_, &ssl_client_session_cache_),
      CreateClientSocketContext(context_,
                                &ssl_client_session_cache_privacy_mode_),
      context_.socket_performance_watcher_factory,
      context_.network_quality_estimator, context_.net_log,
      for_websockets ? &websocket_endpoint_lock_manager_ : nullptr);
}

ClientSocketPoolManager* HttpNetworkSession::GetSocketPoolManager(
    SocketPoolType pool_type) {
  switch (pool_type) {
    case NORMAL_SOCKET_POOL:
      return normal_socket_pool_manager_.get();
    case WEBSOCKET_SOCKET_POOL:
      return websocket_socket_pool_manager_.get();
    default:
      NOTREACHED();
      break;
  }
  return nullptr;
}

void HttpNetworkSession::OnMemoryPressure(
    base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level) {
  DCHECK(!params_.disable_idle_sockets_close_on_memory_pressure);

  switch (memory_pressure_level) {
    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE:
      break;

    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE:
    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL:
      CloseIdleConnections();
      break;
  }
}

}  // namespace net
