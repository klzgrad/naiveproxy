// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_RESOLVER_V8_TRACING_H_
#define NET_PROXY_PROXY_RESOLVER_V8_TRACING_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/proxy/proxy_resolver.h"
#include "net/proxy/proxy_resolver_factory.h"

namespace net {

class HostResolver;
class NetLogWithSource;

// ProxyResolverV8Tracing is a non-blocking proxy resolver.
class NET_EXPORT ProxyResolverV8Tracing {
 public:
  // Bindings is an interface used by ProxyResolverV8Tracing to delegate
  // per-request functionality. Each instance will be destroyed on the origin
  // thread of the ProxyResolverV8Tracing when the request completes or is
  // cancelled. All methods will be invoked from the origin thread.
  class Bindings {
   public:
    Bindings() {}
    virtual ~Bindings() {}

    // Invoked in response to an alert() call by the PAC script.
    virtual void Alert(const base::string16& message) = 0;

    // Invoked in response to an error in the PAC script.
    virtual void OnError(int line_number, const base::string16& message) = 0;

    // Returns a HostResolver to use for DNS resolution.
    virtual HostResolver* GetHostResolver() = 0;

    // Returns a NetLogWithSource to be passed to the HostResolver returned by
    // GetHostResolver().
    virtual NetLogWithSource GetNetLogWithSource() = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Bindings);
  };

  virtual ~ProxyResolverV8Tracing() {}

  // Gets a list of proxy servers to use for |url|. This request always
  // runs asynchronously and notifies the result by running |callback|. If the
  // result code is OK then the request was successful and |results| contains
  // the proxy resolution information.  Request can be cancelled by resetting
  // |*request|.
  virtual void GetProxyForURL(const GURL& url,
                              ProxyInfo* results,
                              const CompletionCallback& callback,
                              std::unique_ptr<ProxyResolver::Request>* request,
                              std::unique_ptr<Bindings> bindings) = 0;
};

// A factory for ProxyResolverV8Tracing instances. The default implementation,
// returned by Create(), creates ProxyResolverV8Tracing instances that execute
// ProxyResolverV8 on a single helper thread, and do some magic to avoid
// blocking in DNS. For more details see the design document:
// https://docs.google.com/a/google.com/document/d/16Ij5OcVnR3s0MH4Z5XkhI9VTPoMJdaBn9rKreAmGOdE/edit?pli=1
class NET_EXPORT ProxyResolverV8TracingFactory {
 public:
  ProxyResolverV8TracingFactory() {}
  virtual ~ProxyResolverV8TracingFactory() = default;

  virtual void CreateProxyResolverV8Tracing(
      const scoped_refptr<ProxyResolverScriptData>& pac_script,
      std::unique_ptr<ProxyResolverV8Tracing::Bindings> bindings,
      std::unique_ptr<ProxyResolverV8Tracing>* resolver,
      const CompletionCallback& callback,
      std::unique_ptr<ProxyResolverFactory::Request>* request) = 0;

  static std::unique_ptr<ProxyResolverV8TracingFactory> Create();

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyResolverV8TracingFactory);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_RESOLVER_V8_TRACING_H_
