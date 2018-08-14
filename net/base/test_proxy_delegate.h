// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_TEST_PROXY_DELEGATE_H_
#define NET_BASE_TEST_PROXY_DELEGATE_H_

#include <string>

#include "net/base/proxy_delegate.h"
#include "net/base/proxy_server.h"

class GURL;

namespace net {

class ProxyInfo;

class TestProxyDelegate : public ProxyDelegate {
 public:
  TestProxyDelegate();
  ~TestProxyDelegate() override;

  void set_trusted_spdy_proxy(const ProxyServer& proxy_server) {
    trusted_spdy_proxy_ = proxy_server;
  }

  // ProxyDelegate implementation:
  void OnResolveProxy(const GURL& url,
                      const std::string& method,
                      const ProxyRetryInfoMap& proxy_retry_info,
                      ProxyInfo* result) override;
  void OnFallback(const ProxyServer& bad_proxy, int net_error) override;

  void set_alternative_proxy_server(
      const ProxyServer& alternative_proxy_server) {
    alternative_proxy_server_ = alternative_proxy_server;
  }
  const ProxyServer& alternative_proxy_server() const {
    return alternative_proxy_server_;
  }

 private:
  ProxyServer trusted_spdy_proxy_;
  ProxyServer alternative_proxy_server_;
};

}  // namespace net

#endif  // NET_BASE_TEST_PROXY_DELEGATE_H_
