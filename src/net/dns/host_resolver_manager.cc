// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_manager.h"

#if defined(OS_WIN)
#include <Winsock2.h>
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
#include <netdb.h>
#include <netinet/in.h>
#if !defined(OS_NACL)
#include <net/if.h>
#if !defined(OS_ANDROID)
#include <ifaddrs.h>
#endif  // !defined(OS_ANDROID)
#endif  // !defined(OS_NACL)
#endif  // defined(OS_WIN)

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/containers/linked_list.h"
#include "base/debug/debugger.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/numerics/checked_math.h"
#include "base/rand_util.h"
#include "base/sequence_checker.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/address_family.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/base/trace_constants.h"
#include "net/base/url_util.h"
#include "net/dns/address_sorter.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_reloader.h"
#include "net/dns/dns_response.h"
#include "net/dns/dns_transaction.h"
#include "net/dns/dns_util.h"
#include "net/dns/host_resolver_mdns_listener_impl.h"
#include "net/dns/host_resolver_mdns_task.h"
#include "net/dns/host_resolver_proc.h"
#include "net/dns/mdns_client.h"
#include "net/dns/public/dns_protocol.h"
#include "net/dns/record_parsed.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/datagram_client_socket.h"
#include "net/url_request/url_request_context.h"
#include "url/url_canon_ip.h"

#if BUILDFLAG(ENABLE_MDNS)
#include "net/dns/mdns_client_impl.h"
#endif

#if defined(OS_WIN)
#include "net/base/winsock_init.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#include "net/android/network_library.h"
#endif

namespace net {

namespace {

// Limit the size of hostnames that will be resolved to combat issues in
// some platform's resolvers.
const size_t kMaxHostLength = 4096;

// Default TTL for successful resolutions with ProcTask.
const unsigned kCacheEntryTTLSeconds = 60;

// Default TTL for unsuccessful resolutions with ProcTask.
const unsigned kNegativeCacheEntryTTLSeconds = 0;

// Minimum TTL for successful resolutions with DnsTask.
const unsigned kMinimumTTLSeconds = kCacheEntryTTLSeconds;

// Time between IPv6 probes, i.e. for how long results of each IPv6 probe are
// cached.
const int kIPv6ProbePeriodMs = 1000;

// Google DNS address used for IPv6 probes.
const uint8_t kIPv6ProbeAddress[] = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x88, 0x88};

enum DnsResolveStatus {
  RESOLVE_STATUS_DNS_SUCCESS = 0,
  RESOLVE_STATUS_PROC_SUCCESS,
  RESOLVE_STATUS_FAIL,
  RESOLVE_STATUS_SUSPECT_NETBIOS,
  RESOLVE_STATUS_MAX
};

// ICANN uses this localhost address to indicate a name collision.
//
// The policy in Chromium is to fail host resolving if it resolves to
// this special address.
//
// Not however that IP literals are exempt from this policy, so it is still
// possible to navigate to http://127.0.53.53/ directly.
//
// For more details: https://www.icann.org/news/announcement-2-2014-08-01-en
const uint8_t kIcanNameCollisionIp[] = {127, 0, 53, 53};

bool ContainsIcannNameCollisionIp(const AddressList& addr_list) {
  for (const auto& endpoint : addr_list) {
    const IPAddress& addr = endpoint.address();
    if (addr.IsIPv4() && IPAddressStartsWith(addr, kIcanNameCollisionIp)) {
      return true;
    }
  }
  return false;
}

// True if |hostname| ends with either ".local" or ".local.".
bool ResemblesMulticastDNSName(const std::string& hostname) {
  DCHECK(!hostname.empty());
  const char kSuffix[] = ".local.";
  const size_t kSuffixLen = sizeof(kSuffix) - 1;
  const size_t kSuffixLenTrimmed = kSuffixLen - 1;
  if (hostname.back() == '.') {
    return hostname.size() > kSuffixLen &&
           !hostname.compare(hostname.size() - kSuffixLen, kSuffixLen, kSuffix);
  }
  return hostname.size() > kSuffixLenTrimmed &&
         !hostname.compare(hostname.size() - kSuffixLenTrimmed,
                           kSuffixLenTrimmed, kSuffix, kSuffixLenTrimmed);
}

bool ConfigureAsyncDnsNoFallbackFieldTrial() {
  const bool kDefault = false;

  // Configure the AsyncDns field trial as follows:
  // groups AsyncDnsNoFallbackA and AsyncDnsNoFallbackB: return true,
  // groups AsyncDnsA and AsyncDnsB: return false,
  // groups SystemDnsA and SystemDnsB: return false,
  // otherwise (trial absent): return default.
  std::string group_name = base::FieldTrialList::FindFullName("AsyncDns");
  if (!group_name.empty()) {
    return base::StartsWith(group_name, "AsyncDnsNoFallback",
                            base::CompareCase::INSENSITIVE_ASCII);
  }
  return kDefault;
}

const base::FeatureParam<base::TaskPriority>::Option prio_modes[] = {
    {base::TaskPriority::USER_VISIBLE, "default"},
    {base::TaskPriority::USER_BLOCKING, "user_blocking"}};
const base::Feature kSystemResolverPriorityExperiment = {
    "SystemResolverPriorityExperiment", base::FEATURE_DISABLED_BY_DEFAULT};
const base::FeatureParam<base::TaskPriority> priority_mode{
    &kSystemResolverPriorityExperiment, "mode",
    base::TaskPriority::USER_VISIBLE, &prio_modes};

//-----------------------------------------------------------------------------

// Returns true if |addresses| contains only IPv4 loopback addresses.
bool IsAllIPv4Loopback(const AddressList& addresses) {
  for (unsigned i = 0; i < addresses.size(); ++i) {
    const IPAddress& address = addresses[i].address();
    switch (addresses[i].GetFamily()) {
      case ADDRESS_FAMILY_IPV4:
        if (address.bytes()[0] != 127)
          return false;
        break;
      case ADDRESS_FAMILY_IPV6:
        return false;
      default:
        NOTREACHED();
        return false;
    }
  }
  return true;
}

// Returns true if it can determine that only loopback addresses are configured.
// i.e. if only 127.0.0.1 and ::1 are routable.
// Also returns false if it cannot determine this.
bool HaveOnlyLoopbackAddresses() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::WILL_BLOCK);
#if defined(OS_WIN)
  // TODO(wtc): implement with the GetAdaptersAddresses function.
  NOTIMPLEMENTED();
  return false;
#elif defined(OS_ANDROID)
  return android::HaveOnlyLoopbackAddresses();
#elif defined(OS_NACL)
  NOTIMPLEMENTED();
  return false;
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
  struct ifaddrs* interface_addr = NULL;
  int rv = getifaddrs(&interface_addr);
  if (rv != 0) {
    DVPLOG(1) << "getifaddrs() failed";
    return false;
  }

  bool result = true;
  for (struct ifaddrs* interface = interface_addr; interface != NULL;
       interface = interface->ifa_next) {
    if (!(IFF_UP & interface->ifa_flags))
      continue;
    if (IFF_LOOPBACK & interface->ifa_flags)
      continue;
    const struct sockaddr* addr = interface->ifa_addr;
    if (!addr)
      continue;
    if (addr->sa_family == AF_INET6) {
      // Safe cast since this is AF_INET6.
      const struct sockaddr_in6* addr_in6 =
          reinterpret_cast<const struct sockaddr_in6*>(addr);
      const struct in6_addr* sin6_addr = &addr_in6->sin6_addr;
      if (IN6_IS_ADDR_LOOPBACK(sin6_addr) || IN6_IS_ADDR_LINKLOCAL(sin6_addr))
        continue;
    }
    if (addr->sa_family != AF_INET6 && addr->sa_family != AF_INET)
      continue;

    result = false;
    break;
  }
  freeifaddrs(interface_addr);
  return result;
#endif  // defined(various platforms)
}

// Creates NetLog parameters when the resolve failed.
base::Value NetLogProcTaskFailedParams(uint32_t attempt_number,
                                       int net_error,
                                       int os_error) {
  base::DictionaryValue dict;
  if (attempt_number)
    dict.SetInteger("attempt_number", attempt_number);

  dict.SetInteger("net_error", net_error);

  if (os_error) {
    dict.SetInteger("os_error", os_error);
#if defined(OS_WIN)
    // Map the error code to a human-readable string.
    LPWSTR error_string = nullptr;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  nullptr,  // Use the internal message table.
                  os_error,
                  0,  // Use default language.
                  (LPWSTR)&error_string,
                  0,         // Buffer size.
                  nullptr);  // Arguments (unused).
    dict.SetString("os_error_string", base::WideToUTF8(error_string));
    LocalFree(error_string);
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
    dict.SetString("os_error_string", gai_strerror(os_error));
#endif
  }

  return std::move(dict);
}

// Creates NetLog parameters when the DnsTask failed.
base::Value NetLogDnsTaskFailedParams(const HostCache::Entry& results,
                                      int dns_error) {
  base::DictionaryValue dict;
  dict.SetInteger("net_error", results.error());
  if (dns_error)
    dict.SetInteger("dns_error", dns_error);
  dict.SetKey("resolve_results", results.NetLogParams());
  return std::move(dict);
}

// Creates NetLog parameters containing the information of the request. Use
// NetLogRequestInfoCallback if the request is specified via RequestInfo.
base::Value NetLogRequestParams(const HostPortPair& host) {
  base::DictionaryValue dict;

  dict.SetString("host", host.ToString());
  dict.SetInteger("address_family",
                  static_cast<int>(ADDRESS_FAMILY_UNSPECIFIED));
  dict.SetBoolean("allow_cached_response", true);
  dict.SetBoolean("is_speculative", false);
  return std::move(dict);
}

// Creates NetLog parameters for the creation of a HostResolverManager::Job.
base::Value NetLogJobCreationParams(const NetLogSource& source,
                                    const std::string& host) {
  base::DictionaryValue dict;
  source.AddToEventParameters(&dict);
  dict.SetString("host", host);
  return std::move(dict);
}

// Creates NetLog parameters for HOST_RESOLVER_IMPL_JOB_ATTACH/DETACH events.
base::Value NetLogJobAttachParams(const NetLogSource& source,
                                  RequestPriority priority) {
  base::DictionaryValue dict;
  source.AddToEventParameters(&dict);
  dict.SetString("priority", RequestPriorityToString(priority));
  return std::move(dict);
}

// Creates NetLog parameters for the DNS_CONFIG_CHANGED event.
base::Value NetLogDnsConfigParams(const DnsConfig* config) {
  return base::Value::FromUniquePtrValue(config->ToValue());
}

base::Value NetLogIPv6AvailableParams(bool ipv6_available, bool cached) {
  base::DictionaryValue dict;
  dict.SetBoolean("ipv6_available", ipv6_available);
  dict.SetBoolean("cached", cached);
  return std::move(dict);
}

// The logging routines are defined here because some requests are resolved
// without a Request object.

// Logs when a request has just been started. Overloads for whether or not the
// request information is specified via a RequestInfo object.
void LogStartRequest(const NetLogWithSource& source_net_log,
                     const HostPortPair& host) {
  source_net_log.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_REQUEST,
                            [&] { return NetLogRequestParams(host); });
}

// Logs when a request has just completed (before its callback is run).
void LogFinishRequest(const NetLogWithSource& source_net_log, int net_error) {
  source_net_log.EndEventWithNetErrorCode(
      NetLogEventType::HOST_RESOLVER_IMPL_REQUEST, net_error);
}

// Logs when a request has been cancelled.
void LogCancelRequest(const NetLogWithSource& source_net_log) {
  source_net_log.AddEvent(NetLogEventType::CANCELLED);
  source_net_log.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_REQUEST);
}

//-----------------------------------------------------------------------------

// Maximum of 6 concurrent resolver threads (excluding retries).
// Some routers (or resolvers) appear to start to provide host-not-found if
// too many simultaneous resolutions are pending.  This number needs to be
// further optimized, but 8 is what FF currently does. We found some routers
// that limit this to 6, so we're temporarily holding it at that level.
const size_t kDefaultMaxProcTasks = 6u;

PrioritizedDispatcher::Limits GetDispatcherLimits(
    const HostResolver::ManagerOptions& options) {
  PrioritizedDispatcher::Limits limits(NUM_PRIORITIES,
                                       options.max_concurrent_resolves);

  // If not using default, do not use the field trial.
  if (limits.total_jobs != HostResolver::ManagerOptions::kDefaultParallelism)
    return limits;

  // Default, without trial is no reserved slots.
  limits.total_jobs = kDefaultMaxProcTasks;

  // Parallelism is determined by the field trial.
  std::string group =
      base::FieldTrialList::FindFullName("HostResolverDispatch");

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

// Keeps track of the highest priority.
class PriorityTracker {
 public:
  explicit PriorityTracker(RequestPriority initial_priority)
      : highest_priority_(initial_priority), total_count_(0) {
    memset(counts_, 0, sizeof(counts_));
  }

  RequestPriority highest_priority() const { return highest_priority_; }

  size_t total_count() const { return total_count_; }

  void Add(RequestPriority req_priority) {
    ++total_count_;
    ++counts_[req_priority];
    if (highest_priority_ < req_priority)
      highest_priority_ = req_priority;
  }

  void Remove(RequestPriority req_priority) {
    DCHECK_GT(total_count_, 0u);
    DCHECK_GT(counts_[req_priority], 0u);
    --total_count_;
    --counts_[req_priority];
    size_t i;
    for (i = highest_priority_; i > MINIMUM_PRIORITY && !counts_[i]; --i) {
    }
    highest_priority_ = static_cast<RequestPriority>(i);

    // In absence of requests, default to MINIMUM_PRIORITY.
    if (total_count_ == 0)
      DCHECK_EQ(MINIMUM_PRIORITY, highest_priority_);
  }

 private:
  RequestPriority highest_priority_;
  size_t total_count_;
  size_t counts_[NUM_PRIORITIES];
};

// Is |dns_server| within the list of known DNS servers that also support
// DNS-over-HTTPS?
bool DnsServerSupportsDoh(const IPAddress& dns_server) {
  static const base::NoDestructor<std::unordered_set<std::string>>
      upgradable_servers(std::initializer_list<std::string>({
          // Google Public DNS
          "8.8.8.8",
          "8.8.4.4",
          "2001:4860:4860::8888",
          "2001:4860:4860::8844",
          // Cloudflare DNS
          "1.1.1.1",
          "1.0.0.1",
          "2606:4700:4700::1111",
          "2606:4700:4700::1001",
          // Quad9 DNS
          "9.9.9.9",
          "149.112.112.112",
          "2620:fe::fe",
          "2620:fe::9",
      }));
  return upgradable_servers->find(dns_server.ToString()) !=
         upgradable_servers->end();
}

void NetLogHostCacheEntry(const NetLogWithSource& net_log,
                          NetLogEventType type,
                          NetLogEventPhase phase,
                          const HostCache::Entry& results) {
  net_log.AddEntry(type, phase, [&] { return results.NetLogParams(); });
}

}  // namespace

//-----------------------------------------------------------------------------

bool ResolveLocalHostname(base::StringPiece host, AddressList* address_list) {
  address_list->clear();

  bool is_local6;
  if (!IsLocalHostname(host, &is_local6))
    return false;

  address_list->push_back(IPEndPoint(IPAddress::IPv6Localhost(), 0));
  if (!is_local6) {
    address_list->push_back(IPEndPoint(IPAddress::IPv4Localhost(), 0));
  }

  return true;
}

const unsigned HostResolverManager::kMaximumDnsFailures = 16;

