// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_factory.h"

#include <utility>

#include "base/lazy_instance.h"
#include "build/build_config.h"
#include "net/cert/cert_database.h"
#include "net/http/http_proxy_client_socket.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/ssl_client_socket_impl.h"
#include "net/socket/tcp_client_socket.h"
#include "net/socket/udp_client_socket.h"

namespace net {

class X509Certificate;

namespace {

class DefaultClientSocketFactory : public ClientSocketFactory,
                                   public CertDatabase::Observer {
 public:
  DefaultClientSocketFactory() {
    CertDatabase::GetInstance()->AddObserver(this);
  }

  ~DefaultClientSocketFactory() override {
    // Note: This code never runs, as the factory is defined as a Leaky
    // singleton.
    CertDatabase::GetInstance()->RemoveObserver(this);
  }

  void OnCertDBChanged() override {
    // Flush sockets whenever CA trust changes.
    ClearSSLSessionCache();
  }

  std::unique_ptr<DatagramClientSocket> CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      NetLog* net_log,
      const NetLogSource& source) override {
    return std::unique_ptr<DatagramClientSocket>(
        new UDPClientSocket(bind_type, net_log, source));
  }

  std::unique_ptr<TransportClientSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log,
      const NetLogSource& source) override {
    return std::make_unique<TCPClientSocket>(
        addresses, std::move(socket_performance_watcher), net_log, source);
  }

  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      const SSLClientSocketContext& context) override {
    return std::unique_ptr<SSLClientSocket>(new SSLClientSocketImpl(
        std::move(transport_socket), host_and_port, ssl_config, context));
  }

  std::unique_ptr<ProxyClientSocket> CreateProxyClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      HttpAuthController* http_auth_controller,
      bool tunnel,
      bool using_spdy,
      NextProto negotiated_protocol,
      bool is_https_proxy,
      const NetworkTrafficAnnotationTag& traffic_annotation) override {
    return std::make_unique<HttpProxyClientSocket>(
        std::move(transport_socket), user_agent, endpoint, http_auth_controller,
        tunnel, using_spdy, negotiated_protocol, is_https_proxy,
        traffic_annotation);
  }

  void ClearSSLSessionCache() override { SSLClientSocket::ClearSessionCache(); }
};

static base::LazyInstance<DefaultClientSocketFactory>::Leaky
    g_default_client_socket_factory = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
ClientSocketFactory* ClientSocketFactory::GetDefaultFactory() {
  return g_default_client_socket_factory.Pointer();
}

}  // namespace net
