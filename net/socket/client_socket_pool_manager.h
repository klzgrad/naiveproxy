// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ClientSocketPoolManager manages access to all ClientSocketPools.  It's a
// simple container for all of them.  Most importantly, it handles the lifetime
// and destruction order properly.

#ifndef NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_H_
#define NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_H_

#include <string>

#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/http/http_network_session.h"

namespace base {
class Value;
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace net {

typedef base::Callback<int(const AddressList&, const NetLogWithSource& net_log)>
    OnHostResolutionCallback;

class ClientSocketHandle;
class HostPortPair;
class HttpNetworkSession;
class HttpProxyClientSocketPool;
class HttpRequestHeaders;
class NetLogWithSource;
class ProxyInfo;
class TransportClientSocketPool;
class SOCKSClientSocketPool;
class SSLClientSocketPool;

struct SSLConfig;

// This should rather be a simple constant but Windows shared libs doesn't
// really offer much flexiblity in exporting contants.
enum DefaultMaxValues { kDefaultMaxSocketsPerProxyServer = 32 };

class NET_EXPORT_PRIVATE ClientSocketPoolManager {
 public:
  enum SocketGroupType {
    SSL_GROUP,     // For all TLS sockets.
    NORMAL_GROUP,  // For normal HTTP sockets.
    FTP_GROUP      // For FTP sockets (over an HTTP proxy).
  };

  ClientSocketPoolManager();
  virtual ~ClientSocketPoolManager();

  // The setter methods below affect only newly created socket pools after the
  // methods are called. Normally they should be called at program startup
  // before any ClientSocketPoolManagerImpl is created.
  static int max_sockets_per_pool(HttpNetworkSession::SocketPoolType pool_type);
  static void set_max_sockets_per_pool(
      HttpNetworkSession::SocketPoolType pool_type,
      int socket_count);

  static int max_sockets_per_group(
      HttpNetworkSession::SocketPoolType pool_type);
  static void set_max_sockets_per_group(
      HttpNetworkSession::SocketPoolType pool_type,
      int socket_count);

  static int max_sockets_per_proxy_server(
      HttpNetworkSession::SocketPoolType pool_type);
  static void set_max_sockets_per_proxy_server(
      HttpNetworkSession::SocketPoolType pool_type,
      int socket_count);

  virtual void FlushSocketPoolsWithError(int error) = 0;
  virtual void CloseIdleSockets() = 0;
  virtual TransportClientSocketPool* GetTransportSocketPool() = 0;
  virtual SSLClientSocketPool* GetSSLSocketPool() = 0;
  virtual SOCKSClientSocketPool* GetSocketPoolForSOCKSProxy(
      const HostPortPair& socks_proxy) = 0;
  virtual HttpProxyClientSocketPool* GetSocketPoolForHTTPProxy(
      const HostPortPair& http_proxy) = 0;
  virtual SSLClientSocketPool* GetSocketPoolForSSLWithProxy(
      const HostPortPair& proxy_server) = 0;
  // Creates a Value summary of the state of the socket pools.
  virtual std::unique_ptr<base::Value> SocketPoolInfoToValue() const = 0;

  // Dumps memory allocation stats. |parent_dump_absolute_name| is the name
  // used by the parent MemoryAllocatorDump in the memory dump hierarchy.
  virtual void DumpMemoryStats(
      base::trace_event::ProcessMemoryDump* pmd,
      const std::string& parent_dump_absolute_name) const = 0;
};

// A helper method that uses the passed in proxy information to initialize a
// ClientSocketHandle with the relevant socket pool. Use this method for
// HTTP/HTTPS requests. |ssl_config_for_origin| is only used if the request
// uses SSL and |ssl_config_for_proxy| is used if the proxy server is HTTPS.
// |resolution_callback| will be invoked after the the hostname is
// resolved.  If |resolution_callback| does not return OK, then the
// connection will be aborted with that value.
// If |expect_spdy| is true, then after the SSL handshake is complete,
// SPDY must have been negotiated or else it will be considered an error.
int InitSocketHandleForHttpRequest(
    ClientSocketPoolManager::SocketGroupType group_type,
    const HostPortPair& endpoint,
    const HttpRequestHeaders& request_extra_headers,
    int request_load_flags,
    RequestPriority request_priority,
    HttpNetworkSession* session,
    const ProxyInfo& proxy_info,
    bool expect_spdy,
    const SSLConfig& ssl_config_for_origin,
    const SSLConfig& ssl_config_for_proxy,
    PrivacyMode privacy_mode,
    const NetLogWithSource& net_log,
    ClientSocketHandle* socket_handle,
    const OnHostResolutionCallback& resolution_callback,
    const CompletionCallback& callback);

// A helper method that uses the passed in proxy information to initialize a
// ClientSocketHandle with the relevant socket pool. Use this method for
// HTTP/HTTPS requests for WebSocket handshake.
// |ssl_config_for_origin| is only used if the request
// uses SSL and |ssl_config_for_proxy| is used if the proxy server is HTTPS.
// |resolution_callback| will be invoked after the the hostname is
// resolved.  If |resolution_callback| does not return OK, then the
// connection will be aborted with that value.
// This function uses WEBSOCKET_SOCKET_POOL socket pools.
int InitSocketHandleForWebSocketRequest(
    ClientSocketPoolManager::SocketGroupType group_type,
    const HostPortPair& endpoint,
    const HttpRequestHeaders& request_extra_headers,
    int request_load_flags,
    RequestPriority request_priority,
    HttpNetworkSession* session,
    const ProxyInfo& proxy_info,
    bool expect_spdy,
    const SSLConfig& ssl_config_for_origin,
    const SSLConfig& ssl_config_for_proxy,
    PrivacyMode privacy_mode,
    const NetLogWithSource& net_log,
    ClientSocketHandle* socket_handle,
    const OnHostResolutionCallback& resolution_callback,
    const CompletionCallback& callback);

// A helper method that uses the passed in proxy information to initialize a
// ClientSocketHandle with the relevant socket pool. Use this method for
// a raw socket connection to a host-port pair (that needs to tunnel through
// the proxies).
NET_EXPORT int InitSocketHandleForRawConnect(
    const HostPortPair& host_port_pair,
    HttpNetworkSession* session,
    const ProxyInfo& proxy_info,
    const SSLConfig& ssl_config_for_origin,
    const SSLConfig& ssl_config_for_proxy,
    PrivacyMode privacy_mode,
    const NetLogWithSource& net_log,
    ClientSocketHandle* socket_handle,
    const CompletionCallback& callback);

// A helper method that uses the passed in proxy information to initialize a
// ClientSocketHandle with the relevant socket pool. Use this method for
// a raw socket connection with TLS negotiation to a host-port pair (that needs
// to tunnel through the proxies).
NET_EXPORT int InitSocketHandleForTlsConnect(
    const HostPortPair& host_port_pair,
    HttpNetworkSession* session,
    const ProxyInfo& proxy_info,
    const SSLConfig& ssl_config_for_origin,
    const SSLConfig& ssl_config_for_proxy,
    PrivacyMode privacy_mode,
    const NetLogWithSource& net_log,
    ClientSocketHandle* socket_handle,
    const CompletionCallback& callback);

// Similar to InitSocketHandleForHttpRequest except that it initiates the
// desired number of preconnect streams from the relevant socket pool.
int PreconnectSocketsForHttpRequest(
    ClientSocketPoolManager::SocketGroupType group_type,
    const HostPortPair& endpoint,
    const HttpRequestHeaders& request_extra_headers,
    int request_load_flags,
    RequestPriority request_priority,
    HttpNetworkSession* session,
    const ProxyInfo& proxy_info,
    bool expect_spdy,
    const SSLConfig& ssl_config_for_origin,
    const SSLConfig& ssl_config_for_proxy,
    PrivacyMode privacy_mode,
    const NetLogWithSource& net_log,
    int num_preconnect_streams,
    HttpRequestInfo::RequestMotivation motivation);

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_POOL_MANAGER_H_