// Holds the callback and request parameters for an outstanding request.
//
// The RequestImpl is owned by the end user of host resolution. Deletion prior
// to the request having completed means the request was cancelled by the
// caller.
//
// Both the RequestImpl and its associated Job hold non-owning pointers to each
// other. Care must be taken to clear the corresponding pointer when
// cancellation is initiated by the Job (OnJobCancelled) vs by the end user
// (~RequestImpl).
class HostResolverManager::RequestImpl
    : public CancellableRequest,
      public base::LinkNode<HostResolverManager::RequestImpl> {
 public:
  RequestImpl(const NetLogWithSource& source_net_log,
              const HostPortPair& request_host,
              const base::Optional<ResolveHostParameters>& optional_parameters,
              URLRequestContext* request_context,
              HostCache* host_cache,
              base::WeakPtr<HostResolverManager> resolver)
      : source_net_log_(source_net_log),
        request_host_(request_host),
        parameters_(optional_parameters ? optional_parameters.value()
                                        : ResolveHostParameters()),
        request_context_(request_context),
        host_cache_(host_cache),
        host_resolver_flags_(
            HostResolver::ParametersToHostResolverFlags(parameters_)),
        priority_(parameters_.initial_priority),
        job_(nullptr),
        resolver_(resolver),
        complete_(false) {
    // If the query name matches one of the DoH server names, set the
    // secure_dns_mode_override field in ResolveHostParameters to OFF to avoid
    // infinite recursion.
    // TODO(crbug.com/878582): Add a URLRequest-level parameter to skip DoH that
    // can be set when a URLRequest to a DoH server is built. This will avoid
    // unnecessarily skipping DoH when a connection to the DoH server has been
    // established but the query happens to be for a DoH server hostname.
    DCHECK(resolver_);
    if (resolver_->HaveDnsConfig()) {
      std::unique_ptr<base::Value> dns_config =
          resolver_->GetDnsConfigAsValue();
      for (const base::Value& doh_server :
           dns_config->FindKey("doh_servers")->GetList()) {
        if (request_host_.host().compare(
                GURL(GetURLFromTemplateWithoutParameters(
                         doh_server.FindKey("server_template")->GetString()))
                    .host()) == 0) {
          parameters_.secure_dns_mode_override = DnsConfig::SecureDnsMode::OFF;
          break;
        }
      }
    }
  }

  ~RequestImpl() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    Cancel();
  }

  void Cancel() override;

  int Start(CompletionOnceCallback callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(callback);
    // Start() may only be called once per request.
    DCHECK(!job_);
    DCHECK(!complete_);
    DCHECK(!callback_);
    // Parent HostResolver must still be alive to call Start().
    DCHECK(resolver_);

    int rv = resolver_->Resolve(this);
    DCHECK(!complete_);
    if (rv == ERR_IO_PENDING) {
      DCHECK(job_);
      callback_ = std::move(callback);
    } else {
      DCHECK(!job_);
      complete_ = true;
    }
    resolver_ = nullptr;

    return rv;
  }

  const base::Optional<AddressList>& GetAddressResults() const override {
    DCHECK(complete_);
    static const base::NoDestructor<base::Optional<AddressList>> nullopt_result;
    return results_ ? results_.value().addresses() : *nullopt_result;
  }

  const base::Optional<std::vector<std::string>>& GetTextResults()
      const override {
    DCHECK(complete_);
    static const base::NoDestructor<base::Optional<std::vector<std::string>>>
        nullopt_result;
    return results_ ? results_.value().text_records() : *nullopt_result;
  }

  const base::Optional<std::vector<HostPortPair>>& GetHostnameResults()
      const override {
    DCHECK(complete_);
    static const base::NoDestructor<base::Optional<std::vector<HostPortPair>>>
        nullopt_result;
    return results_ ? results_.value().hostnames() : *nullopt_result;
  }

  const base::Optional<HostCache::EntryStaleness>& GetStaleInfo()
      const override {
    DCHECK(complete_);
    return stale_info_;
  }

  void ChangeRequestPriority(RequestPriority priority) override;

  void set_results(HostCache::Entry results) {
    // Should only be called at most once and before request is marked
    // completed.
    DCHECK(!complete_);
    DCHECK(!results_);
    DCHECK(!parameters_.is_speculative);

    results_ = std::move(results);
  }

  void set_stale_info(HostCache::EntryStaleness stale_info) {
    // Should only be called at most once and before request is marked
    // completed.
    DCHECK(!complete_);
    DCHECK(!stale_info_);
    DCHECK(!parameters_.is_speculative);

    stale_info_ = std::move(stale_info);
  }

  void AssignJob(Job* job) {
    DCHECK(job);
    DCHECK(!job_);

    job_ = job;
  }

  // Unassigns the Job without calling completion callback.
  void OnJobCancelled(Job* job) {
    DCHECK_EQ(job_, job);
    job_ = nullptr;
    DCHECK(!complete_);
    DCHECK(callback_);
    callback_.Reset();

    // No results should be set.
    DCHECK(!results_);
  }

  // Cleans up Job assignment, marks request completed, and calls the completion
  // callback.
  void OnJobCompleted(Job* job, int error) {
    DCHECK_EQ(job_, job);
    job_ = nullptr;

    DCHECK(!complete_);
    complete_ = true;

    DCHECK(callback_);
    std::move(callback_).Run(error);
  }

  Job* job() const { return job_; }

  // NetLog for the source, passed in HostResolver::Resolve.
  const NetLogWithSource& source_net_log() { return source_net_log_; }

  const HostPortPair& request_host() const { return request_host_; }

  const ResolveHostParameters& parameters() const { return parameters_; }

  URLRequestContext* request_context() const { return request_context_; }

  HostCache* host_cache() const { return host_cache_; }

  HostResolverFlags host_resolver_flags() const { return host_resolver_flags_; }

  RequestPriority priority() const { return priority_; }
  void set_priority(RequestPriority priority) { priority_ = priority; }

  bool complete() const { return complete_; }

  base::TimeTicks request_time() const {
    DCHECK(!request_time_.is_null());
    return request_time_;
  }
  void set_request_time(base::TimeTicks request_time) {
    DCHECK(request_time_.is_null());
    DCHECK(!request_time.is_null());
    request_time_ = request_time;
  }

 private:
  const NetLogWithSource source_net_log_;

  const HostPortPair request_host_;
  ResolveHostParameters parameters_;
  URLRequestContext* const request_context_;
  HostCache* const host_cache_;
  const HostResolverFlags host_resolver_flags_;

  RequestPriority priority_;

  // The resolve job that this request is dependent on.
  Job* job_;
  base::WeakPtr<HostResolverManager> resolver_;

  // The user's callback to invoke when the request completes.
  CompletionOnceCallback callback_;

  bool complete_;
  base::Optional<HostCache::Entry> results_;
  base::Optional<HostCache::EntryStaleness> stale_info_;

  base::TimeTicks request_time_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(RequestImpl);
};

//------------------------------------------------------------------------------

// Calls HostResolverProc in ThreadPool. Performs retries if necessary.
//
// In non-test code, the HostResolverProc is always SystemHostResolverProc,
// which calls a platform API that implements host resolution.
//
// Whenever we try to resolve the host, we post a delayed task to check if host
// resolution (OnLookupComplete) is completed or not. If the original attempt
// hasn't completed, then we start another attempt for host resolution. We take
// the results from the first attempt that finishes and ignore the results from
// all other attempts.
//
// TODO(szym): Move to separate source file for testing and mocking.
//
class HostResolverManager::ProcTask {
 public:
  typedef base::OnceCallback<void(int net_error, const AddressList& addr_list)>
      Callback;

  ProcTask(std::string hostname,
           AddressFamily address_family,
           HostResolverFlags flags,
           const ProcTaskParams& params,
           Callback callback,
           scoped_refptr<base::TaskRunner> proc_task_runner,
           const NetLogWithSource& job_net_log,
           const base::TickClock* tick_clock)
      : hostname_(std::move(hostname)),
        address_family_(address_family),
        flags_(flags),
        params_(params),
        callback_(std::move(callback)),
        network_task_runner_(base::ThreadTaskRunnerHandle::Get()),
        proc_task_runner_(std::move(proc_task_runner)),
        attempt_number_(0),
        net_log_(job_net_log),
        tick_clock_(tick_clock) {
    DCHECK(callback_);
    if (!params_.resolver_proc.get())
      params_.resolver_proc = HostResolverProc::GetDefault();
    // If default is unset, use the system proc.
    if (!params_.resolver_proc.get())
      params_.resolver_proc = new SystemHostResolverProc();
  }

  // Cancels this ProcTask. Any outstanding resolve attempts running on worker
  // thread will continue running, but they will post back to the network thread
  // before checking their WeakPtrs to find that this task is cancelled.
  ~ProcTask() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());

    // If this is cancellation, log the EndEvent (otherwise this was logged in
    // OnLookupComplete()).
    if (!was_completed())
      net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK);
  }

  void Start() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    DCHECK(!was_completed());
    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK);
    StartLookupAttempt();
  }

  bool was_completed() const {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    return callback_.is_null();
  }

 private:
  using AttemptCompletionCallback = base::OnceCallback<
      void(const AddressList& results, int error, const int os_error)>;

  void StartLookupAttempt() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    DCHECK(!was_completed());
    base::TimeTicks start_time = tick_clock_->NowTicks();
    ++attempt_number_;
    // Dispatch the lookup attempt to a worker thread.
    AttemptCompletionCallback completion_callback = base::BindOnce(
        &ProcTask::OnLookupAttemptComplete, weak_ptr_factory_.GetWeakPtr(),
        start_time, attempt_number_, tick_clock_);
    proc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&ProcTask::DoLookup, hostname_, address_family_, flags_,
                       params_.resolver_proc, network_task_runner_,
                       std::move(completion_callback)));

    net_log_.AddEventWithIntParams(
        NetLogEventType::HOST_RESOLVER_IMPL_ATTEMPT_STARTED, "attempt_number",
        attempt_number_);

    // If the results aren't received within a given time, RetryIfNotComplete
    // will start a new attempt if none of the outstanding attempts have
    // completed yet.
    // Use a WeakPtr to avoid keeping the ProcTask alive after completion or
    // cancellation.
    if (attempt_number_ <= params_.max_retry_attempts) {
      network_task_runner_->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&ProcTask::StartLookupAttempt,
                         weak_ptr_factory_.GetWeakPtr()),
          params_.unresponsive_delay *
              std::pow(params_.retry_factor, attempt_number_ - 1));
    }
  }

  // WARNING: This code runs in ThreadPool with CONTINUE_ON_SHUTDOWN. The
  // shutdown code cannot wait for it to finish, so this code must be very
  // careful about using other objects (like MessageLoops, Singletons, etc).
  // During shutdown these objects may no longer exist.
  static void DoLookup(
      std::string hostname,
      AddressFamily address_family,
      HostResolverFlags flags,
      scoped_refptr<HostResolverProc> resolver_proc,
      scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
      AttemptCompletionCallback completion_callback) {
    AddressList results;
    int os_error = 0;
    int error = resolver_proc->Resolve(std::move(hostname), address_family,
                                       flags, &results, &os_error);

    network_task_runner->PostTask(
        FROM_HERE, base::BindOnce(std::move(completion_callback), results,
                                  error, os_error));
  }

  // Callback for when DoLookup() completes (runs on task runner thread). Now
  // that we're back in the network thread, checks that |proc_task| is still
  // valid, and if so, passes back to the object.
  static void OnLookupAttemptComplete(base::WeakPtr<ProcTask> proc_task,
                                      const base::TimeTicks& start_time,
                                      const uint32_t attempt_number,
                                      const base::TickClock* tick_clock,
                                      const AddressList& results,
                                      int error,
                                      const int os_error) {
    TRACE_EVENT0(NetTracingCategory(), "ProcTask::OnLookupComplete");

    // If results are empty, we should return an error.
    bool empty_list_on_ok = (error == OK && results.empty());
    if (empty_list_on_ok)
      error = ERR_NAME_NOT_RESOLVED;

    // Ideally the following code would be part of host_resolver_proc.cc,
    // however it isn't safe to call NetworkChangeNotifier from worker threads.
    // So do it here on the IO thread instead.
    if (error != OK && NetworkChangeNotifier::IsOffline())
      error = ERR_INTERNET_DISCONNECTED;

    if (!proc_task)
      return;

    proc_task->OnLookupComplete(results, start_time, attempt_number, error,
                                os_error);
  }

  void OnLookupComplete(const AddressList& results,
                        const base::TimeTicks& start_time,
                        const uint32_t attempt_number,
                        int error,
                        const int os_error) {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    DCHECK(!was_completed());

    // Invalidate WeakPtrs to cancel handling of all outstanding lookup attempts
    // and retries.
    weak_ptr_factory_.InvalidateWeakPtrs();

    if (error != OK) {
      net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK, [&] {
        return NetLogProcTaskFailedParams(0, error, os_error);
      });
      net_log_.AddEvent(
          NetLogEventType::HOST_RESOLVER_IMPL_ATTEMPT_FINISHED, [&] {
            return NetLogProcTaskFailedParams(attempt_number, error, os_error);
          });
    } else {
      net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK,
                        [&] { return results.NetLogParams(); });
      net_log_.AddEventWithIntParams(
          NetLogEventType::HOST_RESOLVER_IMPL_ATTEMPT_FINISHED,
          "attempt_number", attempt_number);
    }

    std::move(callback_).Run(error, results);
  }

  const std::string hostname_;
  const AddressFamily address_family_;
  const HostResolverFlags flags_;

  // Holds an owning reference to the HostResolverProc that we are going to use.
  // This may not be the current resolver procedure by the time we call
  // ResolveAddrInfo, but that's OK... we'll use it anyways, and the owning
  // reference ensures that it remains valid until we are done.
  ProcTaskParams params_;

  // The listener to the results of this ProcTask.
  Callback callback_;

  // Used to post events onto the network thread.
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;
  // Used to post blocking HostResolverProc tasks.
  scoped_refptr<base::TaskRunner> proc_task_runner_;

  // Keeps track of the number of attempts we have made so far to resolve the
  // host. Whenever we start an attempt to resolve the host, we increase this
  // number.
  uint32_t attempt_number_;

  NetLogWithSource net_log_;

  const base::TickClock* tick_clock_;

  // Used to loop back from the blocking lookup attempt tasks as well as from
  // delayed retry tasks. Invalidate WeakPtrs on completion and cancellation to
  // cancel handling of such posted tasks.
  base::WeakPtrFactory<ProcTask> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ProcTask);
};

//-----------------------------------------------------------------------------

// Resolves the hostname using DnsTransaction, which is a full implementation of
// a DNS stub resolver. One DnsTransaction is created for each resolution
// needed, which for AF_UNSPEC resolutions includes both A and AAAA. The
// transactions are scheduled separately and started separately.
//
// TODO(szym): This could be moved to separate source file as well.
class HostResolverManager::DnsTask : public base::SupportsWeakPtr<DnsTask> {
 public:
  class Delegate {
   public:
    virtual void OnDnsTaskComplete(base::TimeTicks start_time,
                                   const HostCache::Entry& results,
                                   bool secure) = 0;

    // Called when the first of two jobs succeeds.  If the first completed
    // transaction fails, this is not called.  Also not called when the DnsTask
    // only needs to run one transaction.
    virtual void OnFirstDnsTransactionComplete() = 0;

    virtual RequestPriority priority() const = 0;

   protected:
    Delegate() = default;
    virtual ~Delegate() = default;
  };

  DnsTask(DnsClient* client,
          base::StringPiece hostname,
          DnsQueryType query_type,
          URLRequestContext* request_context,
          bool secure,
          Delegate* delegate,
          const NetLogWithSource& job_net_log,
          const base::TickClock* tick_clock)
      : client_(client),
        hostname_(hostname),
        query_type_(query_type),
        request_context_(request_context),
        secure_(secure),
        delegate_(delegate),
        net_log_(job_net_log),
        num_completed_transactions_(0),
        tick_clock_(tick_clock),
        task_start_time_(tick_clock_->NowTicks()) {
    DCHECK(delegate_);
  }

