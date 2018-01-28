// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_RESOLVER_V8_H_
#define NET_PROXY_PROXY_RESOLVER_V8_H_

#include <stddef.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "net/base/net_export.h"

class GURL;

namespace net {
class ProxyInfo;
class ProxyResolverScriptData;

// A synchronous ProxyResolver-like that uses V8 to evaluate PAC scripts.
class NET_EXPORT_PRIVATE ProxyResolverV8 {
 public:
  // Interface for the javascript bindings.
  class NET_EXPORT_PRIVATE JSBindings {
   public:
    enum ResolveDnsOperation {
      DNS_RESOLVE,
      DNS_RESOLVE_EX,
      MY_IP_ADDRESS,
      MY_IP_ADDRESS_EX,
    };

    JSBindings() {}

    // Handler for "dnsResolve()", "dnsResolveEx()", "myIpAddress()",
    // "myIpAddressEx()". Returns true on success and fills |*output| with the
    // result. If |*terminate| is set to true, then the script execution will
    // be aborted. Note that termination may not happen right away.
    virtual bool ResolveDns(const std::string& host,
                            ResolveDnsOperation op,
                            std::string* output,
                            bool* terminate) = 0;

    // Handler for "alert(message)"
    virtual void Alert(const base::string16& message) = 0;

    // Handler for when an error is encountered. |line_number| may be -1
    // if a line number is not applicable to this error.
    virtual void OnError(int line_number, const base::string16& error) = 0;

   protected:
    virtual ~JSBindings() {}
  };

  // Constructs a ProxyResolverV8.
  static int Create(const scoped_refptr<ProxyResolverScriptData>& script_data,
                    JSBindings* bindings,
                    std::unique_ptr<ProxyResolverV8>* resolver);

  ~ProxyResolverV8();

  int GetProxyForURL(const GURL& url, ProxyInfo* results, JSBindings* bindings);

  // Get total/used heap memory usage of all v8 instances used by the proxy
  // resolver.
  static size_t GetTotalHeapSize();
  static size_t GetUsedHeapSize();

 private:
  // Context holds the Javascript state for the PAC script.
  class Context;

  explicit ProxyResolverV8(std::unique_ptr<Context> context);

  std::unique_ptr<Context> context_;

  DISALLOW_COPY_AND_ASSIGN(ProxyResolverV8);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_RESOLVER_V8_H_
