// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_FUZZED_CONTEXT_HOST_RESOLVER_H_
#define NET_DNS_FUZZED_CONTEXT_HOST_RESOLVER_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "net/dns/context_host_resolver.h"
#include "net/dns/host_resolver.h"
#include "net/socket/fuzzed_socket_factory.h"

namespace base {
class FuzzedDataProvider;
}

namespace net {

class NetLog;

// HostResolver that uses a fuzzer to determine what results to return. It
// inherits from ContextHostResolver, unlike MockHostResolver, so more closely
// matches real behavior.
//
// By default uses a mocked out system resolver, though can be configured to
// use the built-in async resolver (Built in DNS stub resolver) with a fuzzed
// set of UDP/TCP sockets.
//
// To make behavior most deterministic, does not use the WorkerPool to run its
// simulated platform host resolver calls, instead runs them on the thread it is
// created on.
//
// Note that it does not attempt to sort the resulting AddressList when using
// the mock system resolver path.
//
// The async DNS client can make system calls in AddressSorterPosix, but other
// methods that make system calls are stubbed out.
class FuzzedContextHostResolver : public ContextHostResolver {
 public:
  FuzzedContextHostResolver(const Options& options,
                            NetLog* net_log,
                            base::FuzzedDataProvider* data_provider);
  ~FuzzedContextHostResolver() override;

  // Enable / disable the async resolver. When enabled, installs a
  // DnsClient with fuzzed UDP and TCP sockets.
  void SetDnsClientEnabled(bool enabled) override;

 private:
  base::FuzzedDataProvider* const data_provider_;

  // Used for UDP and TCP sockets if the async resolver is enabled.
  FuzzedSocketFactory socket_factory_;

  NetLog* const net_log_;

  DISALLOW_COPY_AND_ASSIGN(FuzzedContextHostResolver);
};

}  // namespace net

#endif  // NET_DNS_FUZZED_CONTEXT_HOST_RESOLVER_H_