  bool needs_two_transactions() const {
    return query_type_ == DnsQueryType::UNSPECIFIED;
  }

  bool needs_another_transaction() const {
    return needs_two_transactions() && !transaction2_;
  }

  void StartFirstTransaction() {
    DCHECK(client_);
    DCHECK_EQ(0u, num_completed_transactions_);
    DCHECK(!transaction1_);

    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK);
    transaction1_ = CreateTransaction(query_type_ == DnsQueryType::UNSPECIFIED
                                          ? DnsQueryType::A
                                          : query_type_);
    transaction1_->Start();
  }

  void StartSecondTransaction() {
    DCHECK(client_);
    DCHECK(needs_another_transaction());
    transaction2_ = CreateTransaction(DnsQueryType::AAAA);
    transaction2_->Start();
  }

 private:
  static const HostCache::Entry& GetMalformedResponseResult() {
    static const base::NoDestructor<HostCache::Entry> kMalformedResponseResult(
        ERR_DNS_MALFORMED_RESPONSE, HostCache::Entry::SOURCE_DNS);
    return *kMalformedResponseResult;
  }

  std::unique_ptr<DnsTransaction> CreateTransaction(
      DnsQueryType dns_query_type) {
    DCHECK(client_);
    DCHECK_NE(DnsQueryType::UNSPECIFIED, dns_query_type);
    std::unique_ptr<DnsTransaction> trans =
        client_->GetTransactionFactory()->CreateTransaction(
            hostname_, DnsQueryTypeToQtype(dns_query_type),
            base::BindOnce(&DnsTask::OnTransactionComplete,
                           base::Unretained(this), tick_clock_->NowTicks(),
                           dns_query_type),
            net_log_, secure_, request_context_);
    trans->SetRequestPriority(delegate_->priority());
    return trans;
  }

  void OnTransactionComplete(const base::TimeTicks& start_time,
                             DnsQueryType dns_query_type,
                             DnsTransaction* transaction,
                             int net_error,
                             const DnsResponse* response) {
    DCHECK(transaction);
    if (net_error != OK && !(net_error == ERR_NAME_NOT_RESOLVED && response &&
                             response->IsValid())) {
      OnFailure(net_error, DnsResponse::DNS_PARSE_OK, base::nullopt);
      return;
    }

    DnsResponse::Result parse_result = DnsResponse::DNS_PARSE_RESULT_MAX;
    HostCache::Entry results(ERR_FAILED, HostCache::Entry::SOURCE_UNKNOWN);
    switch (dns_query_type) {
      case DnsQueryType::UNSPECIFIED:
        // Should create two separate transactions with specified type.
        NOTREACHED();
        break;
      case DnsQueryType::A:
      case DnsQueryType::AAAA:
        parse_result = ParseAddressDnsResponse(response, &results);
        break;
      case DnsQueryType::TXT:
        parse_result = ParseTxtDnsResponse(response, &results);
        break;
      case DnsQueryType::PTR:
        parse_result = ParsePointerDnsResponse(response, &results);
        break;
      case DnsQueryType::SRV:
        parse_result = ParseServiceDnsResponse(response, &results);
        break;
    }
    DCHECK_LT(parse_result, DnsResponse::DNS_PARSE_RESULT_MAX);

    if (results.error() != OK && results.error() != ERR_NAME_NOT_RESOLVED) {
      OnFailure(results.error(), parse_result, results.GetOptionalTtl());
      return;
    }

    // Merge results with saved results from previous transactions.
    if (saved_results_) {
      DCHECK(needs_two_transactions());
      DCHECK_GE(1u, num_completed_transactions_);

      switch (dns_query_type) {
        case DnsQueryType::A:
          // A results in |results| go after other results in |saved_results_|,
          // so merge |saved_results_| to the front.
          results = HostCache::Entry::MergeEntries(
              std::move(saved_results_).value(), std::move(results));
          break;
        case DnsQueryType::AAAA:
          // AAAA results in |results| go before other results in
          // |saved_results_|, so merge |saved_results_| to the back.
          results = HostCache::Entry::MergeEntries(
              std::move(results), std::move(saved_results_).value());
          break;
        default:
          // Only expect address query types with multiple transactions.
          NOTREACHED();
      }
    }

    // If not all transactions are complete, the task cannot yet be completed
    // and the results so far must be saved to merge with additional results.
    ++num_completed_transactions_;
    if (needs_two_transactions() && num_completed_transactions_ == 1) {
      saved_results_ = std::move(results);
      delegate_->OnFirstDnsTransactionComplete();
      return;
    }

    // If there are multiple addresses, and at least one is IPv6, need to sort
    // them.  Note that IPv6 addresses are always put before IPv4 ones, so it's
    // sufficient to just check the family of the first address.
    if (results.addresses() && results.addresses().value().size() > 1 &&
        results.addresses().value()[0].GetFamily() == ADDRESS_FAMILY_IPV6) {
      // Sort addresses if needed.  Sort could complete synchronously.
      client_->GetAddressSorter()->Sort(
          results.addresses().value(),
          base::BindOnce(&DnsTask::OnSortComplete, AsWeakPtr(),
                         tick_clock_->NowTicks(), std::move(results), secure_));
      return;
    }

    OnSuccess(results);
  }

  DnsResponse::Result ParseAddressDnsResponse(const DnsResponse* response,
                                              HostCache::Entry* out_results) {
    AddressList addresses;
    base::TimeDelta ttl;
    DnsResponse::Result parse_result =
        response->ParseToAddressList(&addresses, &ttl);

    if (parse_result != DnsResponse::DNS_PARSE_OK) {
      *out_results = GetMalformedResponseResult();
    } else if (addresses.empty()) {
      *out_results = HostCache::Entry(ERR_NAME_NOT_RESOLVED, AddressList(),
                                      HostCache::Entry::SOURCE_DNS, ttl);
    } else {
      *out_results = HostCache::Entry(OK, std::move(addresses),
                                      HostCache::Entry::SOURCE_DNS, ttl);
    }
    return parse_result;
  }

  DnsResponse::Result ParseTxtDnsResponse(const DnsResponse* response,
                                          HostCache::Entry* out_results) {
    std::vector<std::unique_ptr<const RecordParsed>> records;
    base::Optional<base::TimeDelta> response_ttl;
    DnsResponse::Result parse_result = ParseAndFilterResponseRecords(
        response, dns_protocol::kTypeTXT, &records, &response_ttl);

    if (parse_result != DnsResponse::DNS_PARSE_OK) {
      *out_results = GetMalformedResponseResult();
      return parse_result;
    }

    std::vector<std::string> text_records;
    for (const auto& record : records) {
      const TxtRecordRdata* rdata = record->rdata<net::TxtRecordRdata>();
      text_records.insert(text_records.end(), rdata->texts().begin(),
                          rdata->texts().end());
    }

    *out_results = HostCache::Entry(
        text_records.empty() ? ERR_NAME_NOT_RESOLVED : OK,
        std::move(text_records), HostCache::Entry::SOURCE_DNS, response_ttl);
    return DnsResponse::DNS_PARSE_OK;
  }

  DnsResponse::Result ParsePointerDnsResponse(const DnsResponse* response,
                                              HostCache::Entry* out_results) {
    std::vector<std::unique_ptr<const RecordParsed>> records;
    base::Optional<base::TimeDelta> response_ttl;
    DnsResponse::Result parse_result = ParseAndFilterResponseRecords(
        response, dns_protocol::kTypePTR, &records, &response_ttl);

    if (parse_result != DnsResponse::DNS_PARSE_OK) {
      *out_results = GetMalformedResponseResult();
      return parse_result;
    }

    std::vector<HostPortPair> pointers;
    for (const auto& record : records) {
      const PtrRecordRdata* rdata = record->rdata<net::PtrRecordRdata>();
      std::string pointer = rdata->ptrdomain();

      // Skip pointers to the root domain.
      if (!pointer.empty())
        pointers.emplace_back(std::move(pointer), 0);
    }

    *out_results = HostCache::Entry(
        pointers.empty() ? ERR_NAME_NOT_RESOLVED : OK, std::move(pointers),
        HostCache::Entry::SOURCE_DNS, response_ttl);
    return DnsResponse::DNS_PARSE_OK;
  }

  DnsResponse::Result ParseServiceDnsResponse(const DnsResponse* response,
                                              HostCache::Entry* out_results) {
    std::vector<std::unique_ptr<const RecordParsed>> records;
    base::Optional<base::TimeDelta> response_ttl;
    DnsResponse::Result parse_result = ParseAndFilterResponseRecords(
        response, dns_protocol::kTypeSRV, &records, &response_ttl);

    if (parse_result != DnsResponse::DNS_PARSE_OK) {
      *out_results = GetMalformedResponseResult();
      return parse_result;
    }

    std::vector<const SrvRecordRdata*> fitered_rdatas;
    for (const auto& record : records) {
      const SrvRecordRdata* rdata = record->rdata<net::SrvRecordRdata>();

      // Skip pointers to the root domain.
      if (!rdata->target().empty())
        fitered_rdatas.push_back(rdata);
    }

    std::vector<HostPortPair> ordered_service_targets =
        SortServiceTargets(fitered_rdatas);

    *out_results = HostCache::Entry(
        ordered_service_targets.empty() ? ERR_NAME_NOT_RESOLVED : OK,
        std::move(ordered_service_targets), HostCache::Entry::SOURCE_DNS,
        response_ttl);
    return DnsResponse::DNS_PARSE_OK;
  }

  // Sort service targets per RFC2782.  In summary, sort first by |priority|,
  // lowest first.  For targets with the same priority, secondary sort randomly
  // using |weight| with higher weighted objects more likely to go first.
  std::vector<HostPortPair> SortServiceTargets(
      const std::vector<const SrvRecordRdata*>& rdatas) {
    std::map<uint16_t, std::unordered_set<const SrvRecordRdata*>>
        ordered_by_priority;
    for (const SrvRecordRdata* rdata : rdatas)
      ordered_by_priority[rdata->priority()].insert(rdata);

    std::vector<HostPortPair> sorted_targets;
    for (auto& priority : ordered_by_priority) {
      // With (num results) <= UINT16_MAX (and in practice, much less) and
      // (weight per result) <= UINT16_MAX, then it should be the case that
      // (total weight) <= UINT32_MAX, but use CheckedNumeric for extra safety.
      auto total_weight = base::MakeCheckedNum<uint32_t>(0);
      for (const SrvRecordRdata* rdata : priority.second)
        total_weight += rdata->weight();

      // Add 1 to total weight because, to deal with 0-weight targets, we want
      // our random selection to be inclusive [0, total].
      total_weight++;

      // Order by weighted random. Make such random selections, removing from
      // |priority.second| until |priority.second| only contains 1 rdata.
      while (priority.second.size() >= 2) {
        uint32_t random_selection =
            base::RandGenerator(total_weight.ValueOrDie());
        const SrvRecordRdata* selected_rdata = nullptr;
        for (const SrvRecordRdata* rdata : priority.second) {
          // >= to always select the first target on |random_selection| == 0,
          // even if its weight is 0.
          if (rdata->weight() >= random_selection) {
            selected_rdata = rdata;
            break;
          }
          random_selection -= rdata->weight();
        }

        DCHECK(selected_rdata);
        sorted_targets.emplace_back(selected_rdata->target(),
                                    selected_rdata->port());
        total_weight -= selected_rdata->weight();
        size_t removed = priority.second.erase(selected_rdata);
        DCHECK_EQ(1u, removed);
      }

      DCHECK_EQ(1u, priority.second.size());
      DCHECK_EQ((total_weight - 1).ValueOrDie(),
                (*priority.second.begin())->weight());
      const SrvRecordRdata* rdata = *priority.second.begin();
      sorted_targets.emplace_back(rdata->target(), rdata->port());
    }

    return sorted_targets;
  }

  DnsResponse::Result ParseAndFilterResponseRecords(
      const DnsResponse* response,
      uint16_t filter_dns_type,
      std::vector<std::unique_ptr<const RecordParsed>>* out_records,
      base::Optional<base::TimeDelta>* out_response_ttl) {
    out_records->clear();
    out_response_ttl->reset();

    DnsRecordParser parser = response->Parser();

    // Expected to be validated by DnsTransaction.
    DCHECK_EQ(filter_dns_type, response->qtype());

    for (unsigned i = 0; i < response->answer_count(); ++i) {
      std::unique_ptr<const RecordParsed> record =
          RecordParsed::CreateFrom(&parser, base::Time::Now());

      if (!record)
        return DnsResponse::DNS_MALFORMED_RESPONSE;
      if (!base::EqualsCaseInsensitiveASCII(record->name(),
                                            response->GetDottedName())) {
        return DnsResponse::DNS_NAME_MISMATCH;
      }

      // Ignore any records that are not class Internet and type
      // |filter_dns_type|.
      if (record->klass() == dns_protocol::kClassIN &&
          record->type() == filter_dns_type) {
        base::TimeDelta ttl = base::TimeDelta::FromSeconds(record->ttl());
        *out_response_ttl =
            std::min(out_response_ttl->value_or(base::TimeDelta::Max()), ttl);

        out_records->push_back(std::move(record));
      }
    }

    return DnsResponse::DNS_PARSE_OK;
  }

  void OnSortComplete(base::TimeTicks sort_start_time,
                      HostCache::Entry results,
                      bool secure,
                      bool success,
                      const AddressList& addr_list) {
    results.set_addresses(addr_list);

    if (!success) {
      OnFailure(ERR_DNS_SORT_ERROR, DnsResponse::DNS_PARSE_OK,
                results.GetOptionalTtl());
      return;
    }

    // AddressSorter prunes unusable destinations.
    if (addr_list.empty() &&
        results.text_records().value_or(std::vector<std::string>()).empty() &&
        results.hostnames().value_or(std::vector<HostPortPair>()).empty()) {
      LOG(WARNING) << "Address list empty after RFC3484 sort";
      OnFailure(ERR_NAME_NOT_RESOLVED, DnsResponse::DNS_PARSE_OK,
                results.GetOptionalTtl());
      return;
    }

    OnSuccess(results);
  }

  void OnFailure(int net_error,
                 DnsResponse::Result parse_result,
                 base::Optional<base::TimeDelta> ttl) {
    DCHECK_NE(OK, net_error);
    HostCache::Entry results(net_error, HostCache::Entry::SOURCE_UNKNOWN);

    net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK, [&] {
      return NetLogDnsTaskFailedParams(results, parse_result);
    });

    // If we have a TTL from a previously completed transaction, use it.
    base::TimeDelta previous_transaction_ttl;
    if (saved_results_ && saved_results_.value().has_ttl() &&
        saved_results_.value().ttl() <
            base::TimeDelta::FromSeconds(
                std::numeric_limits<uint32_t>::max())) {
      previous_transaction_ttl = saved_results_.value().ttl();
      if (ttl)
        results.set_ttl(std::min(ttl.value(), previous_transaction_ttl));
      else
        results.set_ttl(previous_transaction_ttl);
    } else if (ttl) {
      results.set_ttl(ttl.value());
    }

    delegate_->OnDnsTaskComplete(task_start_time_, results, secure_);
  }

  void OnSuccess(const HostCache::Entry& results) {
    NetLogHostCacheEntry(net_log_, NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK,
                         NetLogEventPhase::END, results);
    delegate_->OnDnsTaskComplete(task_start_time_, results, secure_);
  }

  DnsClient* client_;
  std::string hostname_;
  const DnsQueryType query_type_;
  URLRequestContext* const request_context_;

  // Whether lookups in this DnsTask should occur using DoH or plaintext.
  const bool secure_;

  // The listener to the results of this DnsTask.
  Delegate* delegate_;
  const NetLogWithSource net_log_;

  std::unique_ptr<DnsTransaction> transaction1_;
  std::unique_ptr<DnsTransaction> transaction2_;

  unsigned num_completed_transactions_;

  // Result from previously completed transactions. Only set if a transaction
  // has completed while others are still in progress.
  base::Optional<HostCache::Entry> saved_results_;

  const base::TickClock* tick_clock_;
  base::TimeTicks task_start_time_;

  DISALLOW_COPY_AND_ASSIGN(DnsTask);
};

