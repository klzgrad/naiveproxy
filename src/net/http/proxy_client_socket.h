// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_PROXY_CLIENT_SOCKET_H_
#define NET_HTTP_PROXY_CLIENT_SOCKET_H_

#include <memory>
#include <string>

#include "net/base/completion_once_callback.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/stream_socket.h"

namespace net {

class HostPortPair;
class HttpAuthController;
class HttpResponseInfo;
class HttpRequestHeaders;
class HttpAuthController;
class NetLogWithSource;

// A common base class for a stream socket tunneled through a proxy.
class NET_EXPORT_PRIVATE ProxyClientSocket : public StreamSocket {
 public:
  ProxyClientSocket() = default;

  ProxyClientSocket(const ProxyClientSocket&) = delete;
  ProxyClientSocket& operator=(const ProxyClientSocket&) = delete;

  ~ProxyClientSocket() override = default;

  // Returns the HttpResponseInfo (including HTTP Headers) from
  // the response to the CONNECT request.
  virtual const HttpResponseInfo* GetConnectResponseInfo() const = 0;

  // Returns the HttpAuthController which can be used
  // to interact with an HTTP Proxy Authorization Required (407) request.
  virtual const scoped_refptr<HttpAuthController>& GetAuthController() const
      = 0;

  // If Connect (or its callback) returns PROXY_AUTH_REQUESTED, then an
  // auth challenge was received.  If the HttpAuthController's HaveAuth()
  // method returns true, then the request just needs to be restarted with
  // this method to try with those credentials, and new credentials cannot
  // be provided.  Otherwise, credentials should be added to the
  // HttpAuthController before calling RestartWithAuth.  Not all
  // ProxyClientSocket implementations will be restartable.  Such
  // implementations should disconnect themselves and return OK.
  virtual int RestartWithAuth(CompletionOnceCallback callback) = 0;

  // Set the priority of the underlying stream (for SPDY and QUIC)
  virtual void SetStreamPriority(RequestPriority priority);

 protected:
  // The HTTP CONNECT method for establishing a tunnel connection is documented
  // in Section 9.3.6 of RFC 9110.
  // https://www.rfc-editor.org/rfc/rfc9110#name-connect
  static void BuildTunnelRequest(const HostPortPair& endpoint,
                                 const HttpRequestHeaders& extra_headers,
                                 const std::string& user_agent,
                                 std::string* request_line,
                                 HttpRequestHeaders* request_headers);

  // When an auth challenge (407 response) is received during tunnel
  // construction/ this method should be called.
  static int HandleProxyAuthChallenge(HttpAuthController* auth,
                                      HttpResponseInfo* response,
                                      const NetLogWithSource& net_log);

  // When a proxy authentication response is received during tunnel
  // construction, this method should be called to strip everything
  // but the auth header from the redirect response.
  static void SanitizeProxyAuth(HttpResponseInfo& response);
};

}  // namespace net

#endif  // NET_HTTP_PROXY_CLIENT_SOCKET_H_
