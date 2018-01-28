// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_WRAPPER_H_
#define NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_WRAPPER_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_timing_info.h"
#include "net/http/http_auth_controller.h"
#include "net/http/proxy_client_socket.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/next_proto.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/ssl_client_socket_pool.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/spdy/chromium/spdy_session.h"

namespace net {

class ClientSocketHandle;
class IOBuffer;
class HttpAuthCache;
class HttpResponseInfo;
class HttpStream;
class IOBuffer;
class ProxyDelegate;
class SpdySessionPool;
class SSLClientSocketPool;
class TransportClientSocketPool;

// Class that establishes connections by calling into the lower layer socket
// pools, creates a HttpProxyClientSocket / SpdyProxyClientSocket, and then
// wraps the resulting socket.
//
// This class is needed to handle auth state across multiple connection.  On
// auth challenge, this class retains auth state in its AuthController, and can
// either send the auth response to the old connection, or establish a new
// connection and send the response there.
//
// TODO(mmenke): Ideally, we'd have a central location store auth state across
// multiple connections to the same server instead.
class HttpProxyClientSocketWrapper : public ProxyClientSocket {
 public:
  HttpProxyClientSocketWrapper(
      const std::string& group_name,
      RequestPriority priority,
      ClientSocketPool::RespectLimits respect_limits,
      base::TimeDelta connect_timeout_duration,
      base::TimeDelta proxy_negotiation_timeout_duration,
      TransportClientSocketPool* transport_pool,
      SSLClientSocketPool* ssl_pool,
      const scoped_refptr<TransportSocketParams>& transport_params,
      const scoped_refptr<SSLSocketParams>& ssl_params,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      HttpAuthCache* http_auth_cache,
      HttpAuthHandlerFactory* http_auth_handler_factory,
      SpdySessionPool* spdy_session_pool,
      bool tunnel,
      ProxyDelegate* proxy_delegate,
      const NetLogWithSource& net_log);

  // On destruction Disconnect() is called.
  ~HttpProxyClientSocketWrapper() override;

  // Returns load state while establishing a connection.  Returns
  // LOAD_STATE_IDLE at other times.
  LoadState GetConnectLoadState() const;

  std::unique_ptr<HttpResponseInfo> GetAdditionalErrorState();

  // ProxyClientSocket implementation.
  const HttpResponseInfo* GetConnectResponseInfo() const override;
  std::unique_ptr<HttpStream> CreateConnectResponseStream() override;
  int RestartWithAuth(const CompletionCallback& callback) override;
  const scoped_refptr<HttpAuthController>& GetAuthController() const override;
  bool IsUsingSpdy() const override;
  NextProto GetProxyNegotiatedProtocol() const override;

  // StreamSocket implementation.
  int Connect(const CompletionCallback& callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  const NetLogWithSource& NetLog() const override;
  void SetSubresourceSpeculation() override;
  void SetOmniboxSpeculation() override;
  bool WasEverUsed() const override;
  bool WasAlpnNegotiated() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* ssl_info) override;
  void GetConnectionAttempts(ConnectionAttempts* out) const override;
  void ClearConnectionAttempts() override;
  void AddConnectionAttempts(const ConnectionAttempts& attempts) override;
  int64_t GetTotalReceivedBytes() const override;

  // Socket implementation.
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override;
  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override;
  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;
  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;

 private:
  enum State {
    STATE_BEGIN_CONNECT,
    STATE_TCP_CONNECT,
    STATE_TCP_CONNECT_COMPLETE,
    STATE_SSL_CONNECT,
    STATE_SSL_CONNECT_COMPLETE,
    STATE_HTTP_PROXY_CONNECT,
    STATE_HTTP_PROXY_CONNECT_COMPLETE,
    STATE_SPDY_PROXY_CREATE_STREAM,
    STATE_SPDY_PROXY_CREATE_STREAM_COMPLETE,
    STATE_SPDY_PROXY_CONNECT_COMPLETE,
    STATE_RESTART_WITH_AUTH,
    STATE_RESTART_WITH_AUTH_COMPLETE,
    STATE_NONE,
  };

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  // Determine if need to go through TCP or SSL path.
  int DoBeginConnect();
  // Connecting to HTTP Proxy
  int DoTransportConnect();
  int DoTransportConnectComplete(int result);
  // Connecting to HTTPS Proxy
  int DoSSLConnect();
  int DoSSLConnectComplete(int result);

  int DoHttpProxyConnect();
  int DoHttpProxyConnectComplete(int result);

  int DoSpdyProxyCreateStream();
  int DoSpdyProxyCreateStreamComplete(int result);

  int DoRestartWithAuth();
  int DoRestartWithAuthComplete(int result);

  void NotifyProxyDelegateOfCompletion(int result);

  void SetConnectTimer(base::TimeDelta duration);
  void ConnectTimeout();

  const HostResolver::RequestInfo& GetDestination();

  State next_state_;

  const std::string group_name_;
  RequestPriority priority_;
  ClientSocketPool::RespectLimits respect_limits_;
  const base::TimeDelta connect_timeout_duration_;
  const base::TimeDelta proxy_negotiation_timeout_duration_;

  TransportClientSocketPool* const transport_pool_;
  SSLClientSocketPool* const ssl_pool_;
  const scoped_refptr<TransportSocketParams> transport_params_;
  const scoped_refptr<SSLSocketParams> ssl_params_;

  const std::string user_agent_;
  const HostPortPair endpoint_;
  SpdySessionPool* const spdy_session_pool_;

  bool has_restarted_;
  const bool tunnel_;
  ProxyDelegate* const proxy_delegate_;

  bool using_spdy_;
  NextProto negotiated_protocol_;

  std::unique_ptr<HttpResponseInfo> error_response_info_;

  std::unique_ptr<ClientSocketHandle> transport_socket_handle_;
  std::unique_ptr<ProxyClientSocket> transport_socket_;

  // Called when a connection is established. Also used when restarting with
  // AUTH, which will invoke this when ready to restart, after reconnecting
  // if necessary.
  CompletionCallback connect_callback_;

  SpdyStreamRequest spdy_stream_request_;

  scoped_refptr<HttpAuthController> http_auth_controller_;

  NetLogWithSource net_log_;

  base::OneShotTimer connect_timer_;

  // Time when the connection to the proxy was started.
  base::TimeTicks connect_start_time_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxyClientSocketWrapper);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_WRAPPER_H_
