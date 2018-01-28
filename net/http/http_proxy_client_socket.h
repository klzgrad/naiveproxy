// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_H_
#define NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/http/http_auth_controller.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_info.h"
#include "net/http/proxy_client_socket.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/ssl_client_socket.h"

namespace net {

class ClientSocketHandle;
class GrowableIOBuffer;
class HttpStream;
class HttpStreamParser;
class IOBuffer;
class ProxyDelegate;

class NET_EXPORT_PRIVATE HttpProxyClientSocket : public ProxyClientSocket {
 public:
  // Takes ownership of |transport_socket|, which should already be connected
  // by the time Connect() is called.  If tunnel is true then on Connect()
  // this socket will establish an Http tunnel.
  HttpProxyClientSocket(ClientSocketHandle* transport_socket,
                        const std::string& user_agent,
                        const HostPortPair& endpoint,
                        const HostPortPair& proxy_server,
                        HttpAuthController* http_auth_controller,
                        bool tunnel,
                        bool using_spdy,
                        NextProto negotiated_protocol,
                        ProxyDelegate* proxy_delegate,
                        bool is_https_proxy);

  // On destruction Disconnect() is called.
  ~HttpProxyClientSocket() override;

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
  void ClearConnectionAttempts() override {}
  void AddConnectionAttempts(const ConnectionAttempts& attempts) override {}
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
    STATE_NONE,
    STATE_GENERATE_AUTH_TOKEN,
    STATE_GENERATE_AUTH_TOKEN_COMPLETE,
    STATE_SEND_REQUEST,
    STATE_SEND_REQUEST_COMPLETE,
    STATE_READ_HEADERS,
    STATE_READ_HEADERS_COMPLETE,
    STATE_DRAIN_BODY,
    STATE_DRAIN_BODY_COMPLETE,
    STATE_DONE,
  };

  // The size in bytes of the buffer we use to drain the response body that
  // we want to throw away.  The response body is typically a small error
  // page just a few hundred bytes long.
  static const int kDrainBodyBufferSize = 1024;

  int PrepareForAuthRestart();
  int DidDrainBodyForAuthRestart();

  void LogBlockedTunnelResponse() const;

  void DoCallback(int result);
  void OnIOComplete(int result);

  int DoLoop(int last_io_result);
  int DoGenerateAuthToken();
  int DoGenerateAuthTokenComplete(int result);
  int DoSendRequest();
  int DoSendRequestComplete(int result);
  int DoReadHeaders();
  int DoReadHeadersComplete(int result);
  int DoDrainBody();
  int DoDrainBodyComplete(int result);

  CompletionCallback io_callback_;
  State next_state_;

  // Stores the callback to the layer above, called on completing Connect().
  CompletionCallback user_callback_;

  HttpRequestInfo request_;
  HttpResponseInfo response_;

  scoped_refptr<GrowableIOBuffer> parser_buf_;
  std::unique_ptr<HttpStreamParser> http_stream_parser_;
  scoped_refptr<IOBuffer> drain_buf_;

  // Stores the underlying socket.
  std::unique_ptr<ClientSocketHandle> transport_;

  // The hostname and port of the endpoint.  This is not necessarily the one
  // specified by the URL, due to Alternate-Protocol or fixed testing ports.
  const HostPortPair endpoint_;
  scoped_refptr<HttpAuthController> auth_;
  const bool tunnel_;
  // If true, then the connection to the proxy is a SPDY connection.
  const bool using_spdy_;
  // Protocol negotiated with the server.
  NextProto negotiated_protocol_;
  // If true, then SSL is used to communicate with this proxy
  const bool is_https_proxy_;

  std::string request_line_;
  HttpRequestHeaders request_headers_;

  // Used only for redirects.
  bool redirect_has_load_timing_info_;
  LoadTimingInfo redirect_load_timing_info_;

  const HostPortPair proxy_server_;

  // This delegate must outlive this proxy client socket.
  ProxyDelegate* proxy_delegate_;

  const NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxyClientSocket);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_H_