//-----------------------------------------------------------------------------

struct HostResolverManager::JobKey {
  bool operator<(const JobKey& other) const {
    return std::tie(query_type, flags, source, request_context, hostname) <
           std::tie(other.query_type, other.flags, other.source,
                    other.request_context, other.hostname);
  }

  std::string hostname;
  DnsQueryType query_type;
  HostResolverFlags flags;
  HostResolverSource source;
  URLRequestContext* request_context;
};

// Aggregates all Requests for the same Key. Dispatched via PriorityDispatch.
class HostResolverManager::Job : public PrioritizedDispatcher::Job,
                                 public HostResolverManager::DnsTask::Delegate {
 public:
  // Creates new job for |key| where |request_net_log| is bound to the
  // request that spawned it.
  Job(const base::WeakPtr<HostResolverManager>& resolver,
      base::StringPiece hostname,
      DnsQueryType query_type,
      HostResolverFlags host_resolver_flags,
      HostResolverSource requested_source,
      ResolveHostParameters::CacheUsage cache_usage,
      URLRequestContext* request_context,
      HostCache* host_cache,
      std::deque<TaskType> tasks,
      RequestPriority priority,
      scoped_refptr<base::TaskRunner> proc_task_runner,
      const NetLogWithSource& source_net_log,
      const base::TickClock* tick_clock)
      : resolver_(resolver),
        hostname_(hostname),
        query_type_(query_type),
        host_resolver_flags_(host_resolver_flags),
        requested_source_(requested_source),
        cache_usage_(cache_usage),
        request_context_(request_context),
        host_cache_(host_cache),
        tasks_(tasks),
        job_running_(false),
        priority_tracker_(priority),
        proc_task_runner_(std::move(proc_task_runner)),
        had_non_speculative_request_(false),
        num_occupied_job_slots_(0),
        dns_task_error_(OK),
        tick_clock_(tick_clock),
        net_log_(
            NetLogWithSource::Make(source_net_log.net_log(),
                                   NetLogSourceType::HOST_RESOLVER_IMPL_JOB)) {
    source_net_log.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_CREATE_JOB);

    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB, [&] {
      return NetLogJobCreationParams(source_net_log.source(), hostname_);
    });
  }

  ~Job() override {
    if (is_running()) {
      // |resolver_| was destroyed with this Job still in flight.
      // Clean-up, record in the log, but don't run any callbacks.
      proc_task_ = nullptr;
      // Clean up now for nice NetLog.
      KillDnsTask();
      net_log_.EndEventWithNetErrorCode(NetLogEventType::HOST_RESOLVER_IMPL_JOB,
                                        ERR_ABORTED);
    } else if (is_queued()) {
      // |resolver_| was destroyed without running this Job.
      // TODO(szym): is there any benefit in having this distinction?
      net_log_.AddEvent(NetLogEventType::CANCELLED);
      net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB);
    }
    // else CompleteRequests logged EndEvent.
    while (!requests_.empty()) {
      // Log any remaining Requests as cancelled.
      RequestImpl* req = requests_.head()->value();
      req->RemoveFromList();
      DCHECK_EQ(this, req->job());
      LogCancelRequest(req->source_net_log());
      req->OnJobCancelled(this);
    }
  }

  // Add this job to the dispatcher.  If "at_head" is true, adds at the front
  // of the queue.
  void Schedule(bool at_head) {
    DCHECK(!is_queued());
    PrioritizedDispatcher::Handle handle;
    if (!at_head) {
      handle = resolver_->dispatcher_->Add(this, priority());
    } else {
      handle = resolver_->dispatcher_->AddAtHead(this, priority());
    }
    // The dispatcher could have started |this| in the above call to Add, which
    // could have called Schedule again. In that case |handle| will be null,
    // but |handle_| may have been set by the other nested call to Schedule.
    if (!handle.is_null()) {
      DCHECK(handle_.is_null());
      handle_ = handle;
    }
  }

  void AddRequest(RequestImpl* request) {
    // Job currently assumes a 1:1 correspondence between URLRequestContext and
    // HostCache. Since the URLRequestContext is part of the JobKey, any request
    // added to any existing Job should share the same HostCache.
    DCHECK_EQ(host_cache_, request->host_cache());
    DCHECK_EQ(hostname_, request->request_host().host());

    request->AssignJob(this);

    priority_tracker_.Add(request->priority());

    request->source_net_log().AddEventReferencingSource(
        NetLogEventType::HOST_RESOLVER_IMPL_JOB_ATTACH, net_log_.source());

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_REQUEST_ATTACH,
                      [&] {
                        return NetLogJobAttachParams(
                            request->source_net_log().source(), priority());
                      });

    if (!request->parameters().is_speculative)
      had_non_speculative_request_ = true;

    requests_.Append(request);

    UpdatePriority();
  }

  void ChangeRequestPriority(RequestImpl* req, RequestPriority priority) {
    DCHECK_EQ(hostname_, req->request_host().host());

    priority_tracker_.Remove(req->priority());
    req->set_priority(priority);
    priority_tracker_.Add(req->priority());
    UpdatePriority();
  }

  // Detach cancelled request. If it was the last active Request, also finishes
  // this Job.
  void CancelRequest(RequestImpl* request) {
    DCHECK_EQ(hostname_, request->request_host().host());
    DCHECK(!requests_.empty());

    LogCancelRequest(request->source_net_log());

    priority_tracker_.Remove(request->priority());
    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_REQUEST_DETACH,
                      [&] {
                        return NetLogJobAttachParams(
                            request->source_net_log().source(), priority());
                      });

    if (num_active_requests() > 0) {
      UpdatePriority();
      request->RemoveFromList();
    } else {
      // If we were called from a Request's callback within CompleteRequests,
      // that Request could not have been cancelled, so num_active_requests()
      // could not be 0. Therefore, we are not in CompleteRequests().
      CompleteRequestsWithError(ERR_FAILED /* cancelled */);
    }
  }

  // Called from AbortAllJobs. Completes all requests and destroys
  // the job. This currently assumes the abort is due to a network change.
  // TODO This should not delete |this|.
  void Abort() {
    CompleteRequestsWithError(ERR_NETWORK_CHANGED);
  }

  // Gets a closure that will abort a DnsTask (see AbortDnsTask()) iff |this| is
  // still valid. Useful if aborting a list of Jobs as some may be cancelled
  // while aborting others.
  base::OnceClosure GetAbortDnsTaskClosure(int error, bool fallback_only) {
    return base::BindOnce(&Job::AbortDnsTask, weak_ptr_factory_.GetWeakPtr(),
                          error, fallback_only);
  }

  // Aborts or removes any current/future DnsTasks if a ProcTask is available
  // for fallback. If no fallback is available and |fallback_only| is false, a
  // job that is currently running a DnsTask will be completed with |error|.
  void AbortDnsTask(int error, bool fallback_only) {
    bool has_proc_fallback =
        std::find(tasks_.begin(), tasks_.end(), TaskType::PROC) != tasks_.end();
    if (has_proc_fallback) {
      for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (*it == TaskType::DNS || *it == TaskType::SECURE_DNS)
          it = tasks_.erase(it);
        else
          ++it;
      }
    }

    if (dns_task_) {
      if (has_proc_fallback) {
        KillDnsTask();
        dns_task_error_ = OK;
        RunNextTask();
      } else if (!fallback_only) {
        CompleteRequestsWithError(error);
      }
    }
  }

  // Called by HostResolverManager when this job is evicted due to queue
  // overflow. Completes all requests and destroys the job.
  void OnEvicted(bool complete_asynchronously) {
    DCHECK(!is_running());
    DCHECK(is_queued());
    handle_.Reset();

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_EVICTED);

    // This signals to CompleteRequests that this job never ran.
    if (complete_asynchronously) {
      // Job must be saved in |resolver_| to be completed asynchronously.
      // Otherwise the job will be destroyed with requests silently cancelled
      // before completion runs.
      DCHECK(self_iterator_);
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&Job::CompleteRequestsWithError,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));
    } else {
      // Should not complete synchronously with eviction if requests are
      // attached to the job because those requests will invoke callbacks that
      // could result in reentrancy.
      DCHECK_EQ(0u, num_active_requests());
      CompleteRequestsWithError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE);
    }
  }

  // Attempts to serve the job from HOSTS. Returns true if succeeded and
  // this Job was destroyed.
  bool ServeFromHosts() {
    DCHECK_GT(num_active_requests(), 0u);
    base::Optional<HostCache::Entry> results = resolver_->ServeFromHosts(
        hostname_, query_type_,
        host_resolver_flags_ & HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6);
    if (results) {
      // This will destroy the Job.
      CompleteRequests(results.value(), base::TimeDelta(),
                       true /* allow_cache */, true /* secure */);
      return true;
    }
    return false;
  }

  void OnAddedToJobMap(JobMap::iterator iterator) {
    DCHECK(!self_iterator_);
    DCHECK(iterator != resolver_->jobs_.end());
    self_iterator_ = iterator;
  }

  void OnRemovedFromJobMap() {
    DCHECK(self_iterator_);
    self_iterator_ = base::nullopt;
  }

  bool is_queued() const { return !handle_.is_null(); }

  bool is_running() const { return job_running_; }

 private:
  HostCache::Key GenerateCacheKey(bool secure) const {
    HostCache::Key cache_key(hostname_, query_type_, host_resolver_flags_,
                             requested_source_);
    cache_key.secure = secure;
    return cache_key;
  }

  void KillDnsTask() {
    if (dns_task_) {
      ReduceToOneJobSlot();
      dns_task_.reset();
    }
  }

  // Reduce the number of job slots occupied and queued in the dispatcher
  // to one. If the second Job slot is queued in the dispatcher, cancels the
  // queued job. Otherwise, the second Job has been started by the
  // PrioritizedDispatcher, so signals it is complete.
  void ReduceToOneJobSlot() {
    DCHECK_GE(num_occupied_job_slots_, 1u);
    if (is_queued()) {
      resolver_->dispatcher_->Cancel(handle_);
      handle_.Reset();
    } else if (num_occupied_job_slots_ > 1) {
      resolver_->dispatcher_->OnJobFinished();
      --num_occupied_job_slots_;
    }
    DCHECK_EQ(1u, num_occupied_job_slots_);
  }

  void UpdatePriority() {
    if (is_queued())
      handle_ = resolver_->dispatcher_->ChangePriority(handle_, priority());
  }

  void RunNextTask() {
    // If there are no tasks left to try, cache any stored results and complete
    // the request with the last stored result. All stored results should be
    // errors.
    if (tasks_.empty()) {
      // If there are no stored results, complete with an error.
      if (completion_results_.size() == 0) {
        CompleteRequestsWithError(ERR_NAME_NOT_RESOLVED);
        return;
      }

      // Cache all but the last result here. The last result will be cached
      // as part of CompleteRequests.
      for (size_t i = 0; i < completion_results_.size() - 1; ++i) {
        const auto& result = completion_results_[i];
        DCHECK_NE(OK, result.entry.error());
        MaybeCacheResult(result.entry, result.ttl, result.secure);
      }
      const auto& last_result = completion_results_.back();
      DCHECK_NE(OK, last_result.entry.error());
      CompleteRequests(last_result.entry, last_result.ttl,
                       true /* allow_cache */, last_result.secure);
      return;
    }

    TaskType next_task = tasks_.front();
    tasks_.pop_front();
    switch (next_task) {
      case TaskType::PROC:
        StartProcTask();
        break;
      case TaskType::DNS:
        StartDnsTask(false /* secure */);
        break;
      case TaskType::SECURE_DNS:
        StartDnsTask(true /* secure */);
        break;
      case TaskType::MDNS:
        StartMdnsTask();
        break;
      case TaskType::INSECURE_CACHE_LOOKUP:
        InsecureCacheLookup();
        break;
      case TaskType::SECURE_CACHE_LOOKUP:
      case TaskType::CACHE_LOOKUP:
        // These task types should have been handled synchronously in
        // ResolveLocally() prior to Job creation.
        NOTREACHED();
        break;
    }
  }

  // PriorityDispatch::Job:
  void Start() override {
    DCHECK_LE(num_occupied_job_slots_, 1u);

    handle_.Reset();
    ++num_occupied_job_slots_;

    if (num_occupied_job_slots_ == 2) {
      StartSecondDnsTransaction();
      return;
    }

    DCHECK(!is_running());
    job_running_ = true;

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_STARTED);
    start_time_ = tick_clock_->NowTicks();
    DCHECK(!tasks_.empty());
    RunNextTask();
    // Caution: Job::Start must not complete synchronously.
  }

  // TODO(szym): Since DnsTransaction does not consume threads, we can increase
  // the limits on |dispatcher_|. But in order to keep the number of
  // ThreadPool threads low, we will need to use an "inner"
  // PrioritizedDispatcher with tighter limits.
  void StartProcTask() {
    DCHECK(IsAddressType(query_type_));

    proc_task_ = std::make_unique<ProcTask>(
        hostname_, HostResolver::DnsQueryTypeToAddressFamily(query_type_),
        host_resolver_flags_, resolver_->proc_params_,
        base::BindOnce(&Job::OnProcTaskComplete, base::Unretained(this),
                       tick_clock_->NowTicks()),
        proc_task_runner_, net_log_, tick_clock_);

    // Start() could be called from within Resolve(), hence it must NOT directly
    // call OnProcTaskComplete, for example, on synchronous failure.
    proc_task_->Start();
  }

  // Called by ProcTask when it completes.
  void OnProcTaskComplete(base::TimeTicks start_time,
                          int net_error,
                          const AddressList& addr_list) {
    DCHECK(proc_task_);

    if (dns_task_error_ != OK) {
      // This ProcTask was a fallback resolution after a failed DnsTask.
      if (net_error == OK) {
        resolver_->OnFallbackResolve(dns_task_error_);
      }
    }

    if (ContainsIcannNameCollisionIp(addr_list))
      net_error = ERR_ICANN_NAME_COLLISION;

    base::TimeDelta ttl =
        base::TimeDelta::FromSeconds(kNegativeCacheEntryTTLSeconds);
    if (net_error == OK)
      ttl = base::TimeDelta::FromSeconds(kCacheEntryTTLSeconds);

    // Source unknown because the system resolver could have gotten it from a
    // hosts file, its own cache, a DNS lookup or somewhere else.
    // Don't store the |ttl| in cache since it's not obtained from the server.
    CompleteRequests(
        HostCache::Entry(net_error,
                         net_error == OK
                             ? AddressList::CopyWithPort(addr_list, 0)
                             : AddressList(),
                         HostCache::Entry::SOURCE_UNKNOWN),
        ttl, true /* allow_cache */, false /* secure */);
  }

  void InsecureCacheLookup() {
    // Insecure cache lookups for requests allowing stale results should have
    // occurred prior to Job creation.
    DCHECK(cache_usage_ != ResolveHostParameters::CacheUsage::STALE_ALLOWED);
    base::Optional<HostCache::EntryStaleness> stale_info;
    base::Optional<HostCache::Entry> resolved = resolver_->MaybeServeFromCache(
        host_cache_, GenerateCacheKey(false), cache_usage_,
        false /* ignore_secure */, net_log_, &stale_info);

    if (resolved) {
      DCHECK(stale_info);
      DCHECK(!stale_info.value().is_stale());
      CompleteRequestsWithoutCache(resolved.value(), std::move(stale_info));
    } else {
      RunNextTask();
    }
  }

  void StartDnsTask(bool secure) {
    DCHECK_EQ(1u, num_occupied_job_slots_);
    // Need to create the task even if we're going to post a failure instead of
    // running it, as a "started" job needs a task to be properly cleaned up.
    dns_task_.reset(new DnsTask(resolver_->dns_client_.get(), hostname_,
                                query_type_, request_context_, secure, this,
                                net_log_, tick_clock_));

    if (resolver_->HaveDnsConfig()) {
      dns_task_->StartFirstTransaction();
      // Schedule a second transaction, if needed.
      if (dns_task_->needs_two_transactions())
        Schedule(true);
    } else {
      // Cannot start a DNS task when DnsClient or config is not available.
      // Since we cannot complete synchronously from here, post a failure.
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(
              &Job::OnDnsTaskFailure, weak_ptr_factory_.GetWeakPtr(),
              dns_task_->AsWeakPtr(), base::TimeDelta(),
              HostCache::Entry(ERR_FAILED, HostCache::Entry::SOURCE_UNKNOWN),
              secure));
    }
  }

  void StartSecondDnsTransaction() {
    DCHECK(dns_task_);
    DCHECK(dns_task_->needs_two_transactions());
    dns_task_->StartSecondTransaction();
  }

  // Called if DnsTask fails. It is posted from StartDnsTask, so Job may be
  // deleted before this callback. In this case dns_task is deleted as well,
  // so we use it as indicator whether Job is still valid.
  void OnDnsTaskFailure(const base::WeakPtr<DnsTask>& dns_task,
                        base::TimeDelta duration,
                        const HostCache::Entry& failure_results,
                        bool secure) {
    DCHECK_NE(OK, failure_results.error());

    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.DnsTask.FailureTime", duration);

    if (!dns_task)
      return;

    if (duration < base::TimeDelta::FromMilliseconds(10)) {
      base::UmaHistogramSparse(
          secure ? "Net.DNS.SecureDnsTask.ErrorBeforeFallback.Fast"
                 : "Net.DNS.DnsTask.ErrorBeforeFallback.Fast",
          std::abs(failure_results.error()));
    } else {
      base::UmaHistogramSparse(
          secure ? "Net.DNS.SecureDnsTask.ErrorBeforeFallback.Slow"
                 : "Net.DNS.DnsTask.ErrorBeforeFallback.Slow",
          std::abs(failure_results.error()));
    }

    // If one of the fallback tasks doesn't complete the request, store a result
    // to use during request completion.
    base::TimeDelta ttl = failure_results.has_ttl()
                              ? failure_results.ttl()
                              : base::TimeDelta::FromSeconds(0);
    completion_results_.push_back({failure_results, ttl, secure});

    dns_task_error_ = failure_results.error();
    KillDnsTask();
    RunNextTask();
  }

  // HostResolverManager::DnsTask::Delegate implementation:

  void OnDnsTaskComplete(base::TimeTicks start_time,
                         const HostCache::Entry& results,
                         bool secure) override {
    DCHECK(dns_task_);

    base::TimeDelta duration = tick_clock_->NowTicks() - start_time;
    if (results.error() != OK) {
      OnDnsTaskFailure(dns_task_->AsWeakPtr(), duration, results, secure);
      return;
    }

    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.DnsTask.SuccessTime", duration);

    resolver_->OnDnsTaskResolve();

    base::TimeDelta bounded_ttl = std::max(
        results.ttl(), base::TimeDelta::FromSeconds(kMinimumTTLSeconds));

    if (results.addresses() &&
        ContainsIcannNameCollisionIp(results.addresses().value())) {
      CompleteRequestsWithError(ERR_ICANN_NAME_COLLISION);
      return;
    }

    CompleteRequests(results, bounded_ttl, true /* allow_cache */, secure);
  }

  void OnFirstDnsTransactionComplete() override {
    DCHECK(dns_task_->needs_two_transactions());
    DCHECK_EQ(dns_task_->needs_another_transaction(), is_queued());
    // No longer need to occupy two dispatcher slots.
    ReduceToOneJobSlot();

    // We already have a job slot at the dispatcher, so if the second
    // transaction hasn't started, reuse it now instead of waiting in the queue
    // for the second slot.
    if (dns_task_->needs_another_transaction())
      dns_task_->StartSecondTransaction();
  }

  void StartMdnsTask() {
    // No flags are supported for MDNS except
    // HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6 (which is not actually an
    // input flag).
    DCHECK_EQ(0, host_resolver_flags_ &
                     ~HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6);

    std::vector<DnsQueryType> query_types;
    if (query_type_ == DnsQueryType::UNSPECIFIED) {
      query_types.push_back(DnsQueryType::A);
      query_types.push_back(DnsQueryType::AAAA);
    } else {
      query_types.push_back(query_type_);
    }

    MDnsClient* client;
    int rv = resolver_->GetOrCreateMdnsClient(&client);
    mdns_task_ =
        std::make_unique<HostResolverMdnsTask>(client, hostname_, query_types);

    if (rv == OK) {
      mdns_task_->Start(
          base::BindOnce(&Job::OnMdnsTaskComplete, base::Unretained(this)));
    } else {
      // Could not create an mDNS client. Since we cannot complete synchronously
      // from here, post a failure without starting the task.
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&Job::OnMdnsImmediateFailure,
                                    weak_ptr_factory_.GetWeakPtr(), rv));
    }
  }

  void OnMdnsTaskComplete() {
    DCHECK(mdns_task_);
    // TODO(crbug.com/846423): Consider adding MDNS-specific logging.

    HostCache::Entry results = mdns_task_->GetResults();
    if (results.addresses() &&
        ContainsIcannNameCollisionIp(results.addresses().value())) {
      CompleteRequestsWithError(ERR_ICANN_NAME_COLLISION);
    } else {
      // MDNS uses a separate cache, so skip saving result to cache.
      // TODO(crbug.com/926300): Consider merging caches.
      CompleteRequestsWithoutCache(results, base::nullopt /* stale_info */);
    }
  }

  void OnMdnsImmediateFailure(int rv) {
    DCHECK(mdns_task_);
    DCHECK_NE(OK, rv);

    CompleteRequestsWithError(rv);
  }

  void RecordJobHistograms(int error) {
    // Used in UMA_HISTOGRAM_ENUMERATION. Do not renumber entries or reuse
    // deprecated values.
    enum Category {
      RESOLVE_SUCCESS = 0,
      RESOLVE_FAIL = 1,
      RESOLVE_SPECULATIVE_SUCCESS = 2,
      RESOLVE_SPECULATIVE_FAIL = 3,
      RESOLVE_ABORT = 4,
      RESOLVE_SPECULATIVE_ABORT = 5,
      RESOLVE_MAX,  // Bounding value.
    };
    Category category = RESOLVE_MAX;  // Illegal value for later DCHECK only.

    base::TimeDelta duration = tick_clock_->NowTicks() - start_time_;
    if (error == OK) {
      if (had_non_speculative_request_) {
        category = RESOLVE_SUCCESS;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime", duration);
        switch (query_type_) {
          case DnsQueryType::A:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV4",
                                         duration);
            break;
          case DnsQueryType::AAAA:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV6",
                                         duration);
            break;
          case DnsQueryType::UNSPECIFIED:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.UNSPEC",
                                         duration);
            break;
          default:
            // No histogram for other query types.
            break;
        }
      } else {
        category = RESOLVE_SPECULATIVE_SUCCESS;
      }
    } else if (error == ERR_NETWORK_CHANGED ||
               error == ERR_HOST_RESOLVER_QUEUE_TOO_LARGE) {
      category = had_non_speculative_request_ ? RESOLVE_ABORT
                                              : RESOLVE_SPECULATIVE_ABORT;
    } else {
      if (had_non_speculative_request_) {
        category = RESOLVE_FAIL;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime", duration);
        switch (query_type_) {
          case DnsQueryType::A:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime.IPV4",
                                         duration);
            break;
          case DnsQueryType::AAAA:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime.IPV6",
                                         duration);
            break;
          case DnsQueryType::UNSPECIFIED:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime.UNSPEC",
                                         duration);
            break;
          default:
            // No histogram for other query types.
            break;
        }
      } else {
        category = RESOLVE_SPECULATIVE_FAIL;
      }
    }
    DCHECK_LT(static_cast<int>(category),
              static_cast<int>(RESOLVE_MAX));  // Be sure it was set.
    UMA_HISTOGRAM_ENUMERATION("Net.DNS.ResolveCategory", category, RESOLVE_MAX);

    if (category == RESOLVE_FAIL || category == RESOLVE_ABORT) {
      if (duration < base::TimeDelta::FromMilliseconds(10))
        base::UmaHistogramSparse("Net.DNS.ResolveError.Fast", std::abs(error));
      else
        base::UmaHistogramSparse("Net.DNS.ResolveError.Slow", std::abs(error));
    }
  }

  void MaybeCacheResult(const HostCache::Entry& results,
                        base::TimeDelta ttl,
                        bool secure) {
    // If the request did not complete, don't cache it.
    if (!results.did_complete())
      return;
    HostCache::Key cache_key = GenerateCacheKey(secure);
    resolver_->CacheResult(host_cache_, cache_key, results, ttl);
  }

  // Performs Job's last rites. Completes all Requests. Deletes this.
  //
  // If not |allow_cache|, result will not be stored in the host cache, even if
  // result would otherwise allow doing so. Update the key to reflect |secure|,
  // which indicates whether or not the result was obtained securely.
  void CompleteRequests(const HostCache::Entry& results,
                        base::TimeDelta ttl,
                        bool allow_cache,
                        bool secure) {
    CHECK(resolver_.get());

    // This job must be removed from resolver's |jobs_| now to make room for a
    // new job with the same key in case one of the OnComplete callbacks decides
    // to spawn one. Consequently, if the job was owned by |jobs_|, the job
    // deletes itself when CompleteRequests is done.
    std::unique_ptr<Job> self_deleter;
    if (self_iterator_)
      self_deleter = resolver_->RemoveJob(self_iterator_.value());

    if (is_running()) {
      proc_task_ = nullptr;
      KillDnsTask();
      mdns_task_ = nullptr;

      // Signal dispatcher that a slot has opened.
      DCHECK_EQ(1u, num_occupied_job_slots_);
      resolver_->dispatcher_->OnJobFinished();
    } else if (is_queued()) {
      resolver_->dispatcher_->Cancel(handle_);
      handle_.Reset();
    }

    if (num_active_requests() == 0) {
      net_log_.AddEvent(NetLogEventType::CANCELLED);
      net_log_.EndEventWithNetErrorCode(NetLogEventType::HOST_RESOLVER_IMPL_JOB,
                                        OK);
      return;
    }

    net_log_.EndEventWithNetErrorCode(NetLogEventType::HOST_RESOLVER_IMPL_JOB,
                                      results.error());

    DCHECK(!requests_.empty());

    // Handle all caching before completing requests as completing requests may
    // start new requests that rely on cached results.
    if (allow_cache)
      MaybeCacheResult(results, ttl, secure);

    RecordJobHistograms(results.error());

    // Complete all of the requests that were attached to the job and
    // detach them.
    while (!requests_.empty()) {
      RequestImpl* req = requests_.head()->value();
      req->RemoveFromList();
      DCHECK_EQ(this, req->job());
      // Update the net log and notify registered observers.
      LogFinishRequest(req->source_net_log(), results.error());
      if (results.did_complete()) {
        // Record effective total time from creation to completion.
        resolver_->RecordTotalTime(
            req->parameters().is_speculative, false /* from_cache */,
            tick_clock_->NowTicks() - req->request_time());
      }
      if (results.error() == OK && !req->parameters().is_speculative) {
        req->set_results(
            results.CopyWithDefaultPort(req->request_host().port()));
      }
      req->OnJobCompleted(this, results.error());

      // Check if the resolver was destroyed as a result of running the
      // callback. If it was, we could continue, but we choose to bail.
      if (!resolver_.get())
        return;
    }
  }

  void CompleteRequestsWithoutCache(
      const HostCache::Entry& results,
      base::Optional<HostCache::EntryStaleness> stale_info) {
    // Record the stale_info for all non-speculative requests, if it exists.
    if (stale_info) {
      for (auto* node = requests_.head(); node != requests_.end();
           node = node->next()) {
        if (!node->value()->parameters().is_speculative)
          node->value()->set_stale_info(std::move(stale_info).value());
      }
    }
    CompleteRequests(results, base::TimeDelta(), false /* allow_cache */,
                     false /* secure */);
  }

  // Convenience wrapper for CompleteRequests in case of failure.
  void CompleteRequestsWithError(int net_error) {
    DCHECK_NE(OK, net_error);
    CompleteRequests(
        HostCache::Entry(net_error, HostCache::Entry::SOURCE_UNKNOWN),
        base::TimeDelta(), true /* allow_cache */, false /* secure */);
  }

  RequestPriority priority() const override {
    return priority_tracker_.highest_priority();
  }

  // Number of non-canceled requests in |requests_|.
  size_t num_active_requests() const { return priority_tracker_.total_count(); }

  base::WeakPtr<HostResolverManager> resolver_;

  const std::string hostname_;
  const DnsQueryType query_type_;
  const HostResolverFlags host_resolver_flags_;
  const HostResolverSource requested_source_;
  const ResolveHostParameters::CacheUsage cache_usage_;
  URLRequestContext* const request_context_;
  // TODO(crbug.com/969847): Consider allowing requests within a single Job to
  // have different HostCaches.
  HostCache* const host_cache_;

  struct CompletionResult {
    const HostCache::Entry entry;
    base::TimeDelta ttl;
    bool secure;
  };

  // Results to use in last-ditch attempt to complete request.
  std::vector<CompletionResult> completion_results_;

  // The sequence of tasks to run in this Job. Tasks may be aborted and removed
  // from the sequence, but otherwise the tasks will run in order until a
  // successful result is found.
  std::deque<TaskType> tasks_;

  // Whether the job is running.
  bool job_running_;

  // Tracks the highest priority across |requests_|.
  PriorityTracker priority_tracker_;

  // Task runner used for HostResolverProc.
  scoped_refptr<base::TaskRunner> proc_task_runner_;

  bool had_non_speculative_request_;

  // Number of slots occupied by this Job in resolver's PrioritizedDispatcher.
  unsigned num_occupied_job_slots_;

  // Result of DnsTask.
  int dns_task_error_;

  const base::TickClock* tick_clock_;
  base::TimeTicks start_time_;

  NetLogWithSource net_log_;

  // Resolves the host using a HostResolverProc.
  std::unique_ptr<ProcTask> proc_task_;

  // Resolves the host using a DnsTransaction.
  std::unique_ptr<DnsTask> dns_task_;

  // Resolves the host using MDnsClient.
  std::unique_ptr<HostResolverMdnsTask> mdns_task_;

  // All Requests waiting for the result of this Job. Some can be canceled.
  base::LinkedList<RequestImpl> requests_;

  // A handle used in |HostResolverManager::dispatcher_|.
  PrioritizedDispatcher::Handle handle_;

  // Iterator to |this| in the JobMap. |nullopt| if not owned by the JobMap.
  base::Optional<JobMap::iterator> self_iterator_;

  base::WeakPtrFactory<Job> weak_ptr_factory_{this};
};

