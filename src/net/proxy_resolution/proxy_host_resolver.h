// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_PROXY_HOST_RESOLVER_H_
#define NET_PROXY_RESOLUTION_PROXY_HOST_RESOLVER_H_

#include <memory>
#include <string>
#include <vector>

#include "net/base/completion_once_callback.h"
#include "net/base/ip_address.h"
#include "net/proxy_resolution/proxy_resolve_dns_operation.h"

namespace net {

// Interface for a limited (compared to the standard HostResolver) host resolver
// used just for proxy resolution.
class NET_EXPORT ProxyHostResolver {
 public:
  virtual ~ProxyHostResolver() {}

  class Request {
   public:
    virtual ~Request() {}
    virtual int Start(CompletionOnceCallback callback) = 0;
    virtual const std::vector<IPAddress>& GetResults() const = 0;
  };

  virtual std::unique_ptr<Request> CreateRequest(
      const std::string& hostname,
      ProxyResolveDnsOperation operation) = 0;
};

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_PROXY_HOST_RESOLVER_H_
