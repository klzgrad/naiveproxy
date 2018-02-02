// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_MOCK_CLIENT_SOCKET_POOL_MANAGER_H_
#define NET_SOCKET_MOCK_CLIENT_SOCKET_POOL_MANAGER_H_

#include <string>

#include "base/macros.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/client_socket_pool_manager_impl.h"

namespace net {

class MockClientSocketPoolManager : public ClientSocketPoolManager {
 public:
  MockClientSocketPoolManager();
  ~MockClientSocketPoolManager() override;

  // Sets "override" socket pools that get used instead.
  void SetTransportSocketPool(TransportClientSocketPool* pool);
  void SetSSLSocketPool(SSLClientSocketPool* pool);
  void SetSocketPoolForSOCKSProxy(const HostPortPair& socks_proxy,
                                  std::unique_ptr<SOCKSClientSocketPool> pool);
  void SetSocketPoolForHTTPProxy(
      const HostPortPair& http_proxy,
      std::unique_ptr<HttpProxyClientSocketPool> pool);
  void SetSocketPoolForSSLWithProxy(const HostPortPair& proxy_server,
                                    std::unique_ptr<SSLClientSocketPool> pool);

  // ClientSocketPoolManager methods:
  void FlushSocketPoolsWithError(int error) override;
  void CloseIdleSockets() override;
  TransportClientSocketPool* GetTransportSocketPool() override;
  SSLClientSocketPool* GetSSLSocketPool() override;
  SOCKSClientSocketPool* GetSocketPoolForSOCKSProxy(
      const HostPortPair& socks_proxy) override;
  HttpProxyClientSocketPool* GetSocketPoolForHTTPProxy(
      const HostPortPair& http_proxy) override;
  SSLClientSocketPool* GetSocketPoolForSSLWithProxy(
      const HostPortPair& proxy_server) override;
  std::unique_ptr<base::Value> SocketPoolInfoToValue() const override;
  void DumpMemoryStats(
      base::trace_event::ProcessMemoryDump* pmd,
      const std::string& parent_dump_absolute_name) const override;

 private:
  using TransportSocketPoolMap =
      std::map<HostPortPair, std::unique_ptr<TransportClientSocketPool>>;
  using SOCKSSocketPoolMap =
      std::map<HostPortPair, std::unique_ptr<SOCKSClientSocketPool>>;
  using HTTPProxySocketPoolMap =
      std::map<HostPortPair, std::unique_ptr<HttpProxyClientSocketPool>>;
  using SSLSocketPoolMap =
      std::map<HostPortPair, std::unique_ptr<SSLClientSocketPool>>;

  std::unique_ptr<TransportClientSocketPool> transport_socket_pool_;
  std::unique_ptr<SSLClientSocketPool> ssl_socket_pool_;
  SOCKSSocketPoolMap socks_socket_pools_;
  HTTPProxySocketPoolMap http_proxy_socket_pools_;
  SSLSocketPoolMap ssl_socket_pools_for_proxies_;

  DISALLOW_COPY_AND_ASSIGN(MockClientSocketPoolManager);
};

}  // namespace net

#endif  // NET_SOCKET_MOCK_CLIENT_SOCKET_POOL_MANAGER_H_
