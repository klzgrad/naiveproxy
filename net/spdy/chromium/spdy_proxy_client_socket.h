// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_SPDY_PROXY_CLIENT_SOCKET_H_
#define NET_SPDY_CHROMIUM_SPDY_PROXY_CLIENT_SOCKET_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/http/http_auth_controller.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_info.h"
#include "net/http/proxy_client_socket.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_with_source.h"
#include "net/spdy/chromium/spdy_http_stream.h"
#include "net/spdy/chromium/spdy_read_queue.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/spdy/chromium/spdy_stream.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/spdy/platform/api/spdy_string.h"

namespace net {

class HttpStream;
class IOBuffer;
class SpdyStream;

class NET_EXPORT_PRIVATE SpdyProxyClientSocket : public ProxyClientSocket,
                                                 public SpdyStream::Delegate {
 public:
  // Create a socket on top of the |spdy_stream| by sending a HEADERS CONNECT
  // frame for |endpoint|.  After the response HEADERS frame is received, any
  // data read/written to the socket will be transferred in data frames. This
  // object will set itself as |spdy_stream|'s delegate.
  SpdyProxyClientSocket(const base::WeakPtr<SpdyStream>& spdy_stream,
                        const SpdyString& user_agent,
                        const HostPortPair& endpoint,
                        const NetLogWithSource& source_net_log,
                        HttpAuthController* auth_controller);

  // On destruction Disconnect() is called.
  ~SpdyProxyClientSocket() override;

  // ProxyClientSocket methods:
  const HttpResponseInfo* GetConnectResponseInfo() const override;
  std::unique_ptr<HttpStream> CreateConnectResponseStream() override;
  const scoped_refptr<HttpAuthController>& GetAuthController() const override;
  int RestartWithAuth(const CompletionCallback& callback) override;
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

  // SpdyStream::Delegate implementation.
  void OnHeadersSent() override;
  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override;
  void OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) override;
  void OnDataSent() override;
  void OnTrailers(const SpdyHeaderBlock& trailers) override;
  void OnClose(int status) override;
  NetLogSource source_dependency() const override;

 private:
  enum State {
    STATE_DISCONNECTED,
    STATE_GENERATE_AUTH_TOKEN,
    STATE_GENERATE_AUTH_TOKEN_COMPLETE,
    STATE_SEND_REQUEST,
    STATE_SEND_REQUEST_COMPLETE,
    STATE_READ_REPLY_COMPLETE,
    STATE_OPEN,
    STATE_CLOSED
  };

  void LogBlockedTunnelResponse() const;

  // Calls |callback.Run(result)|. Used to run a callback posted to the
  // message loop.
  void RunCallback(const CompletionCallback& callback, int result) const;

  void OnIOComplete(int result);

  int DoLoop(int last_io_result);
  int DoGenerateAuthToken();
  int DoGenerateAuthTokenComplete(int result);
  int DoSendRequest();
  int DoSendRequestComplete(int result);
  int DoReadReplyComplete(int result);

  // Populates |user_buffer_| with as much read data as possible
  // and returns the number of bytes read.
  size_t PopulateUserReadBuffer(char* out, size_t len);

  State next_state_;

  // Pointer to the SPDY Stream that this sits on top of.
  base::WeakPtr<SpdyStream> spdy_stream_;

  // Stores the callback to the layer above, called on completing Read() or
  // Connect().
  CompletionCallback read_callback_;
  // Stores the callback to the layer above, called on completing Write().
  CompletionCallback write_callback_;

  // CONNECT request and response.
  HttpRequestInfo request_;
  HttpResponseInfo response_;

  // The hostname and port of the endpoint.  This is not necessarily the one
  // specified by the URL, due to Alternate-Protocol or fixed testing ports.
  const HostPortPair endpoint_;
  scoped_refptr<HttpAuthController> auth_;

  SpdyString user_agent_;

  // We buffer the response body as it arrives asynchronously from the stream.
  SpdyReadQueue read_buffer_queue_;

  // User provided buffer for the Read() response.
  scoped_refptr<IOBuffer> user_buffer_;
  size_t user_buffer_len_;

  // User specified number of bytes to be written.
  int write_buffer_len_;

  // True if the transport socket has ever sent data.
  bool was_ever_used_;

  // Used only for redirects.
  bool redirect_has_load_timing_info_;
  LoadTimingInfo redirect_load_timing_info_;

  const NetLogWithSource net_log_;
  const NetLogSource source_dependency_;

  // The default weak pointer factory.
  base::WeakPtrFactory<SpdyProxyClientSocket> weak_factory_;

  // Only used for posting write callbacks. Weak pointers created by this
  // factory are invalidated in Disconnect().
  base::WeakPtrFactory<SpdyProxyClientSocket> write_callback_weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(SpdyProxyClientSocket);
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_SPDY_PROXY_CLIENT_SOCKET_H_