//-----------------------------------------------------------------------------

HostResolverManager::HostResolverManager(
    const HostResolver::ManagerOptions& options,
    NetLog* net_log,
    DnsClientFactory dns_client_factory_for_testing)
    : max_queued_jobs_(0),
      proc_params_(nullptr, options.max_system_retry_attempts),
      net_log_(net_log),
      dns_client_factory_for_testing_(
          std::move(dns_client_factory_for_testing)),
      received_dns_config_(false),
      dns_config_overrides_(options.dns_config_overrides),
      num_dns_failures_(0),
      check_ipv6_on_wifi_(options.check_ipv6_on_wifi),
      use_local_ipv6_(false),
      last_ipv6_probe_result_(true),
      additional_resolver_flags_(0),
      use_proctask_by_default_(false),
      allow_fallback_to_proctask_(true),
      tick_clock_(base::DefaultTickClock::GetInstance()),
      invalidation_in_progress_(false) {
  PrioritizedDispatcher::Limits job_limits = GetDispatcherLimits(options);
  dispatcher_.reset(new PrioritizedDispatcher(job_limits));
  max_queued_jobs_ = job_limits.total_jobs * 100u;

  DCHECK_GE(dispatcher_->num_priorities(), static_cast<size_t>(NUM_PRIORITIES));

  proc_task_runner_ = base::CreateTaskRunnerWithTraits(
      {base::MayBlock(), priority_mode.Get(),
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN});

#if defined(OS_WIN)
  EnsureWinsockInit();
#endif
#if (defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)) || \
    defined(OS_FUCHSIA)
  RunLoopbackProbeJob();
#endif
  NetworkChangeNotifier::AddIPAddressObserver(this);
  NetworkChangeNotifier::AddConnectionTypeObserver(this);
  NetworkChangeNotifier::AddDNSObserver(this);
#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_OPENBSD) && \
    !defined(OS_ANDROID)
  EnsureDnsReloaderInit();
#endif

  OnConnectionTypeChanged(NetworkChangeNotifier::GetConnectionType());

  SetDnsClientEnabled(options.dns_client_enabled);

  {
    DnsConfig dns_config = GetBaseDnsConfig(false);
    // Conservatively assume local IPv6 is needed when DnsConfig is not valid.
    use_local_ipv6_ = !dns_config.IsValid() || dns_config.use_local_ipv6;
    UpdateModeForHistogram(dns_config);
  }

  allow_fallback_to_proctask_ = !ConfigureAsyncDnsNoFallbackFieldTrial();
}

