// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/test_proxy_delegate.h"

#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TestProxyDelegate::TestProxyDelegate()
    : on_before_tunnel_request_called_(false),
      on_tunnel_request_completed_called_(false),
      on_tunnel_headers_received_called_(false),
      get_alternative_proxy_invocations_(0) {}

TestProxyDelegate::~TestProxyDelegate() {}

void TestProxyDelegate::VerifyOnTunnelRequestCompleted(
    const std::string& endpoint,
    const std::string& proxy_server) const {
  EXPECT_TRUE(on_tunnel_request_completed_called_);
  EXPECT_TRUE(HostPortPair::FromString(endpoint).Equals(
      on_tunnel_request_completed_endpoint_));
  EXPECT_TRUE(HostPortPair::FromString(proxy_server)
                  .Equals(on_tunnel_request_completed_proxy_server_));
}

void TestProxyDelegate::VerifyOnTunnelHeadersReceived(
    const std::string& origin,
    const std::string& proxy_server,
    const std::string& status_line) const {
  EXPECT_TRUE(on_tunnel_headers_received_called_);
  EXPECT_TRUE(HostPortPair::FromString(origin).Equals(
      on_tunnel_headers_received_origin_));
  EXPECT_TRUE(HostPortPair::FromString(proxy_server)
                  .Equals(on_tunnel_headers_received_proxy_server_));
  EXPECT_EQ(status_line, on_tunnel_headers_received_status_line_);
}

void TestProxyDelegate::OnResolveProxy(const GURL& url,
                                       const std::string& method,
                                       const ProxyService& proxy_service,
                                       ProxyInfo* result) {}

void TestProxyDelegate::OnTunnelConnectCompleted(
    const HostPortPair& endpoint,
    const HostPortPair& proxy_server,
    int net_error) {
  on_tunnel_request_completed_called_ = true;
  on_tunnel_request_completed_endpoint_ = endpoint;
  on_tunnel_request_completed_proxy_server_ = proxy_server;
}

void TestProxyDelegate::OnFallback(const ProxyServer& bad_proxy,
                                   int net_error) {}

void TestProxyDelegate::OnBeforeTunnelRequest(
    const HostPortPair& proxy_server,
    HttpRequestHeaders* extra_headers) {
  on_before_tunnel_request_called_ = true;
  if (extra_headers)
    extra_headers->SetHeader("Foo", proxy_server.ToString());
}

void TestProxyDelegate::OnTunnelHeadersReceived(
    const HostPortPair& origin,
    const HostPortPair& proxy_server,
    const HttpResponseHeaders& response_headers) {
  on_tunnel_headers_received_called_ = true;
  on_tunnel_headers_received_origin_ = origin;
  on_tunnel_headers_received_proxy_server_ = proxy_server;
  on_tunnel_headers_received_status_line_ = response_headers.GetStatusLine();
}

bool TestProxyDelegate::IsTrustedSpdyProxy(
    const net::ProxyServer& proxy_server) {
  return proxy_server.is_valid() && trusted_spdy_proxy_ == proxy_server;
}

void TestProxyDelegate::GetAlternativeProxy(
    const GURL& url,
    const ProxyServer& resolved_proxy_server,
    ProxyServer* alternative_proxy_server) const {
  EXPECT_TRUE(resolved_proxy_server.is_valid());
  EXPECT_FALSE(alternative_proxy_server->is_valid());
  *alternative_proxy_server = alternative_proxy_server_;
  get_alternative_proxy_invocations_++;
}

void TestProxyDelegate::OnAlternativeProxyBroken(
    const ProxyServer& alternative_proxy_server) {
  EXPECT_TRUE(alternative_proxy_server.is_valid());
  EXPECT_EQ(alternative_proxy_server_, alternative_proxy_server);
  alternative_proxy_server_ = ProxyServer();
}

}  // namespace net
