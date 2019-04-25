// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_CONTEXT_HOST_RESOLVER_H_
#define NET_DNS_CONTEXT_HOST_RESOLVER_H_

#include <memory>
#include <vector>

#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"

namespace base {
class TickClock;
}  // namespace base

namespace net {

class DnsClient;
struct DnsConfig;
class HostResolverImpl;
struct ProcTaskParams;

// Wrapper for HostResolverImpl that sets per-context parameters for created
// requests. Except for tests, typically only interacted with through the
// HostResolver interface.
//
// See HostResolver::Create[...]() methods for construction.
//
// TODO(crbug.com/934402): Construct individually for each URLRequestContext
// rather than using this as the singleton shared resolver.
class NET_EXPORT ContextHostResolver : public HostResolver {
 public:
  // Creates a ContextHostResolver that forwards all of its requests through
  // |impl|.
  explicit ContextHostResolver(std::unique_ptr<HostResolverImpl> impl);
  ~ContextHostResolver() override;

  // HostResolver methods:
  std::unique_ptr<ResolveHostRequest> CreateRequest(
      const HostPortPair& host,
      const NetLogWithSource& net_log,
      const base::Optional<ResolveHostParameters>& optional_parameters)
      override;
  std::unique_ptr<MdnsListener> CreateMdnsListener(
      const HostPortPair& host,
      DnsQueryType query_type) override;
  void SetDnsClientEnabled(bool enabled) override;
  HostCache* GetHostCache() override;
  bool HasCached(base::StringPiece hostname,
                 HostCache::Entry::Source* source_out,
                 HostCache::EntryStaleness* stale_out,
                 bool* secure_out) const override;
  std::unique_ptr<base::Value> GetDnsConfigAsValue() const override;
  void SetNoIPv6OnWifi(bool no_ipv6_on_wifi) override;
  bool GetNoIPv6OnWifi() override;
  void SetDnsConfigOverrides(const DnsConfigOverrides& overrides) override;
  void SetRequestContext(URLRequestContext* request_context) override;
  const std::vector<DnsConfig::DnsOverHttpsServerConfig>*
  GetDnsOverHttpsServersForTesting() const override;

  // Returns the number of host cache entries that were restored, or 0 if there
  // is no cache.
  size_t LastRestoredCacheSize() const;
  // Returns the number of entries in the host cache, or 0 if there is no cache.
  size_t CacheSize() const;

  void SetProcParamsForTesting(const ProcTaskParams& proc_params);
  void SetDnsClientForTesting(std::unique_ptr<DnsClient> dns_client);
  void SetBaseDnsConfigForTesting(const DnsConfig& base_config);
  void SetTickClockForTesting(const base::TickClock* tick_clock);

 private:
  // TODO(crbug.com/934402): Make this a non-owned pointer to the singleton
  // resolver.
  std::unique_ptr<HostResolverImpl> impl_;
};

}  // namespace net

#endif  // NET_DNS_CONTEXT_HOST_RESOLVER_H_