HostResolverManager::~HostResolverManager() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Prevent the dispatcher from starting new jobs.
  dispatcher_->SetLimitsToZero();
  // It's now safe for Jobs to call KillDnsTask on destruction, because
  // OnJobComplete will not start any new jobs.
  jobs_.clear();

  NetworkChangeNotifier::RemoveIPAddressObserver(this);
  NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
  NetworkChangeNotifier::RemoveDNSObserver(this);
}

std::unique_ptr<HostResolverManager::CancellableRequest>
HostResolverManager::CreateRequest(
    const HostPortPair& host,
    const NetLogWithSource& net_log,
    const base::Optional<ResolveHostParameters>& optional_parameters,
    URLRequestContext* request_context,
    HostCache* host_cache) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!invalidation_in_progress_);

  // HostCaches must add invalidators (via AddHostCacheInvalidator()) before use
  // to ensure they are invalidated on network and configuration changes.
  if (host_cache)
    DCHECK(host_cache_invalidators_.HasObserver(host_cache->invalidator()));

  return std::make_unique<RequestImpl>(net_log, host, optional_parameters,
                                       request_context, host_cache,
                                       weak_ptr_factory_.GetWeakPtr());
}

std::unique_ptr<HostResolver::MdnsListener>
HostResolverManager::CreateMdnsListener(const HostPortPair& host,
                                        DnsQueryType query_type) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK_NE(DnsQueryType::UNSPECIFIED, query_type);

  auto listener =
      std::make_unique<HostResolverMdnsListenerImpl>(host, query_type);

  MDnsClient* client;
  int rv = GetOrCreateMdnsClient(&client);

  if (rv == OK) {
    std::unique_ptr<net::MDnsListener> inner_listener = client->CreateListener(
        DnsQueryTypeToQtype(query_type), host.host(), listener.get());
    listener->set_inner_listener(std::move(inner_listener));
  } else {
    listener->set_initialization_error(rv);
  }
  return listener;
}

void HostResolverManager::SetDnsClientEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (enabled && !dns_client_) {
    if (dns_client_factory_for_testing_) {
      SetDnsClient(dns_client_factory_for_testing_.Run(net_log_));
    } else {
#if defined(ENABLE_BUILT_IN_DNS)
      SetDnsClient(DnsClient::CreateClient(net_log_));
#endif
    }
    return;
  }

  if (!enabled && dns_client_) {
    SetDnsClient(nullptr);
  }
}

std::unique_ptr<base::Value> HostResolverManager::GetDnsConfigAsValue() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Check if async DNS is disabled.
  if (!dns_client_.get())
    return nullptr;

  // Check if async DNS is enabled, but we currently have no configuration
  // for it.
  const DnsConfig* dns_config = dns_client_->GetConfig();
  if (!dns_config)
    return std::make_unique<base::DictionaryValue>();

  return dns_config->ToValue();
}

void HostResolverManager::SetDnsConfigOverrides(
    const DnsConfigOverrides& overrides) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (dns_config_overrides_ == overrides)
    return;

  dns_config_overrides_ = overrides;
  if (dns_client_.get())
    UpdateDNSConfig(true);
}

void HostResolverManager::AddHostCacheInvalidator(
    HostCache::Invalidator* invalidator) {
  host_cache_invalidators_.AddObserver(invalidator);
}

void HostResolverManager::RemoveHostCacheInvalidator(
    const HostCache::Invalidator* invalidator) {
  host_cache_invalidators_.RemoveObserver(invalidator);
}

const std::vector<DnsConfig::DnsOverHttpsServerConfig>*
HostResolverManager::GetDnsOverHttpsServersForTesting() const {
  if (!dns_config_overrides_.dns_over_https_servers ||
      dns_config_overrides_.dns_over_https_servers.value().empty()) {
    return nullptr;
  }
  return &dns_config_overrides_.dns_over_https_servers.value();
}

void HostResolverManager::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  tick_clock_ = tick_clock;
}

void HostResolverManager::SetMaxQueuedJobsForTesting(size_t value) {
  DCHECK_EQ(0u, dispatcher_->num_queued_jobs());
  DCHECK_GE(value, 0u);
  max_queued_jobs_ = value;
}

void HostResolverManager::SetHaveOnlyLoopbackAddresses(bool result) {
  if (result) {
    additional_resolver_flags_ |= HOST_RESOLVER_LOOPBACK_ONLY;
  } else {
    additional_resolver_flags_ &= ~HOST_RESOLVER_LOOPBACK_ONLY;
  }
}

void HostResolverManager::SetMdnsSocketFactoryForTesting(
    std::unique_ptr<MDnsSocketFactory> socket_factory) {
  DCHECK(!mdns_client_);
  mdns_socket_factory_ = std::move(socket_factory);
}

void HostResolverManager::SetMdnsClientForTesting(
    std::unique_ptr<MDnsClient> client) {
  mdns_client_ = std::move(client);
}

void HostResolverManager::SetBaseDnsConfigForTesting(
    const DnsConfig& base_config) {
  test_base_config_ = base_config;
  UpdateDNSConfig(true);
}

void HostResolverManager::SetDnsClientForTesting(
    std::unique_ptr<DnsClient> dns_client) {
  // Use SetDnsClientEnabled(false) to disable.
  DCHECK(dns_client);

  SetDnsClient(std::move(dns_client));
}

void HostResolverManager::SetTaskRunnerForTesting(
    scoped_refptr<base::TaskRunner> task_runner) {
  proc_task_runner_ = std::move(task_runner);
}

int HostResolverManager::Resolve(RequestImpl* request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Request should not yet have a scheduled Job.
  DCHECK(!request->job());
  // Request may only be resolved once.
  DCHECK(!request->complete());
  // MDNS requests do not support skipping cache or stale lookups.
  // TODO(crbug.com/926300): Either add support for skipping the MDNS cache, or
  // merge to use the normal host cache for MDNS requests.
  DCHECK(request->parameters().source != HostResolverSource::MULTICAST_DNS ||
         request->parameters().cache_usage ==
             ResolveHostParameters::CacheUsage::ALLOWED);
  DCHECK(!invalidation_in_progress_);

  request->set_request_time(tick_clock_->NowTicks());

  LogStartRequest(request->source_net_log(), request->request_host());

  DnsQueryType effective_query_type;
  HostResolverFlags effective_host_resolver_flags;
  std::deque<TaskType> tasks;
  base::Optional<HostCache::EntryStaleness> stale_info;
  HostCache::Entry results = ResolveLocally(
      request->request_host().host(), request->parameters().dns_query_type,
      request->parameters().source, request->host_resolver_flags(),
      request->parameters().secure_dns_mode_override,
      request->parameters().cache_usage, request->source_net_log(),
      request->host_cache(), &effective_query_type,
      &effective_host_resolver_flags, &tasks, &stale_info);
  if (results.error() != ERR_DNS_CACHE_MISS ||
      request->parameters().source == HostResolverSource::LOCAL_ONLY) {
    if (results.error() == OK && !request->parameters().is_speculative) {
      request->set_results(
          results.CopyWithDefaultPort(request->request_host().port()));
    }
    if (stale_info && !request->parameters().is_speculative)
      request->set_stale_info(std::move(stale_info).value());
    LogFinishRequest(request->source_net_log(), results.error());
    RecordTotalTime(request->parameters().is_speculative, true /* from_cache */,
                    base::TimeDelta());
    return results.error();
  }

  int rv =
      CreateAndStartJob(effective_query_type, effective_host_resolver_flags,
                        std::move(tasks), request);
  // At this point, expect only async or errors.
  DCHECK_NE(OK, rv);

  return rv;
}

HostCache::Entry HostResolverManager::ResolveLocally(
    const std::string& hostname,
    DnsQueryType dns_query_type,
    HostResolverSource source,
    HostResolverFlags flags,
    base::Optional<DnsConfig::SecureDnsMode> secure_dns_mode_override,
    ResolveHostParameters::CacheUsage cache_usage,
    const NetLogWithSource& source_net_log,
    HostCache* cache,
    DnsQueryType* out_effective_query_type,
    HostResolverFlags* out_effective_host_resolver_flags,
    std::deque<TaskType>* out_tasks,
    base::Optional<HostCache::EntryStaleness>* out_stale_info) {
  DCHECK(out_stale_info);
  *out_stale_info = base::nullopt;

  IPAddress ip_address;
  IPAddress* ip_address_ptr = nullptr;
  if (ip_address.AssignFromIPLiteral(hostname)) {
    ip_address_ptr = &ip_address;
  } else {
    // Check that the caller supplied a valid hostname to resolve. For
    // MULTICAST_DNS, we are less restrictive.
    // TODO(ericorth): Control validation based on an explicit flag rather
    // than implicitly based on |source|.
    const bool is_valid_hostname = source == HostResolverSource::MULTICAST_DNS
                                       ? IsValidUnrestrictedDNSDomain(hostname)
                                       : IsValidDNSDomain(hostname);
    if (!is_valid_hostname) {
      return HostCache::Entry(ERR_NAME_NOT_RESOLVED,
                              HostCache::Entry::SOURCE_UNKNOWN);
    }
  }

  GetEffectiveParametersForRequest(
      hostname, dns_query_type, source, flags, secure_dns_mode_override,
      cache_usage, ip_address_ptr, source_net_log, out_effective_query_type,
      out_effective_host_resolver_flags, out_tasks);

  bool resolve_canonname =
      *out_effective_host_resolver_flags & HOST_RESOLVER_CANONNAME;
  bool default_family_due_to_no_ipv6 =
      *out_effective_host_resolver_flags &
      HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6;

  // The result of |getaddrinfo| for empty hosts is inconsistent across systems.
  // On Windows it gives the default interface's address, whereas on Linux it
  // gives an error. We will make it fail on all platforms for consistency.
  if (hostname.empty() || hostname.size() > kMaxHostLength) {
    return HostCache::Entry(ERR_NAME_NOT_RESOLVED,
                            HostCache::Entry::SOURCE_UNKNOWN);
  }

  base::Optional<HostCache::Entry> resolved =
      ResolveAsIP(*out_effective_query_type, resolve_canonname, ip_address_ptr);
  if (resolved)
    return resolved.value();

  // Special-case localhost names, as per the recommendations in
  // https://tools.ietf.org/html/draft-west-let-localhost-be-localhost.
  resolved = ServeLocalhost(hostname, *out_effective_query_type,
                            default_family_due_to_no_ipv6);
  if (resolved)
    return resolved.value();

  // Do initial cache lookup.
  if (!out_tasks->empty() &&
      (out_tasks->front() == TaskType::SECURE_CACHE_LOOKUP ||
       out_tasks->front() == TaskType::INSECURE_CACHE_LOOKUP ||
       out_tasks->front() == TaskType::CACHE_LOOKUP)) {
    HostCache::Key key(hostname, *out_effective_query_type,
                       *out_effective_host_resolver_flags, source);

    if (out_tasks->front() == TaskType::SECURE_CACHE_LOOKUP)
      key.secure = true;

    bool ignore_secure = false;
    if (out_tasks->front() == TaskType::CACHE_LOOKUP)
      ignore_secure = true;

    out_tasks->pop_front();

    resolved = MaybeServeFromCache(cache, key, cache_usage, ignore_secure,
                                   source_net_log, out_stale_info);
    if (resolved) {
      // |MaybeServeFromCache()| will update |*out_stale_info| as needed.
      DCHECK(out_stale_info->has_value());
      NetLogHostCacheEntry(source_net_log,
                           NetLogEventType::HOST_RESOLVER_IMPL_CACHE_HIT,
                           NetLogEventPhase::NONE, resolved.value());

      return resolved.value();
    }
    DCHECK(!out_stale_info->has_value());
  }

  // TODO(szym): Do not do this if nsswitch.conf instructs not to.
  // http://crbug.com/117655
  resolved = ServeFromHosts(hostname, *out_effective_query_type,
                            default_family_due_to_no_ipv6);
  if (resolved) {
    NetLogHostCacheEntry(source_net_log,
                         NetLogEventType::HOST_RESOLVER_IMPL_HOSTS_HIT,
                         NetLogEventPhase::NONE, resolved.value());
    return resolved.value();
  }

  return HostCache::Entry(ERR_DNS_CACHE_MISS, HostCache::Entry::SOURCE_UNKNOWN);
}

