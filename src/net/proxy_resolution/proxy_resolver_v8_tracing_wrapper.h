// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_PROXY_RESOLVER_V8_TRACING_WRAPPER_H_
#define NET_PROXY_RESOLUTION_PROXY_RESOLVER_V8_TRACING_WRAPPER_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_once_callback.h"
#include "net/base/net_export.h"
#include "net/proxy_resolution/proxy_resolver.h"
#include "net/proxy_resolution/proxy_resolver_factory.h"
#include "net/proxy_resolution/proxy_resolver_v8_tracing.h"

namespace net {

class NetLog;
class ProxyHostResolver;
class ProxyResolverErrorObserver;

// A wrapper for ProxyResolverV8TracingFactory that implements the
// ProxyResolverFactory interface.
class NET_EXPORT ProxyResolverFactoryV8TracingWrapper
    : public ProxyResolverFactory {
 public:
  // Note that |host_resolver| and |net_log| are expected to outlive |this| and
  // any ProxyResolver instances created using |this|. |error_observer_factory|
  // will be invoked once per CreateProxyResolver() call to create a
  // ProxyResolverErrorObserver to be used by the ProxyResolver instance
  // returned by that call.
  ProxyResolverFactoryV8TracingWrapper(
      ProxyHostResolver* host_resolver,
      NetLog* net_log,
      const base::Callback<std::unique_ptr<ProxyResolverErrorObserver>()>&
          error_observer_factory);
  ~ProxyResolverFactoryV8TracingWrapper() override;

  // ProxyResolverFactory override.
  int CreateProxyResolver(const scoped_refptr<PacFileData>& pac_script,
                          std::unique_ptr<ProxyResolver>* resolver,
                          CompletionOnceCallback callback,
                          std::unique_ptr<Request>* request) override;

 private:
  void OnProxyResolverCreated(
      std::unique_ptr<std::unique_ptr<ProxyResolverV8Tracing>> v8_resolver,
      std::unique_ptr<ProxyResolver>* resolver,
      CompletionOnceCallback callback,
      std::unique_ptr<ProxyResolverErrorObserver> error_observer,
      int error);

  std::unique_ptr<ProxyResolverV8TracingFactory> factory_impl_;
  ProxyHostResolver* const host_resolver_;
  NetLog* const net_log_;
  const base::Callback<std::unique_ptr<ProxyResolverErrorObserver>()>
      error_observer_factory_;

  DISALLOW_COPY_AND_ASSIGN(ProxyResolverFactoryV8TracingWrapper);
};

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_PROXY_RESOLVER_V8_TRACING_WRAPPER_H_
