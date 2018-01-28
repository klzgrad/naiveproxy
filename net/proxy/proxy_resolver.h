// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_RESOLVER_H_
#define NET_PROXY_PROXY_RESOLVER_H_

#include "base/callback_forward.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "net/base/completion_callback.h"
#include "net/base/load_states.h"
#include "net/base/net_export.h"
#include "net/proxy/proxy_resolver_script_data.h"
#include "url/gurl.h"

namespace net {

class NetLogWithSource;
class ProxyInfo;

// Interface for "proxy resolvers". A ProxyResolver fills in a list of proxies
// to use for a particular URL. Generally the backend for a ProxyResolver is
// a PAC script, but it doesn't need to be. ProxyResolver can service multiple
// requests at a time.
class NET_EXPORT_PRIVATE ProxyResolver {
 public:
  class Request {
   public:
    virtual ~Request() {}  // Cancels the request
    virtual LoadState GetLoadState() = 0;
  };

  ProxyResolver() {}

  virtual ~ProxyResolver() {}

  // Gets a list of proxy servers to use for |url|. If the request will
  // complete asynchronously returns ERR_IO_PENDING and notifies the result
  // by running |callback|.  If the result code is OK then
  // the request was successful and |results| contains the proxy
  // resolution information.  In the case of asynchronous completion
  // |*request| is written to. Call request_.reset() to cancel the request
  virtual int GetProxyForURL(const GURL& url,
                             ProxyInfo* results,
                             const CompletionCallback& callback,
                             std::unique_ptr<Request>* request,
                             const NetLogWithSource& net_log) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyResolver);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_RESOLVER_H_
