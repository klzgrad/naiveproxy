// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_IMPL_H_
#define NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <type_traits>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "net/base/net_export.h"
#include "net/cert/cert_database.h"
#include "net/http/http_network_session.h"
#include "net/socket/client_socket_pool_manager.h"

namespace base {
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace net {

class CertVerifier;
class ChannelIDService;
class ClientSocketFactory;
class CTVerifier;
class HostResolver;
class NetLog;
class NetworkQualityEstimator;
class ProxyDelegate;
class ProxyServer;
class SocketPerformanceWatcherFactory;
class SSLClientSessionCache;
class SSLConfigService;
class TransportClientSocketPool;
class TransportSecurityState;
class WebSocketEndpointLockManager;

class NET_EXPORT_PRIVATE ClientSocketPoolManagerImpl
    : public ClientSocketPoolManager,
      public CertDatabase::Observer {
 public:
  ClientSocketPoolManagerImpl(
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
      HttpNetworkSession::SocketPoolType pool_type);
  ~ClientSocketPoolManagerImpl() override;

  void FlushSocketPoolsWithError(int error) override;
  void CloseIdleSockets() override;

  TransportClientSocketPool* GetSocketPool(
      const ProxyServer& proxy_server) override;

  // Creates a Value summary of the state of the socket pools.
  std::unique_ptr<base::Value> SocketPoolInfoToValue() const override;

  // CertDatabase::Observer methods:
  void OnCertDBChanged() override;

  void DumpMemoryStats(
      base::trace_event::ProcessMemoryDump* pmd,
      const std::string& parent_dump_absolute_name) const override;

 private:
  using TransportSocketPoolMap =
      std::map<ProxyServer, std::unique_ptr<TransportClientSocketPool>>;

  NetLog* const net_log_;
  ClientSocketFactory* const socket_factory_;
  SocketPerformanceWatcherFactory* socket_performance_watcher_factory_;
  NetworkQualityEstimator* network_quality_estimator_;
  HostResolver* const host_resolver_;
  CertVerifier* const cert_verifier_;
  ChannelIDService* const channel_id_service_;
  TransportSecurityState* const transport_security_state_;
  CTVerifier* const cert_transparency_verifier_;
  CTPolicyEnforcer* const ct_policy_enforcer_;
  SSLClientSessionCache* const ssl_client_session_cache_;
  SSLClientSessionCache* const ssl_client_session_cache_privacy_mode_;
  const std::string ssl_session_cache_shard_;
  SSLConfigService* const ssl_config_service_;
  WebSocketEndpointLockManager* const websocket_endpoint_lock_manager_;
  ProxyDelegate* const proxy_delegate_;
  const HttpNetworkSession::SocketPoolType pool_type_;

  TransportSocketPoolMap socket_pools_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(ClientSocketPoolManagerImpl);
};

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_IMPL_H_
