// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_factory.h"

#include <utility>

#include "base/lazy_instance.h"
#include "build/build_config.h"
#include "net/cert/cert_database.h"
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
      const RandIntCallback& rand_int_cb,
      NetLog* net_log,
      const NetLogSource& source) override {
    return std::unique_ptr<DatagramClientSocket>(
        new UDPClientSocket(bind_type, rand_int_cb, net_log, source));
  }

  std::unique_ptr<StreamSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log,
      const NetLogSource& source) override {
    return std::unique_ptr<StreamSocket>(new TCPClientSocket(
        addresses, std::move(socket_performance_watcher), net_log, source));
  }

  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      const SSLClientSocketContext& context) override {
    return std::unique_ptr<SSLClientSocket>(new SSLClientSocketImpl(
        std::move(transport_socket), host_and_port, ssl_config, context));
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