int HostResolverManager::CreateAndStartJob(
    DnsQueryType effective_query_type,
    HostResolverFlags effective_host_resolver_flags,
    std::deque<TaskType> tasks,
    RequestImpl* request) {
  DCHECK(!tasks.empty());
  JobKey key = {request->request_host().host(), effective_query_type,
                effective_host_resolver_flags, request->parameters().source,
                request->request_context()};

  auto jobit = jobs_.find(key);
  Job* job;
  if (jobit == jobs_.end()) {
    auto new_job = std::make_unique<Job>(
        weak_ptr_factory_.GetWeakPtr(), request->request_host().host(),
        effective_query_type, effective_host_resolver_flags,
        request->parameters().source, request->parameters().cache_usage,
        request->request_context(), request->host_cache(), std::move(tasks),
        request->priority(), proc_task_runner_, request->source_net_log(),
        tick_clock_);
    job = new_job.get();
    new_job->Schedule(false);
    DCHECK(new_job->is_running() || new_job->is_queued());

    // Check for queue overflow.
    if (dispatcher_->num_queued_jobs() > max_queued_jobs_) {
      Job* evicted = static_cast<Job*>(dispatcher_->EvictOldestLowest());
      DCHECK(evicted);
      if (evicted == new_job.get()) {
        evicted->OnEvicted(false /* complete_asynchronously */);
        LogFinishRequest(request->source_net_log(),
                         ERR_HOST_RESOLVER_QUEUE_TOO_LARGE);
        return ERR_HOST_RESOLVER_QUEUE_TOO_LARGE;
      } else {
        // |evicted| could have waiting requests that will receive completion
        // callbacks, so cleanup asynchronously to avoid reentrancy.
        evicted->OnEvicted(true /* complete_asynchronously */);
      }
    }

    DCHECK(new_job->is_running() || new_job->is_queued());
    auto insert_result = jobs_.emplace(std::move(key), std::move(new_job));
    DCHECK(insert_result.second);
    job->OnAddedToJobMap(insert_result.first);
  } else {
    job = jobit->second.get();
  }

  // Can't complete synchronously. Attach request and job to each other.
  job->AddRequest(request);
  return ERR_IO_PENDING;
}

base::Optional<HostCache::Entry> HostResolverManager::ResolveAsIP(
    DnsQueryType query_type,
    bool resolve_canonname,
    const IPAddress* ip_address) {
  if (ip_address == nullptr || !IsAddressType(query_type))
    return base::nullopt;

  AddressFamily family = GetAddressFamily(*ip_address);
  if (query_type != DnsQueryType::UNSPECIFIED &&
      query_type != AddressFamilyToDnsQueryType(family)) {
    // Don't return IPv6 addresses for IPv4 queries, and vice versa.
    return HostCache::Entry(ERR_NAME_NOT_RESOLVED,
                            HostCache::Entry::SOURCE_UNKNOWN);
  }

  AddressList addresses = AddressList::CreateFromIPAddress(*ip_address, 0);
  if (resolve_canonname)
    addresses.SetDefaultCanonicalName();
  return HostCache::Entry(OK, std::move(addresses),
                          HostCache::Entry::SOURCE_UNKNOWN);
}

base::Optional<HostCache::Entry> HostResolverManager::MaybeServeFromCache(
    HostCache* cache,
    const HostCache::Key& key,
    ResolveHostParameters::CacheUsage cache_usage,
    bool ignore_secure,
    const NetLogWithSource& source_net_log,
    base::Optional<HostCache::EntryStaleness>* out_stale_info) {
  DCHECK(out_stale_info);
  *out_stale_info = base::nullopt;

  if (!cache)
    return base::nullopt;

  if (cache_usage == ResolveHostParameters::CacheUsage::DISALLOWED)
    return base::nullopt;

  // Local-only requests search the cache for non-local-only results.
  HostCache::Key effective_key = key;
  if (effective_key.host_resolver_source == HostResolverSource::LOCAL_ONLY)
    effective_key.host_resolver_source = HostResolverSource::ANY;

  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result;
  HostCache::EntryStaleness staleness;
  if (cache_usage == ResolveHostParameters::CacheUsage::STALE_ALLOWED) {
    cache_result = cache->LookupStale(effective_key, tick_clock_->NowTicks(),
                                      &staleness, ignore_secure);
  } else {
    DCHECK(cache_usage == ResolveHostParameters::CacheUsage::ALLOWED);
    cache_result =
        cache->Lookup(effective_key, tick_clock_->NowTicks(), ignore_secure);
    staleness = HostCache::kNotStale;
  }
  if (cache_result) {
    *out_stale_info = std::move(staleness);
    NetLogHostCacheEntry(source_net_log,
                         NetLogEventType::HOST_RESOLVER_IMPL_CACHE_HIT,
                         NetLogEventPhase::NONE, cache_result->second);
    return cache_result->second;
  }
  return base::nullopt;
}

base::Optional<HostCache::Entry> HostResolverManager::ServeFromHosts(
    base::StringPiece hostname,
    DnsQueryType query_type,
    bool default_family_due_to_no_ipv6) {
  if (!HaveDnsConfig() || !IsAddressType(query_type))
    return base::nullopt;

  // HOSTS lookups are case-insensitive.
  std::string effective_hostname = base::ToLowerASCII(hostname);

  const DnsHosts& hosts = dns_client_->GetConfig()->hosts;

  // If |address_family| is ADDRESS_FAMILY_UNSPECIFIED other implementations
  // (glibc and c-ares) return the first matching line. We have more
  // flexibility, but lose implicit ordering.
  // We prefer IPv6 because "happy eyeballs" will fall back to IPv4 if
  // necessary.
  AddressList addresses;
  if (query_type == DnsQueryType::AAAA ||
      query_type == DnsQueryType::UNSPECIFIED) {
    auto it = hosts.find(DnsHostsKey(effective_hostname, ADDRESS_FAMILY_IPV6));
    if (it != hosts.end())
      addresses.push_back(IPEndPoint(it->second, 0));
  }

  if (query_type == DnsQueryType::A ||
      query_type == DnsQueryType::UNSPECIFIED) {
    auto it = hosts.find(DnsHostsKey(effective_hostname, ADDRESS_FAMILY_IPV4));
    if (it != hosts.end())
      addresses.push_back(IPEndPoint(it->second, 0));
  }

  // If got only loopback addresses and the family was restricted, resolve
  // again, without restrictions. See SystemHostResolverCall for rationale.
  if (default_family_due_to_no_ipv6 && IsAllIPv4Loopback(addresses)) {
    return ServeFromHosts(hostname, DnsQueryType::UNSPECIFIED, false);
  }

  if (!addresses.empty()) {
    return HostCache::Entry(OK, std::move(addresses),
                            HostCache::Entry::SOURCE_HOSTS);
  }

  return base::nullopt;
}

base::Optional<HostCache::Entry> HostResolverManager::ServeLocalhost(
    base::StringPiece hostname,
    DnsQueryType query_type,
    bool default_family_due_to_no_ipv6) {
  AddressList resolved_addresses;
  if (!IsAddressType(query_type) ||
      !ResolveLocalHostname(hostname, &resolved_addresses)) {
    return base::nullopt;
  }

  AddressList filtered_addresses;
  for (const auto& address : resolved_addresses) {
    // Include the address if:
    // - caller didn't specify an address family, or
    // - caller specifically asked for the address family of this address, or
    // - this is an IPv6 address and caller specifically asked for IPv4 due
    //   to lack of detected IPv6 support. (See SystemHostResolverCall for
    //   rationale).
    if (query_type == DnsQueryType::UNSPECIFIED ||
        HostResolver::DnsQueryTypeToAddressFamily(query_type) ==
            address.GetFamily() ||
        (address.GetFamily() == ADDRESS_FAMILY_IPV6 &&
         query_type == DnsQueryType::A && default_family_due_to_no_ipv6)) {
      filtered_addresses.push_back(address);
    }
  }

  return HostCache::Entry(OK, std::move(filtered_addresses),
                          HostCache::Entry::SOURCE_UNKNOWN);
}

void HostResolverManager::CacheResult(HostCache* cache,
                                      const HostCache::Key& key,
                                      const HostCache::Entry& entry,
                                      base::TimeDelta ttl) {
  // Don't cache an error unless it has a positive TTL.
  if (cache && (entry.error() == OK || ttl > base::TimeDelta()))
    cache->Set(key, entry, tick_clock_->NowTicks(), ttl);
}

// Record time from Request creation until a valid DNS response.
void HostResolverManager::RecordTotalTime(bool speculative,
                                          bool from_cache,
                                          base::TimeDelta duration) const {
  if (!speculative) {
    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTime", duration);

    switch (mode_for_histogram_) {
      case MODE_FOR_HISTOGRAM_SYSTEM:
        UMA_HISTOGRAM_MEDIUM_TIMES("Net.DNS.TotalTimeTyped.System", duration);
        break;
      case MODE_FOR_HISTOGRAM_SYSTEM_SUPPORTS_DOH:
        UMA_HISTOGRAM_MEDIUM_TIMES("Net.DNS.TotalTimeTyped.SystemSupportsDoh",
                                   duration);
        break;
      case MODE_FOR_HISTOGRAM_SYSTEM_PRIVATE_DNS:
        UMA_HISTOGRAM_MEDIUM_TIMES("Net.DNS.TotalTimeTyped.SystemPrivate",
                                   duration);
        break;
      case MODE_FOR_HISTOGRAM_ASYNC_DNS:
        UMA_HISTOGRAM_MEDIUM_TIMES("Net.DNS.TotalTimeTyped.Async", duration);
        break;
      case MODE_FOR_HISTOGRAM_ASYNC_DNS_PRIVATE_SUPPORTS_DOH:
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Net.DNS.TotalTimeTyped.AsyncPrivateSupportsDoh", duration);
        break;
    }

    if (!from_cache)
      UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTimeNotCached", duration);
  }
}

std::unique_ptr<HostResolverManager::Job> HostResolverManager::RemoveJob(
    JobMap::iterator job_it) {
  DCHECK(job_it != jobs_.end());
  DCHECK(job_it->second);
  DCHECK_EQ(1u, jobs_.count(job_it->first));

  std::unique_ptr<Job> job;
  job_it->second.swap(job);
  jobs_.erase(job_it);
  job->OnRemovedFromJobMap();

  return job;
}

bool HostResolverManager::HasAvailableDohServer() {
  // TODO(crbug.com/878582): Once DoH probes are sent, update this such that a
  // DoH server is considered successful if it has a successful probe state.
  return dns_client_->GetConfig()->dns_over_https_servers.size() > 0;
}

DnsConfig::SecureDnsMode HostResolverManager::GetEffectiveSecureDnsMode(
    base::Optional<DnsConfig::SecureDnsMode> secure_dns_mode_override) {
  DnsConfig::SecureDnsMode secure_dns_mode = DnsConfig::SecureDnsMode::OFF;
  if (secure_dns_mode_override) {
    secure_dns_mode = secure_dns_mode_override.value();
  } else if (HaveDnsConfig()) {
    secure_dns_mode = dns_client_->GetConfig()->secure_dns_mode;
  }
  return secure_dns_mode;
}

void HostResolverManager::PushDnsTasks(
    bool allow_proc_fallback,
    DnsConfig::SecureDnsMode secure_dns_mode,
    ResolveHostParameters::CacheUsage cache_usage,
    std::deque<TaskType>* out_tasks) {
  bool allow_cache =
      cache_usage != ResolveHostParameters::CacheUsage::DISALLOWED;
  // Upgrade the insecure DnsTask depending on the secure dns mode.
  switch (secure_dns_mode) {
    case DnsConfig::SecureDnsMode::SECURE:
      DCHECK(dns_client_->GetConfig()->dns_over_https_servers.size() != 0);
      // Replace the insecure DnsTask with a secure cache lookup followed
      // by a secure DnsTask.
      if (allow_cache)
        out_tasks->push_back(TaskType::SECURE_CACHE_LOOKUP);
      out_tasks->push_back(TaskType::SECURE_DNS);
      break;
    case DnsConfig::SecureDnsMode::AUTOMATIC:
      // TODO(crbug.com/878582): For a DnsTask in AUTOMATIC mode, the async
      // resolver should only send insecure requests if it is enabled on this
      // platform.
      if (!HasAvailableDohServer()) {
        // Don't run a secure DnsTask if there are no available DoH servers.
        if (allow_cache)
          out_tasks->push_back(TaskType::CACHE_LOOKUP);
        out_tasks->push_back(TaskType::DNS);
      } else if (cache_usage == HostResolver::ResolveHostParameters::
                                    CacheUsage::STALE_ALLOWED) {
        // If stale results are allowed, the cache should be checked for both
        // secure and insecure results prior to running a secure DnsTask.
        out_tasks->push_back(TaskType::CACHE_LOOKUP);
        out_tasks->push_back(TaskType::SECURE_DNS);
        out_tasks->push_back(TaskType::DNS);
      } else {
        if (allow_cache)
          out_tasks->push_back(TaskType::SECURE_CACHE_LOOKUP);
        out_tasks->push_back(TaskType::SECURE_DNS);
        if (allow_cache)
          out_tasks->push_back(TaskType::INSECURE_CACHE_LOOKUP);
        out_tasks->push_back(TaskType::DNS);
      }
      break;
    case DnsConfig::SecureDnsMode::OFF:
      if (allow_cache)
        out_tasks->push_back(TaskType::CACHE_LOOKUP);
      out_tasks->push_back(TaskType::DNS);
      break;
    default:
      NOTREACHED();
      break;
  }

  // The system resolver can be used as a fallback if allowed by the request
  // parameters.
  if (allow_proc_fallback && allow_fallback_to_proctask_)
    out_tasks->push_back(TaskType::PROC);
}

void HostResolverManager::CreateTaskSequence(
    const std::string& hostname,
    DnsQueryType dns_query_type,
    HostResolverSource source,
    HostResolverFlags flags,
    base::Optional<DnsConfig::SecureDnsMode> secure_dns_mode_override,
    ResolveHostParameters::CacheUsage cache_usage,
    std::deque<TaskType>* out_tasks) {
  DCHECK(out_tasks->empty());
  DnsConfig::SecureDnsMode secure_dns_mode =
      GetEffectiveSecureDnsMode(secure_dns_mode_override);

  // A cache lookup should generally be performed first. For jobs involving a
  // DnsTask, this task will be removed before DnsTasks and other related tasks
  // are added to the sequence.
  if (cache_usage != ResolveHostParameters::CacheUsage::DISALLOWED)
    out_tasks->push_front(TaskType::CACHE_LOOKUP);

  // Determine what type of task a future Job should start.
  switch (source) {
    case HostResolverSource::ANY:
      // Force address queries with canonname to use ProcTask to counter poor
      // CNAME support in DnsTask. See https://crbug.com/872665
      //
      // Otherwise, default to DnsTask (with allowed fallback to ProcTask for
      // address queries). But if hostname appears to be an MDNS name (ends in
      // *.local), go with ProcTask for address queries and MdnsTask for non-
      // address queries.
      if ((flags & HOST_RESOLVER_CANONNAME) && IsAddressType(dns_query_type)) {
        out_tasks->push_back(TaskType::PROC);
      } else if (!ResemblesMulticastDNSName(hostname)) {
        bool allow_proc_fallback =
            IsAddressType(dns_query_type) &&
            secure_dns_mode != DnsConfig::SecureDnsMode::SECURE;
        // DnsClient or config is not available, but we're allowed to switch to
        // ProcTask instead.
        if ((!HaveDnsConfig() || use_proctask_by_default_) &&
            allow_proc_fallback) {
          out_tasks->push_back(TaskType::PROC);
        } else {
          // Remove the initial cache lookup task.
          if (!out_tasks->empty())
            out_tasks->pop_front();
          PushDnsTasks(allow_proc_fallback, secure_dns_mode, cache_usage,
                       out_tasks);
        }
      } else if (IsAddressType(dns_query_type)) {
        out_tasks->push_back(TaskType::PROC);
      } else {
        out_tasks->push_back(TaskType::MDNS);
      }
      break;
    case HostResolverSource::SYSTEM:
      out_tasks->push_back(TaskType::PROC);
      break;
    case HostResolverSource::DNS:
      // Remove the initial cache lookup task.
      if (!out_tasks->empty())
        out_tasks->pop_front();
      PushDnsTasks(false /* allow_proc_fallback */, secure_dns_mode,
                   cache_usage, out_tasks);
      break;
    case HostResolverSource::MULTICAST_DNS:
      out_tasks->push_back(TaskType::MDNS);
      break;
    case HostResolverSource::LOCAL_ONLY:
      // If no external source allowed, a job should not be created or started
      break;
  }
}

