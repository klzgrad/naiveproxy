// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver.h"

#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_config_service.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver_impl.h"

namespace net {

namespace {

// Maximum of 6 concurrent resolver threads (excluding retries).
// Some routers (or resolvers) appear to start to provide host-not-found if
// too many simultaneous resolutions are pending.  This number needs to be
// further optimized, but 8 is what FF currently does. We found some routers
// that limit this to 6, so we're temporarily holding it at that level.
const size_t kDefaultMaxProcTasks = 6u;

}  // namespace

PrioritizedDispatcher::Limits HostResolver::Options::GetDispatcherLimits()
    const {
  PrioritizedDispatcher::Limits limits(NUM_PRIORITIES, max_concurrent_resolves);

  // If not using default, do not use the field trial.
  if (limits.total_jobs != HostResolver::kDefaultParallelism)
    return limits;

  // Default, without trial is no reserved slots.
  limits.total_jobs = kDefaultMaxProcTasks;

  // Parallelism is determined by the field trial.
  std::string group = base::FieldTrialList::FindFullName(
      "HostResolverDispatch");

  if (group.empty())
    return limits;

  // The format of the group name is a list of non-negative integers separated
  // by ':'. Each of the elements in the list corresponds to an element in
  // |reserved_slots|, except the last one which is the |total_jobs|.
  std::vector<base::StringPiece> group_parts = base::SplitStringPiece(
      group, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (group_parts.size() != NUM_PRIORITIES + 1) {
    NOTREACHED();
    return limits;
  }

  std::vector<size_t> parsed(group_parts.size());
  size_t total_reserved_slots = 0;

  for (size_t i = 0; i < group_parts.size(); ++i) {
    if (!base::StringToSizeT(group_parts[i], &parsed[i])) {
      NOTREACHED();
      return limits;
    }
  }

  size_t total_jobs = parsed.back();
  parsed.pop_back();
  for (size_t i = 0; i < parsed.size(); ++i) {
    total_reserved_slots += parsed[i];
  }

  // There must be some unreserved slots available for the all priorities.
  if (total_reserved_slots > total_jobs ||
      (total_reserved_slots == total_jobs && parsed[MINIMUM_PRIORITY] == 0)) {
    NOTREACHED();
    return limits;
  }

  limits.total_jobs = total_jobs;
  limits.reserved_slots = parsed;
  return limits;
}

HostResolver::Options::Options()
    : max_concurrent_resolves(kDefaultParallelism),
      max_retry_attempts(kDefaultRetryAttempts),
      enable_caching(true) {
}

HostResolver::RequestInfo::RequestInfo(const HostPortPair& host_port_pair)
    : RequestInfo() {
  host_port_pair_ = host_port_pair;
}

HostResolver::RequestInfo::RequestInfo(const RequestInfo& request_info)
    : host_port_pair_(request_info.host_port_pair_),
      address_family_(request_info.address_family_),
      host_resolver_flags_(request_info.host_resolver_flags_),
      allow_cached_response_(request_info.allow_cached_response_),
      is_speculative_(request_info.is_speculative_),
      is_my_ip_address_(request_info.is_my_ip_address_) {}

HostResolver::RequestInfo::~RequestInfo() {}

HostResolver::RequestInfo::RequestInfo()
    : address_family_(ADDRESS_FAMILY_UNSPECIFIED),
      host_resolver_flags_(0),
      allow_cached_response_(true),
      is_speculative_(false),
      is_my_ip_address_(false) {}

HostResolver::~HostResolver() {
}

void HostResolver::SetDnsClientEnabled(bool enabled) {
}

HostCache* HostResolver::GetHostCache() {
  return nullptr;
}

std::unique_ptr<base::Value> HostResolver::GetDnsConfigAsValue() const {
  return nullptr;
}

void HostResolver::InitializePersistence(
    const PersistCallback& persist_callback,
    std::unique_ptr<const base::Value> old_data) {}

void HostResolver::SetNoIPv6OnWifi(bool no_ipv6_on_wifi) {
  NOTREACHED();
}

bool HostResolver::GetNoIPv6OnWifi() {
  return false;
}

// static
std::unique_ptr<HostResolver> HostResolver::CreateSystemResolver(
    const Options& options,
    NetLog* net_log) {
  return std::unique_ptr<HostResolver>(
      CreateSystemResolverImpl(options, net_log).release());
}

// static
std::unique_ptr<HostResolverImpl> HostResolver::CreateSystemResolverImpl(
    const Options& options,
    NetLog* net_log) {
  return std::make_unique<HostResolverImpl>(options, net_log);
}

// static
std::unique_ptr<HostResolver> HostResolver::CreateDefaultResolver(
    NetLog* net_log) {
  return CreateSystemResolver(Options(), net_log);
}

// static
std::unique_ptr<HostResolverImpl> HostResolver::CreateDefaultResolverImpl(
    NetLog* net_log) {
  return CreateSystemResolverImpl(Options(), net_log);
}

HostResolver::HostResolver() {}

}  // namespace net
