// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CLIENT_SOCKET_FACTORY_H_
#define NET_SOCKET_CLIENT_SOCKET_FACTORY_H_

#include <memory>
#include <string>

#include "net/base/net_export.h"
#include "net/http/proxy_client_socket.h"
#include "net/socket/datagram_socket.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/socket/transport_client_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class AddressList;
class ClientSocketHandle;
class DatagramClientSocket;
class HostPortPair;
class NetLog;
struct NetLogSource;
class SSLClientSocket;
struct SSLClientSocketContext;
struct SSLConfig;
class ProxyClientSocket;
class HttpAuthController;

// An interface used to instantiate StreamSocket objects.  Used to facilitate
// testing code with mock socket implementations.
class NET_EXPORT ClientSocketFactory {
 public:
  virtual ~ClientSocketFactory() {}

  // |source| is the NetLogSource for the entity trying to create the socket,
  // if it has one.
  virtual std::unique_ptr<DatagramClientSocket> CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      NetLog* net_log,
      const NetLogSource& source) = 0;

  virtual std::unique_ptr<TransportClientSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log,
      const NetLogSource& source) = 0;

  // It is allowed to pass in a |transport_socket| that is not obtained from a
  // socket pool. The caller could create a ClientSocketHandle directly and call
  // set_socket() on it to set a valid StreamSocket instance.
  virtual std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      const SSLClientSocketContext& context) = 0;

  virtual std::unique_ptr<ProxyClientSocket> CreateProxyClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      HttpAuthController* http_auth_controller,
      bool tunnel,
      bool using_spdy,
      NextProto negotiated_protocol,
      bool is_https_proxy,
      const NetworkTrafficAnnotationTag& traffic_annotation) = 0;

  // Clears cache used for SSL session resumption.
  virtual void ClearSSLSessionCache() = 0;

  // Returns the default ClientSocketFactory.
  static ClientSocketFactory* GetDefaultFactory();
};

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_FACTORY_H_
