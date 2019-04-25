// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/context_host_resolver.h"

#include <utility>

#include "base/strings/string_piece.h"
#include "base/time/tick_clock.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_config.h"
#include "net/dns/host_resolver_impl.h"
#include "net/dns/host_resolver_proc.h"

namespace net {

ContextHostResolver::ContextHostResolver(std::unique_ptr<HostResolverImpl> impl)
    : impl_(std::move(impl)) {}

ContextHostResolver::~ContextHostResolver() = default;

std::unique_ptr<HostResolver::ResolveHostRequest>
ContextHostResolver::CreateRequest(
    const HostPortPair& host,
    const NetLogWithSource& source_net_log,
    const base::Optional<ResolveHostParameters>& optional_parameters) {
  return impl_->CreateRequest(host, source_net_log, optional_parameters);
}

std::unique_ptr<HostResolver::MdnsListener>
ContextHostResolver::CreateMdnsListener(const HostPortPair& host,
                                        DnsQueryType query_type) {
  return impl_->CreateMdnsListener(host, query_type);
}

void ContextHostResolver::SetDnsClientEnabled(bool enabled) {
  impl_->SetDnsClientEnabled(enabled);
}

HostCache* ContextHostResolver::GetHostCache() {
  return impl_->GetHostCache();
}

bool ContextHostResolver::HasCached(base::StringPiece hostname,
                                    HostCache::Entry::Source* source_out,
                                    HostCache::EntryStaleness* stale_out,
                                    bool* secure_out) const {
  return impl_->HasCached(hostname, source_out, stale_out, secure_out);
}

std::unique_ptr<base::Value> ContextHostResolver::GetDnsConfigAsValue() const {
  return impl_->GetDnsConfigAsValue();
}

void ContextHostResolver::SetNoIPv6OnWifi(bool no_ipv6_on_wifi) {
  impl_->SetNoIPv6OnWifi(no_ipv6_on_wifi);
}

bool ContextHostResolver::GetNoIPv6OnWifi() {
  return impl_->GetNoIPv6OnWifi();
}

void ContextHostResolver::SetDnsConfigOverrides(
    const DnsConfigOverrides& overrides) {
  impl_->SetDnsConfigOverrides(overrides);
}

void ContextHostResolver::SetRequestContext(
    URLRequestContext* request_context) {
  impl_->SetRequestContext(request_context);
}

const std::vector<DnsConfig::DnsOverHttpsServerConfig>*
ContextHostResolver::GetDnsOverHttpsServersForTesting() const {
  return impl_->GetDnsOverHttpsServersForTesting();
}

size_t ContextHostResolver::LastRestoredCacheSize() const {
  return impl_->LastRestoredCacheSize();
}

size_t ContextHostResolver::CacheSize() const {
  return impl_->CacheSize();
}

void ContextHostResolver::SetProcParamsForTesting(
    const ProcTaskParams& proc_params) {
  impl_->set_proc_params_for_test(proc_params);
}

void ContextHostResolver::SetDnsClientForTesting(
    std::unique_ptr<DnsClient> dns_client) {
  impl_->SetDnsClient(std::move(dns_client));
}

void ContextHostResolver::SetBaseDnsConfigForTesting(
    const DnsConfig& base_config) {
  impl_->SetBaseDnsConfigForTesting(base_config);
}

void ContextHostResolver::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  impl_->SetTickClockForTesting(tick_clock);
}

}  // namespace net
