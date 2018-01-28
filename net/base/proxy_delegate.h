// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PROXY_DELEGATE_H_
#define NET_BASE_PROXY_DELEGATE_H_

#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"

class GURL;

namespace net {

class HttpRequestHeaders;
class HttpResponseHeaders;
class HostPortPair;
class ProxyInfo;
class ProxyServer;
class ProxyService;

// Delegate for setting up a connection.
class NET_EXPORT ProxyDelegate {
 public:
  ProxyDelegate() {
  }

  virtual ~ProxyDelegate() {
  }

  // Called as the proxy is being resolved for |url| for a |method| request.
  // The caller may pass an empty string to get method agnostic resoulution.
  // Allows the delegate to override the proxy resolution decision made by
  // ProxyService. The delegate may override the decision by modifying the
  // ProxyInfo |result|.
  virtual void OnResolveProxy(const GURL& url,
                              const std::string& method,
                              const ProxyService& proxy_service,
                              ProxyInfo* result) = 0;

  // Called when use of |bad_proxy| fails due to |net_error|. |net_error| is
  // the network error encountered, if any, and OK if the fallback was
  // for a reason other than a network error (e.g. the proxy service was
  // explicitly directed to skip a proxy).
  virtual void OnFallback(const ProxyServer& bad_proxy,
                          int net_error) = 0;

  // Called immediately before a proxy tunnel request is sent.
  // Provides the embedder an opportunity to add extra request headers.
  virtual void OnBeforeTunnelRequest(const HostPortPair& proxy_server,
                                     HttpRequestHeaders* extra_headers) = 0;

  // Called when the connect attempt to a CONNECT proxy has completed.
  virtual void OnTunnelConnectCompleted(const HostPortPair& endpoint,
                                        const HostPortPair& proxy_server,
                                        int net_error) = 0;

  // Called after the response headers for the tunnel request are received.
  virtual void OnTunnelHeadersReceived(
      const HostPortPair& origin,
      const HostPortPair& proxy_server,
      const HttpResponseHeaders& response_headers) = 0;

  // Returns true if |proxy_server| is a trusted SPDY/HTTP2 proxy that is
  // allowed to push cross-origin resources.
  virtual bool IsTrustedSpdyProxy(const net::ProxyServer& proxy_server) = 0;

  // Called after the proxy is resolved but before the connection is
  // established. |resolved_proxy_server| is the proxy server resolved by the
  // proxy service for fetching |url|. Sets |alternative_proxy_server| to an
  // alternative proxy server, if one is available to fetch |url|.
  // |alternative_proxy_server| is owned by the caller, and is guaranteed to be
  // non-null.
  virtual void GetAlternativeProxy(
      const GURL& url,
      const ProxyServer& resolved_proxy_server,
      ProxyServer* alternative_proxy_server) const = 0;

  // Notifies the ProxyDelegate that |alternative_proxy_server| is broken.
  virtual void OnAlternativeProxyBroken(
      const ProxyServer& alternative_proxy_server) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyDelegate);
};

}  // namespace net

#endif  // NET_BASE_PROXY_DELEGATE_H_
