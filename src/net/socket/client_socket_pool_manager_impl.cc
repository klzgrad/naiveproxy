// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_pool_manager_impl.h"

#include <algorithm>
#include <utility>

#include "base/logging.h"
#include "base/values.h"
#include "net/base/proxy_server.h"
#include "net/http/http_network_session.h"
#include "net/socket/socks_connect_job.h"
#include "net/socket/ssl_connect_job.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/socket/transport_connect_job.h"
#include "net/socket/websocket_transport_client_socket_pool.h"
#include "net/ssl/ssl_config_service.h"

namespace net {

class SocketPerformanceWatcherFactory;

ClientSocketPoolManagerImpl::ClientSocketPoolManagerImpl(
    NetLog* net_log,
    ClientSocketFactory* socket_factory,
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
    NetworkQualityEstimator* network_quality_estimator,
    HostResolver* host_resolver,
    CertVerifier* cert_verifier,
    ChannelIDService* channel_id_service,
    TransportSecurityState* transport_security_state,
    CTVerifier* cert_transparency_verifier,
    CTPolicyEnforcer* ct_policy_enforcer,
    SSLClientSessionCache* ssl_client_session_cache,
    SSLClientSessionCache* ssl_client_session_cache_privacy_mode,
    SSLConfigService* ssl_config_service,
    WebSocketEndpointLockManager* websocket_endpoint_lock_manager,
    ProxyDelegate* proxy_delegate,
    HttpNetworkSession::SocketPoolType pool_type)
    : net_log_(net_log),
      socket_factory_(socket_factory),
      socket_performance_watcher_factory_(socket_performance_watcher_factory),
      network_quality_estimator_(network_quality_estimator),
      host_resolver_(host_resolver),
      cert_verifier_(cert_verifier),
      channel_id_service_(channel_id_service),
      transport_security_state_(transport_security_state),
      cert_transparency_verifier_(cert_transparency_verifier),
      ct_policy_enforcer_(ct_policy_enforcer),
      ssl_client_session_cache_(ssl_client_session_cache),
      ssl_client_session_cache_privacy_mode_(
          ssl_client_session_cache_privacy_mode),
      ssl_config_service_(ssl_config_service),
      websocket_endpoint_lock_manager_(websocket_endpoint_lock_manager),
      proxy_delegate_(proxy_delegate),
      pool_type_(pool_type) {
  CertDatabase::GetInstance()->AddObserver(this);
}

ClientSocketPoolManagerImpl::~ClientSocketPoolManagerImpl() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CertDatabase::GetInstance()->RemoveObserver(this);
}

void ClientSocketPoolManagerImpl::FlushSocketPoolsWithError(int error) {
  for (const auto& it : socket_pools_) {
    it.second->FlushWithError(error);
  }
}

void ClientSocketPoolManagerImpl::CloseIdleSockets() {
  for (const auto& it : socket_pools_) {
    it.second->CloseIdleSockets();
  }
}

TransportClientSocketPool* ClientSocketPoolManagerImpl::GetSocketPool(
    const ProxyServer& proxy_server) {
  TransportSocketPoolMap::const_iterator it = socket_pools_.find(proxy_server);
  if (it != socket_pools_.end())
    return it->second.get();

  int sockets_per_proxy_server;
  int sockets_per_group;
  if (proxy_server.is_direct()) {
    sockets_per_proxy_server = max_sockets_per_pool(pool_type_);
    sockets_per_group = max_sockets_per_group(pool_type_);
  } else {
    sockets_per_proxy_server = max_sockets_per_proxy_server(pool_type_);
    sockets_per_group =
        std::min(sockets_per_proxy_server, max_sockets_per_group(pool_type_));
  }

  std::unique_ptr<TransportClientSocketPool> new_pool;

  // Use specialized WebSockets pool for WebSockets when no proxy is in use.
  if (pool_type_ == HttpNetworkSession::WEBSOCKET_SOCKET_POOL &&
      proxy_server.is_direct()) {
    new_pool = std::make_unique<WebSocketTransportClientSocketPool>(
        sockets_per_proxy_server, sockets_per_group,
        unused_idle_socket_timeout(pool_type_), socket_factory_, host_resolver_,
        proxy_delegate_, cert_verifier_, channel_id_service_,
        transport_security_state_, cert_transparency_verifier_,
        ct_policy_enforcer_, ssl_client_session_cache_,
        ssl_client_session_cache_privacy_mode_, ssl_config_service_,
        network_quality_estimator_, websocket_endpoint_lock_manager_, net_log_);
  } else {
    new_pool = std::make_unique<TransportClientSocketPool>(
        sockets_per_proxy_server, sockets_per_group,
        unused_idle_socket_timeout(pool_type_), socket_factory_, host_resolver_,
        proxy_delegate_, cert_verifier_, channel_id_service_,
        transport_security_state_, cert_transparency_verifier_,
        ct_policy_enforcer_, ssl_client_session_cache_,
        ssl_client_session_cache_privacy_mode_, ssl_config_service_,
        socket_performance_watcher_factory_, network_quality_estimator_,
        net_log_);
  }

  std::pair<TransportSocketPoolMap::iterator, bool> ret =
      socket_pools_.insert(std::make_pair(proxy_server, std::move(new_pool)));
  return ret.first->second.get();
}

std::unique_ptr<base::Value>
ClientSocketPoolManagerImpl::SocketPoolInfoToValue() const {
  std::unique_ptr<base::ListValue> list(new base::ListValue());
  for (const auto& socket_pool : socket_pools_) {
    // TODO(menke): Is this really needed?
    const char* type;
    if (socket_pool.first.is_direct()) {
      type = "transport_socket_pool";
    } else if (socket_pool.first.is_socks()) {
      type = "socks_socket_pool";
    } else {
      type = "http_proxy_socket_pool";
    }
    list->Append(
        socket_pool.second->GetInfoAsValue(socket_pool.first.ToURI(), type));
  }

  return std::move(list);
}

void ClientSocketPoolManagerImpl::OnCertDBChanged() {
  FlushSocketPoolsWithError(ERR_NETWORK_CHANGED);
}

void ClientSocketPoolManagerImpl::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_dump_absolute_name) const {
  TransportSocketPoolMap::const_iterator socket_pool =
      socket_pools_.find(ProxyServer::Direct());
  if (socket_pool == socket_pools_.end())
    return;
  socket_pool->second->DumpMemoryStats(pmd, parent_dump_absolute_name);
}

}  // namespace net
