// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_FUZZED_HOST_RESOLVER_H_
#define NET_DNS_FUZZED_HOST_RESOLVER_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/address_family.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_impl.h"
#include "net/socket/fuzzed_socket_factory.h"

namespace base {
class FuzzedDataProvider;
}

namespace net {

class AddressList;
class DnsClient;
class NetLog;

// HostResolver that uses a fuzzer to determine what results to return. It
// inherits from HostResolverImpl, unlike MockHostResolver, so more closely
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
class FuzzedHostResolver : public HostResolverImpl {
 public:
  // |data_provider| and |net_log| must outlive the FuzzedHostResolver.
  FuzzedHostResolver(const Options& options,
                     NetLog* net_log,
                     base::FuzzedDataProvider* data_provider);
  ~FuzzedHostResolver() override;

  // Enable / disable the async resolver. When enabled, installs a
  // DnsClient with fuzzed UDP and TCP sockets. Overrides
  // HostResolverImpl method of the same name.
  void SetDnsClientEnabled(bool enabled) override;

 private:
  // HostResolverImpl implementation:
  bool IsGloballyReachable(const IPAddress& dest,
                           const NetLogWithSource& net_log) override;
  void RunLoopbackProbeJob() override;

  base::FuzzedDataProvider* data_provider_;

  // Used for UDP and TCP sockets if the async resolver is enabled.
  FuzzedSocketFactory socket_factory_;

  // Fixed value to be returned by IsIPv6Reachable.
  const bool is_ipv6_reachable_;

  NetLog* net_log_;

  base::WeakPtrFactory<base::FuzzedDataProvider> data_provider_weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(FuzzedHostResolver);
};

}  // namespace net

#endif  // NET_DNS_FUZZED_HOST_RESOLVER_H_
