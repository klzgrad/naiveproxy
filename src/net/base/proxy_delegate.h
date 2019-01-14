// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PROXY_DELEGATE_H_
#define NET_BASE_PROXY_DELEGATE_H_

#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/proxy_resolution/proxy_retry_info.h"

class GURL;

namespace net {

class ProxyInfo;
class ProxyServer;

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
  // ProxyResolutionService. The delegate may override the decision by modifying
  // the ProxyInfo |result|.
  virtual void OnResolveProxy(const GURL& url,
                              const std::string& method,
                              const ProxyRetryInfoMap& proxy_retry_info,
                              ProxyInfo* result) = 0;

  // Called when use of |bad_proxy| fails due to |net_error|. |net_error| is
  // the network error encountered, if any, and OK if the fallback was
  // for a reason other than a network error (e.g. the proxy service was
  // explicitly directed to skip a proxy).
  virtual void OnFallback(const ProxyServer& bad_proxy,
                          int net_error) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyDelegate);
};

}  // namespace net

#endif  // NET_BASE_PROXY_DELEGATE_H_
