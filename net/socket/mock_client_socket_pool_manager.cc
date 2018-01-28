// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/mock_client_socket_pool_manager.h"

#include "base/values.h"
#include "net/http/http_proxy_client_socket_pool.h"
#include "net/socket/socks_client_socket_pool.h"
#include "net/socket/ssl_client_socket_pool.h"
#include "net/socket/transport_client_socket_pool.h"

namespace net {

MockClientSocketPoolManager::MockClientSocketPoolManager() {}
MockClientSocketPoolManager::~MockClientSocketPoolManager() {}

void MockClientSocketPoolManager::SetTransportSocketPool(
    TransportClientSocketPool* pool) {
  transport_socket_pool_.reset(pool);
}

void MockClientSocketPoolManager::SetSSLSocketPool(
    SSLClientSocketPool* pool) {
  ssl_socket_pool_.reset(pool);
}

void MockClientSocketPoolManager::SetSocketPoolForSOCKSProxy(
    const HostPortPair& socks_proxy,
    std::unique_ptr<SOCKSClientSocketPool> pool) {
  socks_socket_pools_[socks_proxy] = std::move(pool);
}

void MockClientSocketPoolManager::SetSocketPoolForHTTPProxy(
    const HostPortPair& http_proxy,
    std::unique_ptr<HttpProxyClientSocketPool> pool) {
  http_proxy_socket_pools_[http_proxy] = std::move(pool);
}

void MockClientSocketPoolManager::SetSocketPoolForSSLWithProxy(
    const HostPortPair& proxy_server,
    std::unique_ptr<SSLClientSocketPool> pool) {
  ssl_socket_pools_for_proxies_[proxy_server] = std::move(pool);
}

void MockClientSocketPoolManager::FlushSocketPoolsWithError(int error) {
  NOTIMPLEMENTED();
}

void MockClientSocketPoolManager::CloseIdleSockets() {
  NOTIMPLEMENTED();
}

TransportClientSocketPool*
MockClientSocketPoolManager::GetTransportSocketPool() {
  return transport_socket_pool_.get();
}

SSLClientSocketPool* MockClientSocketPoolManager::GetSSLSocketPool() {
  return ssl_socket_pool_.get();
}

SOCKSClientSocketPool* MockClientSocketPoolManager::GetSocketPoolForSOCKSProxy(
    const HostPortPair& socks_proxy) {
  SOCKSSocketPoolMap::const_iterator it = socks_socket_pools_.find(socks_proxy);
  if (it != socks_socket_pools_.end())
    return it->second.get();
  return nullptr;
}

HttpProxyClientSocketPool*
MockClientSocketPoolManager::GetSocketPoolForHTTPProxy(
    const HostPortPair& http_proxy) {
  HTTPProxySocketPoolMap::const_iterator it =
      http_proxy_socket_pools_.find(http_proxy);
  if (it != http_proxy_socket_pools_.end())
    return it->second.get();
  return nullptr;
}

SSLClientSocketPool* MockClientSocketPoolManager::GetSocketPoolForSSLWithProxy(
    const HostPortPair& proxy_server) {
  SSLSocketPoolMap::const_iterator it =
      ssl_socket_pools_for_proxies_.find(proxy_server);
  if (it != ssl_socket_pools_for_proxies_.end())
    return it->second.get();
  return nullptr;
}

std::unique_ptr<base::Value>
MockClientSocketPoolManager::SocketPoolInfoToValue() const {
  NOTIMPLEMENTED();
  return std::unique_ptr<base::Value>(nullptr);
}

void MockClientSocketPoolManager::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_dump_absolute_name) const {}

}  // namespace net
