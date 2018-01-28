// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_RESOLVER_WINHTTP_H_
#define NET_PROXY_PROXY_RESOLVER_WINHTTP_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/proxy/proxy_resolver_factory.h"
#include "url/gurl.h"

namespace net {

// An implementation of ProxyResolverFactory that uses WinHTTP and the system
// proxy settings.
class NET_EXPORT_PRIVATE ProxyResolverFactoryWinHttp
    : public ProxyResolverFactory {
 public:
  ProxyResolverFactoryWinHttp();

  int CreateProxyResolver(
      const scoped_refptr<ProxyResolverScriptData>& pac_script,
      std::unique_ptr<ProxyResolver>* resolver,
      const CompletionCallback& callback,
      std::unique_ptr<Request>* request) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyResolverFactoryWinHttp);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_RESOLVER_WINHTTP_H_