void HostResolverManager::GetEffectiveParametersForRequest(
    const std::string& hostname,
    DnsQueryType dns_query_type,
    HostResolverSource source,
    HostResolverFlags flags,
    base::Optional<DnsConfig::SecureDnsMode> secure_dns_mode_override,
    ResolveHostParameters::CacheUsage cache_usage,
    const IPAddress* ip_address,
    const NetLogWithSource& net_log,
    DnsQueryType* out_effective_type,
    HostResolverFlags* out_effective_flags,
    std::deque<TaskType>* out_tasks) {
  *out_effective_flags = flags | additional_resolver_flags_;
  *out_effective_type = dns_query_type;
  if (*out_effective_type == DnsQueryType::UNSPECIFIED &&
      // When resolving IPv4 literals, there's no need to probe for IPv6.
      // When resolving IPv6 literals, there's no benefit to artificially
      // limiting our resolution based on a probe.  Prior logic ensures
      // that this query is UNSPECIFIED (see effective_query_type check above)
      // so the code requesting the resolution should be amenable to receiving a
      // IPv6 resolution.
      !use_local_ipv6_ && ip_address == nullptr && !IsIPv6Reachable(net_log)) {
    *out_effective_type = DnsQueryType::A;
    *out_effective_flags |= HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6;
  }

  CreateTaskSequence(hostname, *out_effective_type, source,
                     *out_effective_flags, secure_dns_mode_override,
                     cache_usage, out_tasks);
}

bool HostResolverManager::IsIPv6Reachable(const NetLogWithSource& net_log) {
  // Don't bother checking if the device is on WiFi and IPv6 is assumed to not
  // work on WiFi.
  if (!check_ipv6_on_wifi_ && NetworkChangeNotifier::GetConnectionType() ==
                                  NetworkChangeNotifier::CONNECTION_WIFI) {
    return false;
  }

  // Cache the result for kIPv6ProbePeriodMs (measured from after
  // IsGloballyReachable() completes).
  bool cached = true;
  if ((tick_clock_->NowTicks() - last_ipv6_probe_time_).InMilliseconds() >
      kIPv6ProbePeriodMs) {
    last_ipv6_probe_result_ =
        IsGloballyReachable(IPAddress(kIPv6ProbeAddress), net_log);
    last_ipv6_probe_time_ = tick_clock_->NowTicks();
    cached = false;
  }
  net_log.AddEvent(
      NetLogEventType::HOST_RESOLVER_IMPL_IPV6_REACHABILITY_CHECK, [&] {
        return NetLogIPv6AvailableParams(last_ipv6_probe_result_, cached);
      });
  return last_ipv6_probe_result_;
}

bool HostResolverManager::IsGloballyReachable(const IPAddress& dest,
                                              const NetLogWithSource& net_log) {
  std::unique_ptr<DatagramClientSocket> socket(
      ClientSocketFactory::GetDefaultFactory()->CreateDatagramClientSocket(
          DatagramSocket::DEFAULT_BIND, net_log.net_log(), net_log.source()));
  int rv = socket->Connect(IPEndPoint(dest, 53));
  if (rv != OK)
    return false;
  IPEndPoint endpoint;
  rv = socket->GetLocalAddress(&endpoint);
  if (rv != OK)
    return false;
  DCHECK_EQ(ADDRESS_FAMILY_IPV6, endpoint.GetFamily());
  const IPAddress& address = endpoint.address();

  bool is_link_local =
      (address.bytes()[0] == 0xFE) && ((address.bytes()[1] & 0xC0) == 0x80);
  if (is_link_local)
    return false;

  const uint8_t kTeredoPrefix[] = {0x20, 0x01, 0, 0};
  if (IPAddressStartsWith(address, kTeredoPrefix))
    return false;

  return true;
}

void HostResolverManager::RunLoopbackProbeJob() {
  // Run this asynchronously as it can take 40-100ms and should not block
  // initialization.
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&HaveOnlyLoopbackAddresses),
      base::BindOnce(&HostResolverManager::SetHaveOnlyLoopbackAddresses,
                     weak_ptr_factory_.GetWeakPtr()));
}

void HostResolverManager::AbortAllJobs(bool in_progress_only) {
  // In Abort, a Request callback could spawn new Jobs with matching keys, so
  // first collect and remove all running jobs from |jobs_|.
  std::vector<std::unique_ptr<Job>> jobs_to_abort;
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    Job* job = it->second.get();
    if (!in_progress_only || job->is_running()) {
      jobs_to_abort.push_back(RemoveJob(it++));
    } else {
      ++it;
    }
  }

  // Pause the dispatcher so it won't start any new dispatcher jobs while
  // aborting the old ones.  This is needed so that it won't start the second
  // DnsTransaction for a job in |jobs_to_abort| if the DnsConfig just became
  // invalid.
  PrioritizedDispatcher::Limits limits = dispatcher_->GetLimits();
  dispatcher_->SetLimits(
      PrioritizedDispatcher::Limits(limits.reserved_slots.size(), 0));

  // Life check to bail once |this| is deleted.
  base::WeakPtr<HostResolverManager> self = weak_ptr_factory_.GetWeakPtr();

  // Then Abort them.
  for (size_t i = 0; self.get() && i < jobs_to_abort.size(); ++i) {
    jobs_to_abort[i]->Abort();
  }

  if (self)
    dispatcher_->SetLimits(limits);
}

void HostResolverManager::SetDnsClient(std::unique_ptr<DnsClient> dns_client) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // DnsClient and config must be updated before aborting DnsTasks, since doing
  // so may start new jobs.
  dns_client_ = std::move(dns_client);
  if (dns_client_ && !dns_client_->GetConfig() &&
      num_dns_failures_ < kMaximumDnsFailures) {
    dns_client_->SetConfig(GetBaseDnsConfig(false));
    num_dns_failures_ = 0;
  }

  AbortDnsTasks(ERR_NETWORK_CHANGED, false /* fallback_only */);
  DnsConfig dns_config;
  if (!HaveDnsConfig()) {
    // UpdateModeForHistogram() needs to know the DnsConfig when
    // !HaveDnsConfig()
    dns_config = GetBaseDnsConfig(false);
  }
  UpdateModeForHistogram(dns_config);
}

void HostResolverManager::AbortDnsTasks(int error, bool fallback_only) {
  // Aborting jobs potentially modifies |jobs_| and may even delete some jobs.
  // Create safe closures of all current jobs.
  std::vector<base::OnceClosure> job_abort_closures;
  for (auto& job : jobs_) {
    job_abort_closures.push_back(
        job.second->GetAbortDnsTaskClosure(error, fallback_only));
  }

  // Pause the dispatcher so it won't start any new dispatcher jobs while
  // aborting the old ones.  This is needed so that it won't start the second
  // DnsTransaction for a job if the DnsConfig just changed.
  PrioritizedDispatcher::Limits limits = dispatcher_->GetLimits();
  dispatcher_->SetLimits(
      PrioritizedDispatcher::Limits(limits.reserved_slots.size(), 0));

  for (base::OnceClosure& closure : job_abort_closures)
    std::move(closure).Run();

  dispatcher_->SetLimits(limits);
}

void HostResolverManager::TryServingAllJobsFromHosts() {
  if (!HaveDnsConfig())
    return;

  // TODO(szym): Do not do this if nsswitch.conf instructs not to.
  // http://crbug.com/117655

  // Life check to bail once |this| is deleted.
  base::WeakPtr<HostResolverManager> self = weak_ptr_factory_.GetWeakPtr();

  for (auto it = jobs_.begin(); self.get() && it != jobs_.end();) {
    Job* job = it->second.get();
    ++it;
    // This could remove |job| from |jobs_|, but iterator will remain valid.
    job->ServeFromHosts();
  }
}

void HostResolverManager::OnIPAddressChanged() {
  last_ipv6_probe_time_ = base::TimeTicks();
  // Abandon all ProbeJobs.
  probe_weak_ptr_factory_.InvalidateWeakPtrs();
  InvalidateCaches();
#if (defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)) || \
    defined(OS_FUCHSIA)
  RunLoopbackProbeJob();
#endif
  AbortAllJobs(true /* in_progress_only */);
  // |this| may be deleted inside AbortAllJobs().
}

void HostResolverManager::OnConnectionTypeChanged(
    NetworkChangeNotifier::ConnectionType type) {
  proc_params_.unresponsive_delay =
      GetTimeDeltaForConnectionTypeFromFieldTrialOrDefault(
          "DnsUnresponsiveDelayMsByConnectionType",
          ProcTaskParams::kDnsDefaultUnresponsiveDelay, type);
}

void HostResolverManager::OnInitialDNSConfigRead() {
  UpdateDNSConfig(false);
}

void HostResolverManager::OnDNSChanged() {
  // Ignore changes if we're using a test config or if we have overriding
  // configuration that overrides everything from the base config.
  if (test_base_config_ || dns_config_overrides_.OverridesEverything())
    return;

  UpdateDNSConfig(true);
}

DnsConfig HostResolverManager::GetBaseDnsConfig(bool log_to_net_log) {
  DnsConfig dns_config;

  // Skip retrieving the base config if all values will be overridden.
  if (!dns_config_overrides_.OverridesEverything()) {
    if (test_base_config_) {
      dns_config = test_base_config_.value();
    } else {
      NetworkChangeNotifier::GetDnsConfig(&dns_config);
    }

    if (log_to_net_log && net_log_) {
      net_log_->AddGlobalEntry(NetLogEventType::DNS_CONFIG_CHANGED, [&] {
        return NetLogDnsConfigParams(&dns_config);
      });
    }

    // TODO(szym): Remove once http://crbug.com/137914 is resolved.
    received_dns_config_ = dns_config.IsValid();
  }

  return dns_config_overrides_.ApplyOverrides(dns_config);
}

void HostResolverManager::UpdateDNSConfig(bool config_changed) {
  DnsConfig dns_config = GetBaseDnsConfig(true);

  // Conservatively assume local IPv6 is needed when DnsConfig is not valid.
  use_local_ipv6_ = !dns_config.IsValid() || dns_config.use_local_ipv6;

  num_dns_failures_ = 0;

  // We want a new DnsSession in place, before we Abort running Jobs, so that
  // the newly started jobs use the new config.
  if (dns_client_.get()) {
    // Make sure that if the update is an initial read, not a change, there
    // wasn't already a DnsConfig or it's the same one.
    DCHECK(config_changed || !dns_client_->GetConfig() ||
           dns_client_->GetConfig()->Equals(dns_config));
    dns_client_->SetConfig(dns_config);
  }
  use_proctask_by_default_ = false;

  if (config_changed) {
    InvalidateCaches();

    // Life check to bail once |this| is deleted.
    base::WeakPtr<HostResolverManager> self = weak_ptr_factory_.GetWeakPtr();

    // Existing jobs that were set up using the nameservers and secure dns mode
    // from the original config need to be aborted.
    AbortAllJobs(false /* in_progress_only */);

    // |this| may be deleted inside AbortAllJobs().
    if (self.get())
      TryServingAllJobsFromHosts();
  }

  UpdateModeForHistogram(dns_config);
}

bool HostResolverManager::HaveDnsConfig() const {
  // Use DnsClient only if it's fully configured and there is no override by
  // ScopedDefaultHostResolverProc.
  // The alternative is to use NetworkChangeNotifier to override DnsConfig,
  // but that would introduce construction order requirements for NCN and SDHRP.
  return dns_client_ && dns_client_->GetConfig() &&
         (proc_params_.resolver_proc || !HostResolverProc::GetDefault());
}

void HostResolverManager::OnDnsTaskResolve() {
  DCHECK(dns_client_);
  num_dns_failures_ = 0;
}

void HostResolverManager::OnFallbackResolve(int dns_task_error) {
  DCHECK(dns_client_);
  DCHECK_NE(OK, dns_task_error);

  ++num_dns_failures_;
  if (num_dns_failures_ < kMaximumDnsFailures)
    return;

  // Force fallback until the next DNS change.  Must be done before aborting
  // DnsTasks, since doing so may start new jobs.  Do not fully clear out or
  // disable the DnsClient as some requests (e.g. those specifying DNS source)
  // are not allowed to fallback and will continue using DnsTask.
  use_proctask_by_default_ = true;

  // Fallback all fallback-allowed DnsTasks to ProcTasks.
  AbortDnsTasks(ERR_FAILED, true /* fallback_only */);
}

int HostResolverManager::GetOrCreateMdnsClient(MDnsClient** out_client) {
#if BUILDFLAG(ENABLE_MDNS)
  if (!mdns_client_) {
    if (!mdns_socket_factory_)
      mdns_socket_factory_ = std::make_unique<MDnsSocketFactoryImpl>(net_log_);
    mdns_client_ = MDnsClient::CreateDefault();
  }

  int rv = OK;
  if (!mdns_client_->IsListening())
    rv = mdns_client_->StartListening(mdns_socket_factory_.get());

  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(rv != OK || mdns_client_->IsListening());
  if (rv == OK)
    *out_client = mdns_client_.get();
  return rv;
#else
  // Should not request MDNS resoltuion unless MDNS is enabled.
  NOTREACHED();
  return ERR_UNEXPECTED;
#endif
}

void HostResolverManager::UpdateModeForHistogram(const DnsConfig& dns_config) {
  // Resolving with Async DNS resolver?
  if (HaveDnsConfig()) {
    mode_for_histogram_ = MODE_FOR_HISTOGRAM_ASYNC_DNS;
    for (const auto& dns_server : dns_client_->GetConfig()->nameservers) {
      if (DnsServerSupportsDoh(dns_server.address())) {
        mode_for_histogram_ = MODE_FOR_HISTOGRAM_ASYNC_DNS_PRIVATE_SUPPORTS_DOH;
        break;
      }
    }
  } else {
    mode_for_histogram_ = MODE_FOR_HISTOGRAM_SYSTEM;
    for (const auto& dns_server : dns_config.nameservers) {
      if (DnsServerSupportsDoh(dns_server.address())) {
        mode_for_histogram_ = MODE_FOR_HISTOGRAM_SYSTEM_SUPPORTS_DOH;
        break;
      }
    }
#if defined(OS_ANDROID)
    if (base::android::BuildInfo::GetInstance()->sdk_int() >=
        base::android::SDK_VERSION_P) {
      std::vector<IPEndPoint> dns_servers;
      if (net::android::GetDnsServers(&dns_servers) ==
          internal::CONFIG_PARSE_POSIX_PRIVATE_DNS_ACTIVE) {
        mode_for_histogram_ = MODE_FOR_HISTOGRAM_SYSTEM_PRIVATE_DNS;
      }
    }
#endif  // defined(OS_ANDROID)
  }
}

void HostResolverManager::InvalidateCaches() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!invalidation_in_progress_);

#if DCHECK_IS_ON()
  base::WeakPtr<HostResolverManager> self_ptr = weak_ptr_factory_.GetWeakPtr();
  size_t num_jobs = jobs_.size();
#endif

  invalidation_in_progress_ = true;
  for (auto& invalidator : host_cache_invalidators_)
    invalidator.Invalidate();
  invalidation_in_progress_ = false;

#if DCHECK_IS_ON()
  // Sanity checks that invalidation does not have reentrancy issues.
  DCHECK(self_ptr);
  DCHECK_EQ(num_jobs, jobs_.size());
#endif
}

void HostResolverManager::RequestImpl::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!job_)
    return;

  job_->CancelRequest(this);
  job_ = nullptr;
  callback_.Reset();
}

void HostResolverManager::RequestImpl::ChangeRequestPriority(
    RequestPriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(job_);
  job_->ChangeRequestPriority(this, priority);
}

}  // namespace net
