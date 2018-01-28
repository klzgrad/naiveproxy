// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_impl.h"

#if defined(OS_WIN)
#include <Winsock2.h>
#elif defined(OS_POSIX)
#include <netdb.h>
#endif

#if defined(OS_POSIX)
#include <netinet/in.h>
#if !defined(OS_NACL)
#include <net/if.h>
#if !defined(OS_ANDROID)
#include <ifaddrs.h>
#endif  // !defined(OS_ANDROID)
#endif  // !defined(OS_NACL)
#endif  // defined(OS_POSIX)

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/containers/circular_deque.h"
#include "base/debug/debugger.h"
#include "base/debug/stack_trace.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "net/base/address_family.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/base/url_util.h"
#include "net/dns/address_sorter.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_config_service.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/dns_reloader.h"
#include "net/dns/dns_response.h"
#include "net/dns/dns_transaction.h"
#include "net/dns/dns_util.h"
#include "net/dns/host_resolver_proc.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_parameters_callback.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/datagram_client_socket.h"
#include "url/url_canon_ip.h"

#if defined(OS_WIN)
#include "net/base/winsock_init.h"
#endif

#if defined(OS_ANDROID)
#include "net/android/network_library.h"
#endif

namespace net {

namespace {

// Default delay between calls to the system resolver for the same hostname.
// (Can be overridden by field trial.)
const int64_t kDnsDefaultUnresponsiveDelayMs = 6000;

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
const uint8_t kIPv6ProbeAddress[] =
    { 0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88 };

// We use a separate histogram name for each platform to facilitate the
// display of error codes by their symbolic name (since each platform has
// different mappings).
const char kOSErrorsForGetAddrinfoHistogramName[] =
#if defined(OS_WIN)
    "Net.OSErrorsForGetAddrinfo_Win";
#elif defined(OS_MACOSX)
    "Net.OSErrorsForGetAddrinfo_Mac";
#elif defined(OS_LINUX)
    "Net.OSErrorsForGetAddrinfo_Linux";
#else
    "Net.OSErrorsForGetAddrinfo";
#endif

// Gets a list of the likely error codes that getaddrinfo() can return
// (non-exhaustive). These are the error codes that we will track via
// a histogram.
std::vector<int> GetAllGetAddrinfoOSErrors() {
  int os_errors[] = {
#if defined(OS_POSIX)
#if !defined(OS_FREEBSD)
#if !defined(OS_ANDROID)
    // EAI_ADDRFAMILY has been declared obsolete in Android's and
    // FreeBSD's netdb.h.
    EAI_ADDRFAMILY,
#endif
    // EAI_NODATA has been declared obsolete in FreeBSD's netdb.h.
    EAI_NODATA,
#endif
    EAI_AGAIN,
    EAI_BADFLAGS,
    EAI_FAIL,
    EAI_FAMILY,
    EAI_MEMORY,
    EAI_NONAME,
    EAI_SERVICE,
    EAI_SOCKTYPE,
    EAI_SYSTEM,
#elif defined(OS_WIN)
    // See: http://msdn.microsoft.com/en-us/library/ms738520(VS.85).aspx
    WSA_NOT_ENOUGH_MEMORY,
    WSAEAFNOSUPPORT,
    WSAEINVAL,
    WSAESOCKTNOSUPPORT,
    WSAHOST_NOT_FOUND,
    WSANO_DATA,
    WSANO_RECOVERY,
    WSANOTINITIALISED,
    WSATRY_AGAIN,
    WSATYPE_NOT_FOUND,
    // The following are not in doc, but might be to appearing in results :-(.
    WSA_INVALID_HANDLE,
#endif
  };

  // Ensure all errors are positive, as histogram only tracks positive values.
  for (size_t i = 0; i < arraysize(os_errors); ++i) {
    os_errors[i] = std::abs(os_errors[i]);
  }

  return base::CustomHistogram::ArrayToCustomRanges(os_errors,
                                                    arraysize(os_errors));
}

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

void UmaAsyncDnsResolveStatus(DnsResolveStatus result) {
  UMA_HISTOGRAM_ENUMERATION("AsyncDNS.ResolveStatus",
                            result,
                            RESOLVE_STATUS_MAX);
}

bool ResemblesNetBIOSName(const std::string& hostname) {
  return (hostname.size() < 16) && (hostname.find('.') == std::string::npos);
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
      !hostname.compare(hostname.size() - kSuffixLenTrimmed, kSuffixLenTrimmed,
                        kSuffix, kSuffixLenTrimmed);
}

// A macro to simplify code and readability.
#define DNS_HISTOGRAM_BY_PRIORITY(basename, priority, time)        \
  do {                                                             \
    switch (priority) {                                            \
      case HIGHEST:                                                \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".HIGHEST", time);   \
        break;                                                     \
      case MEDIUM:                                                 \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".MEDIUM", time);    \
        break;                                                     \
      case LOW:                                                    \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".LOW", time);       \
        break;                                                     \
      case LOWEST:                                                 \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".LOWEST", time);    \
        break;                                                     \
      case IDLE:                                                   \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".IDLE", time);      \
        break;                                                     \
      case THROTTLED:                                              \
        UMA_HISTOGRAM_LONG_TIMES_100(basename ".THROTTLED", time); \
        break;                                                     \
    }                                                              \
    UMA_HISTOGRAM_LONG_TIMES_100(basename, time);                  \
  } while (0)

// Record time from Request creation until a valid DNS response.
void RecordTotalTime(bool speculative,
                     bool from_cache,
                     base::TimeDelta duration) {
  if (speculative) {
    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTime.Speculative", duration);
  } else {
    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTime", duration);
  }

  if (!from_cache) {
    if (speculative) {
      UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTimeNotCached.Speculative",
                                   duration);
    } else {
      UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.TotalTimeNotCached", duration);
    }
  }
}

void RecordTTL(base::TimeDelta ttl) {
  UMA_HISTOGRAM_CUSTOM_TIMES("AsyncDNS.TTL", ttl,
                             base::TimeDelta::FromSeconds(1),
                             base::TimeDelta::FromDays(1), 100);
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

//-----------------------------------------------------------------------------

AddressList EnsurePortOnAddressList(const AddressList& list, uint16_t port) {
  if (list.empty() || list.front().port() == port)
    return list;
  return AddressList::CopyWithPort(list, port);
}

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
  base::ScopedBlockingCall scoped_blocking_call(base::BlockingType::WILL_BLOCK);
#if defined(OS_ANDROID)
  return android::HaveOnlyLoopbackAddresses();
#elif defined(OS_NACL)
  NOTIMPLEMENTED();
  return false;
#elif defined(OS_POSIX)
  struct ifaddrs* interface_addr = NULL;
  int rv = getifaddrs(&interface_addr);
  if (rv != 0) {
    DVLOG(1) << "getifaddrs() failed with errno = " << errno;
    return false;
  }

  bool result = true;
  for (struct ifaddrs* interface = interface_addr;
       interface != NULL;
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
#elif defined(OS_WIN)
  // TODO(wtc): implement with the GetAdaptersAddresses function.
  NOTIMPLEMENTED();
  return false;
#else
  NOTIMPLEMENTED();
  return false;
#endif  // defined(various platforms)
}

// Creates NetLog parameters when the resolve failed.
std::unique_ptr<base::Value> NetLogProcTaskFailedCallback(
    uint32_t attempt_number,
    int net_error,
    int os_error,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  if (attempt_number)
    dict->SetInteger("attempt_number", attempt_number);

  dict->SetInteger("net_error", net_error);

  if (os_error) {
    dict->SetInteger("os_error", os_error);
#if defined(OS_POSIX)
    dict->SetString("os_error_string", gai_strerror(os_error));
#elif defined(OS_WIN)
    // Map the error code to a human-readable string.
    LPWSTR error_string = nullptr;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  0,  // Use the internal message table.
                  os_error,
                  0,  // Use default language.
                  (LPWSTR)&error_string,
                  0,  // Buffer size.
                  0);  // Arguments (unused).
    dict->SetString("os_error_string", base::WideToUTF8(error_string));
    LocalFree(error_string);
#endif
  }

  return std::move(dict);
}

// Creates NetLog parameters when the DnsTask failed.
std::unique_ptr<base::Value> NetLogDnsTaskFailedCallback(
    int net_error,
    int dns_error,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("net_error", net_error);
  if (dns_error)
    dict->SetInteger("dns_error", dns_error);
  return std::move(dict);
}

// Creates NetLog parameters containing the information in a RequestInfo object,
// along with the associated NetLogSource.
std::unique_ptr<base::Value> NetLogRequestInfoCallback(
    const HostResolver::RequestInfo* info,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());

  dict->SetString("host", info->host_port_pair().ToString());
  dict->SetInteger("address_family",
                   static_cast<int>(info->address_family()));
  dict->SetBoolean("allow_cached_response", info->allow_cached_response());
  dict->SetBoolean("is_speculative", info->is_speculative());
  return std::move(dict);
}

// Creates NetLog parameters for the creation of a HostResolverImpl::Job.
std::unique_ptr<base::Value> NetLogJobCreationCallback(
    const NetLogSource& source,
    const std::string* host,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  source.AddToEventParameters(dict.get());
  dict->SetString("host", *host);
  return std::move(dict);
}

// Creates NetLog parameters for HOST_RESOLVER_IMPL_JOB_ATTACH/DETACH events.
std::unique_ptr<base::Value> NetLogJobAttachCallback(
    const NetLogSource& source,
    RequestPriority priority,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  source.AddToEventParameters(dict.get());
  dict->SetString("priority", RequestPriorityToString(priority));
  return std::move(dict);
}

// Creates NetLog parameters for the DNS_CONFIG_CHANGED event.
std::unique_ptr<base::Value> NetLogDnsConfigCallback(
    const DnsConfig* config,
    NetLogCaptureMode /* capture_mode */) {
  return config->ToValue();
}

std::unique_ptr<base::Value> NetLogIPv6AvailableCallback(
    bool ipv6_available,
    bool cached,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetBoolean("ipv6_available", ipv6_available);
  dict->SetBoolean("cached", cached);
  return std::move(dict);
}

// The logging routines are defined here because some requests are resolved
// without a Request object.

// Logs when a request has just been started.
void LogStartRequest(const NetLogWithSource& source_net_log,
                     const HostResolver::RequestInfo& info) {
  source_net_log.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_REQUEST,
                            base::Bind(&NetLogRequestInfoCallback, &info));
}

// Logs when a request has just completed (before its callback is run).
void LogFinishRequest(const NetLogWithSource& source_net_log,
                      const HostResolver::RequestInfo& info,
                      int net_error) {
  source_net_log.EndEventWithNetErrorCode(
      NetLogEventType::HOST_RESOLVER_IMPL_REQUEST, net_error);
}

// Logs when a request has been cancelled.
void LogCancelRequest(const NetLogWithSource& source_net_log,
                      const HostResolverImpl::RequestInfo& info) {
  source_net_log.AddEvent(NetLogEventType::CANCELLED);
  source_net_log.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_REQUEST);
}

//-----------------------------------------------------------------------------

// Keeps track of the highest priority.
class PriorityTracker {
 public:
  explicit PriorityTracker(RequestPriority initial_priority)
      : highest_priority_(initial_priority), total_count_(0) {
    memset(counts_, 0, sizeof(counts_));
  }

  RequestPriority highest_priority() const {
    return highest_priority_;
  }

  size_t total_count() const {
    return total_count_;
  }

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

void MakeNotStale(HostCache::EntryStaleness* stale_info) {
  if (!stale_info)
    return;
  stale_info->expired_by = base::TimeDelta::FromSeconds(-1);
  stale_info->network_changes = 0;
  stale_info->stale_hits = 0;
}

// Persist data every five minutes (potentially, cache and learned RTT).
const int64_t kPersistDelaySec = 300;

}  // namespace

//-----------------------------------------------------------------------------

bool ResolveLocalHostname(base::StringPiece host,
                          uint16_t port,
                          AddressList* address_list) {
  address_list->clear();

  bool is_local6;
  if (!IsLocalHostname(host, &is_local6))
    return false;

  address_list->push_back(IPEndPoint(IPAddress::IPv6Localhost(), port));
  if (!is_local6) {
    address_list->push_back(IPEndPoint(IPAddress::IPv4Localhost(), port));
  }

  return true;
}

const unsigned HostResolverImpl::kMaximumDnsFailures = 16;

// Holds the data for a request that could not be completed synchronously.
// It is owned by a Job. Canceled Requests are only marked as canceled rather
// than removed from the Job's |requests_| list.
class HostResolverImpl::RequestImpl : public HostResolver::Request {
 public:
  RequestImpl(const NetLogWithSource& source_net_log,
              const RequestInfo& info,
              RequestPriority priority,
              const CompletionCallback& callback,
              AddressList* addresses,
              Job* job)
      : source_net_log_(source_net_log),
        info_(info),
        priority_(priority),
        job_(job),
        callback_(callback),
        addresses_(addresses),
        request_time_(base::TimeTicks::Now()) {}

  ~RequestImpl() override;

  void ChangeRequestPriority(RequestPriority priority) override;

  void OnJobCancelled(Job* job) {
    DCHECK_EQ(job_, job);
    job_ = nullptr;
    addresses_ = nullptr;
    callback_.Reset();
  }

  // Prepare final AddressList and call completion callback.
  void OnJobCompleted(Job* job, int error, const AddressList& addr_list) {
    DCHECK_EQ(job_, job);
    if (error == OK)
      *addresses_ = EnsurePortOnAddressList(addr_list, info_.port());
    job_ = nullptr;
    addresses_ = nullptr;
    base::ResetAndReturn(&callback_).Run(error);
  }

  Job* job() const {
    return job_;
  }

  // NetLog for the source, passed in HostResolver::Resolve.
  const NetLogWithSource& source_net_log() { return source_net_log_; }

  const RequestInfo& info() const {
    return info_;
  }

  RequestPriority priority() const { return priority_; }
  void set_priority(RequestPriority priority) { priority_ = priority; }

  base::TimeTicks request_time() const { return request_time_; }

 private:
  const NetLogWithSource source_net_log_;

  // The request info that started the request.
  const RequestInfo info_;

  RequestPriority priority_;

  // The resolve job that this request is dependent on.
  Job* job_;

  // The user's callback to invoke when the request completes.
  CompletionCallback callback_;

  // The address list to save result into.
  AddressList* addresses_;

  const base::TimeTicks request_time_;

  DISALLOW_COPY_AND_ASSIGN(RequestImpl);
};

//------------------------------------------------------------------------------

// Calls HostResolverProc in TaskScheduler. Performs retries if necessary.
//
// Whenever we try to resolve the host, we post a delayed task to check if host
// resolution (OnLookupComplete) is completed or not. If the original attempt
// hasn't completed, then we start another attempt for host resolution. We take
// the results from the first attempt that finishes and ignore the results from
// all other attempts.
//
// TODO(szym): Move to separate source file for testing and mocking.
//
class HostResolverImpl::ProcTask
    : public base::RefCountedThreadSafe<HostResolverImpl::ProcTask> {
 public:
  typedef base::Callback<void(int net_error,
                              const AddressList& addr_list)> Callback;

  ProcTask(const Key& key,
           const ProcTaskParams& params,
           const Callback& callback,
           const NetLogWithSource& job_net_log)
      : key_(key),
        params_(params),
        callback_(callback),
        network_task_runner_(base::ThreadTaskRunnerHandle::Get()),
        attempt_number_(0),
        completed_attempt_number_(0),
        completed_attempt_error_(ERR_UNEXPECTED),
        net_log_(job_net_log) {
    if (!params_.resolver_proc.get())
      params_.resolver_proc = HostResolverProc::GetDefault();
    // If default is unset, use the system proc.
    if (!params_.resolver_proc.get())
      params_.resolver_proc = new SystemHostResolverProc();
  }

  void Start() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK);
    StartLookupAttempt();
  }

  // Cancels this ProcTask. It will be orphaned. Any outstanding resolve
  // attempts running on worker thread will continue running. Only once all the
  // attempts complete will the final reference to this ProcTask be released.
  void Cancel() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());

    if (was_canceled() || was_completed())
      return;

    callback_.Reset();
    net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK);
  }

  bool was_canceled() const {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    return callback_.is_null();
  }

  bool was_completed() const {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    return completed_attempt_number_ > 0;
  }

 private:
  friend class base::RefCountedThreadSafe<ProcTask>;
  ~ProcTask() {}

  void StartLookupAttempt() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    base::TimeTicks start_time = base::TimeTicks::Now();
    ++attempt_number_;
    // Dispatch the lookup attempt to a worker thread.
    base::PostTaskWithTraits(
        FROM_HERE,
        {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::Bind(&ProcTask::DoLookup, this, start_time, attempt_number_));

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_ATTEMPT_STARTED,
                      NetLog::IntCallback("attempt_number", attempt_number_));

    // If the results aren't received within a given time, RetryIfNotComplete
    // will start a new attempt if none of the outstanding attempts have
    // completed yet.
    if (attempt_number_ <= params_.max_retry_attempts) {
      network_task_runner_->PostDelayedTask(
          FROM_HERE, base::Bind(&ProcTask::RetryIfNotComplete, this),
          params_.unresponsive_delay);
    }
  }

  // WARNING: This code runs in TaskScheduler with CONTINUE_ON_SHUTDOWN. The
  // shutdown code cannot wait for it to finish, so this code must be very
  // careful about using other objects (like MessageLoops, Singletons, etc).
  // During shutdown these objects may no longer exist. Multiple DoLookups()
  // could be running in parallel, so any state inside of |this| must not
  // mutate.
  void DoLookup(const base::TimeTicks& start_time,
                const uint32_t attempt_number) {
    TRACE_HEAP_PROFILER_API_SCOPED_TASK_EXECUTION scoped_heap_context(
        "net/dns/proctask");
    AddressList results;
    int os_error = 0;
    int error = params_.resolver_proc->Resolve(key_.hostname,
                                               key_.address_family,
                                               key_.host_resolver_flags,
                                               &results,
                                               &os_error);

    network_task_runner_->PostTask(
        FROM_HERE, base::Bind(&ProcTask::OnLookupComplete, this, results,
                              start_time, attempt_number, error, os_error));
  }

  // Makes next attempt if DoLookup() has not finished.
  void RetryIfNotComplete() {
    DCHECK(network_task_runner_->BelongsToCurrentThread());

    if (was_completed() || was_canceled())
      return;

    params_.unresponsive_delay *= params_.retry_factor;
    StartLookupAttempt();
  }

  // Callback for when DoLookup() completes (runs on task runner thread).
  void OnLookupComplete(const AddressList& results,
                        const base::TimeTicks& start_time,
                        const uint32_t attempt_number,
                        int error,
                        const int os_error) {
    TRACE_EVENT0(kNetTracingCategory, "ProcTask::OnLookupComplete");
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    // If results are empty, we should return an error.
    bool empty_list_on_ok = (error == OK && results.empty());
    if (empty_list_on_ok)
      error = ERR_NAME_NOT_RESOLVED;

    bool was_retry_attempt = attempt_number > 1;

    // Ideally the following code would be part of host_resolver_proc.cc,
    // however it isn't safe to call NetworkChangeNotifier from worker threads.
    // So do it here on the IO thread instead.
    if (error != OK && NetworkChangeNotifier::IsOffline())
      error = ERR_INTERNET_DISCONNECTED;

    RecordAttemptHistograms(start_time, attempt_number, error, os_error);

    if (was_canceled())
      return;

    NetLogParametersCallback net_log_callback;
    if (error != OK) {
      net_log_callback = base::Bind(&NetLogProcTaskFailedCallback,
                                    attempt_number,
                                    error,
                                    os_error);
    } else {
      net_log_callback = NetLog::IntCallback("attempt_number", attempt_number);
    }
    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_ATTEMPT_FINISHED,
                      net_log_callback);

    if (was_completed())
      return;

    RecordTaskHistograms(start_time, error, os_error);

    // Copy the results from the first worker thread that resolves the host.
    results_ = results;
    completed_attempt_number_ = attempt_number;
    completed_attempt_error_ = error;

    if (was_retry_attempt) {
      // If retry attempt finishes before 1st attempt, then get stats on how
      // much time is saved by having spawned an extra attempt.
      retry_attempt_finished_time_ = base::TimeTicks::Now();
    }

    if (error != OK) {
      net_log_callback = base::Bind(&NetLogProcTaskFailedCallback,
                                    0, error, os_error);
    } else {
      net_log_callback = results_.CreateNetLogCallback();
    }
    net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_PROC_TASK,
                      net_log_callback);

    callback_.Run(error, results_);
  }

  void RecordTaskHistograms(const base::TimeTicks& start_time,
                            const int error,
                            const int os_error) const {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    base::TimeDelta duration = base::TimeTicks::Now() - start_time;
    if (error == OK)
      UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ProcTask.SuccessTime", duration);
    else
      UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ProcTask.FailureTime", duration);

    UMA_HISTOGRAM_CUSTOM_ENUMERATION(kOSErrorsForGetAddrinfoHistogramName,
                                     std::abs(os_error),
                                     GetAllGetAddrinfoOSErrors());
  }

  void RecordAttemptHistograms(const base::TimeTicks& start_time,
                               const uint32_t attempt_number,
                               const int error,
                               const int os_error) const {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    bool first_attempt_to_complete =
        completed_attempt_number_ == attempt_number;
    bool is_first_attempt = (attempt_number == 1);

    if (first_attempt_to_complete) {
      // If this was first attempt to complete, then record the resolution
      // status of the attempt.
      if (completed_attempt_error_ == OK) {
        UMA_HISTOGRAM_ENUMERATION(
            "DNS.AttemptFirstSuccess", attempt_number, 100);
      } else {
        UMA_HISTOGRAM_ENUMERATION(
            "DNS.AttemptFirstFailure", attempt_number, 100);
      }
    }

    if (error == OK)
      UMA_HISTOGRAM_ENUMERATION("DNS.AttemptSuccess", attempt_number, 100);
    else
      UMA_HISTOGRAM_ENUMERATION("DNS.AttemptFailure", attempt_number, 100);

    // If first attempt didn't finish before retry attempt, then calculate stats
    // on how much time is saved by having spawned an extra attempt.
    if (!first_attempt_to_complete && is_first_attempt && !was_canceled()) {
      UMA_HISTOGRAM_LONG_TIMES_100(
          "DNS.AttemptTimeSavedByRetry",
          base::TimeTicks::Now() - retry_attempt_finished_time_);
    }

    if (was_canceled() || !first_attempt_to_complete) {
      // Count those attempts which completed after the job was already canceled
      // OR after the job was already completed by an earlier attempt (so in
      // effect).
      UMA_HISTOGRAM_ENUMERATION("DNS.AttemptDiscarded", attempt_number, 100);

      // Record if job is canceled.
      if (was_canceled())
        UMA_HISTOGRAM_ENUMERATION("DNS.AttemptCancelled", attempt_number, 100);
    }

    base::TimeDelta duration = base::TimeTicks::Now() - start_time;
    if (error == OK)
      UMA_HISTOGRAM_LONG_TIMES_100("DNS.AttemptSuccessDuration", duration);
    else
      UMA_HISTOGRAM_LONG_TIMES_100("DNS.AttemptFailDuration", duration);
  }

  // Set on the task runner thread, read on the worker thread.
  Key key_;

  // Holds an owning reference to the HostResolverProc that we are going to use.
  // This may not be the current resolver procedure by the time we call
  // ResolveAddrInfo, but that's OK... we'll use it anyways, and the owning
  // reference ensures that it remains valid until we are done.
  ProcTaskParams params_;

  // The listener to the results of this ProcTask.
  Callback callback_;

  // Used to post events onto the network thread.
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  // Keeps track of the number of attempts we have made so far to resolve the
  // host. Whenever we start an attempt to resolve the host, we increase this
  // number.
  uint32_t attempt_number_;

  // The index of the attempt which finished first (or 0 if the job is still in
  // progress).
  uint32_t completed_attempt_number_;

  // The result (a net error code) from the first attempt to complete.
  int completed_attempt_error_;

  // The time when retry attempt was finished.
  base::TimeTicks retry_attempt_finished_time_;

  AddressList results_;

  NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(ProcTask);
};

//-----------------------------------------------------------------------------

// Resolves the hostname using DnsTransaction.
// TODO(szym): This could be moved to separate source file as well.
class HostResolverImpl::DnsTask : public base::SupportsWeakPtr<DnsTask> {
 public:
  class Delegate {
   public:
    virtual void OnDnsTaskComplete(base::TimeTicks start_time,
                                   int net_error,
                                   const AddressList& addr_list,
                                   base::TimeDelta ttl) = 0;

    // Called when the first of two jobs succeeds.  If the first completed
    // transaction fails, this is not called.  Also not called when the DnsTask
    // only needs to run one transaction.
    virtual void OnFirstDnsTransactionComplete() = 0;

   protected:
    Delegate() {}
    virtual ~Delegate() {}
  };

  DnsTask(DnsClient* client,
          const Key& key,
          Delegate* delegate,
          const NetLogWithSource& job_net_log)
      : client_(client),
        key_(key),
        delegate_(delegate),
        net_log_(job_net_log),
        num_completed_transactions_(0),
        task_start_time_(base::TimeTicks::Now()) {
    DCHECK(client);
    DCHECK(delegate_);
  }

  bool needs_two_transactions() const {
    return key_.address_family == ADDRESS_FAMILY_UNSPECIFIED;
  }

  bool needs_another_transaction() const {
    return needs_two_transactions() && !transaction_aaaa_;
  }

  void StartFirstTransaction() {
    DCHECK_EQ(0u, num_completed_transactions_);
    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK);
    if (key_.address_family == ADDRESS_FAMILY_IPV6) {
      StartAAAA();
    } else {
      StartA();
    }
  }

  void StartSecondTransaction() {
    DCHECK(needs_two_transactions());
    StartAAAA();
  }

 private:
  void StartA() {
    DCHECK(!transaction_a_);
    DCHECK_NE(ADDRESS_FAMILY_IPV6, key_.address_family);
    transaction_a_ = CreateTransaction(ADDRESS_FAMILY_IPV4);
    transaction_a_->Start();
  }

  void StartAAAA() {
    DCHECK(!transaction_aaaa_);
    DCHECK_NE(ADDRESS_FAMILY_IPV4, key_.address_family);
    transaction_aaaa_ = CreateTransaction(ADDRESS_FAMILY_IPV6);
    transaction_aaaa_->Start();
  }

  std::unique_ptr<DnsTransaction> CreateTransaction(AddressFamily family) {
    DCHECK_NE(ADDRESS_FAMILY_UNSPECIFIED, family);
    return client_->GetTransactionFactory()->CreateTransaction(
        key_.hostname,
        family == ADDRESS_FAMILY_IPV6 ? dns_protocol::kTypeAAAA :
                                        dns_protocol::kTypeA,
        base::Bind(&DnsTask::OnTransactionComplete, base::Unretained(this),
                   base::TimeTicks::Now()),
        net_log_);
  }

  void OnTransactionComplete(const base::TimeTicks& start_time,
                             DnsTransaction* transaction,
                             int net_error,
                             const DnsResponse* response) {
    DCHECK(transaction);
    base::TimeDelta duration = base::TimeTicks::Now() - start_time;
    if (net_error != OK) {
      UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.TransactionFailure", duration);
      OnFailure(net_error, DnsResponse::DNS_PARSE_OK);
      return;
    }

    UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.TransactionSuccess", duration);
    switch (transaction->GetType()) {
      case dns_protocol::kTypeA:
        UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.TransactionSuccess_A", duration);
        break;
      case dns_protocol::kTypeAAAA:
        UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.TransactionSuccess_AAAA",
                                     duration);
        break;
    }

    AddressList addr_list;
    base::TimeDelta ttl;
    DnsResponse::Result result = response->ParseToAddressList(&addr_list, &ttl);
    UMA_HISTOGRAM_ENUMERATION("AsyncDNS.ParseToAddressList",
                              result,
                              DnsResponse::DNS_PARSE_RESULT_MAX);
    if (result != DnsResponse::DNS_PARSE_OK) {
      // Fail even if the other query succeeds.
      OnFailure(ERR_DNS_MALFORMED_RESPONSE, result);
      return;
    }

    ++num_completed_transactions_;
    if (num_completed_transactions_ == 1) {
      ttl_ = ttl;
    } else {
      ttl_ = std::min(ttl_, ttl);
    }

    if (transaction->GetType() == dns_protocol::kTypeA) {
      DCHECK_EQ(transaction_a_.get(), transaction);
      // Place IPv4 addresses after IPv6.
      addr_list_.insert(addr_list_.end(), addr_list.begin(), addr_list.end());
    } else {
      DCHECK_EQ(transaction_aaaa_.get(), transaction);
      // Place IPv6 addresses before IPv4.
      addr_list_.insert(addr_list_.begin(), addr_list.begin(), addr_list.end());
    }

    if (needs_two_transactions() && num_completed_transactions_ == 1) {
      // No need to repeat the suffix search.
      key_.hostname = transaction->GetHostname();
      delegate_->OnFirstDnsTransactionComplete();
      return;
    }

    if (addr_list_.empty()) {
      // TODO(szym): Don't fallback to ProcTask in this case.
      OnFailure(ERR_NAME_NOT_RESOLVED, DnsResponse::DNS_PARSE_OK);
      return;
    }

    // If there are multiple addresses, and at least one is IPv6, need to sort
    // them.  Note that IPv6 addresses are always put before IPv4 ones, so it's
    // sufficient to just check the family of the first address.
    if (addr_list_.size() > 1 &&
        addr_list_[0].GetFamily() == ADDRESS_FAMILY_IPV6) {
      // Sort addresses if needed.  Sort could complete synchronously.
      client_->GetAddressSorter()->Sort(
          addr_list_,
          base::Bind(&DnsTask::OnSortComplete,
                     AsWeakPtr(),
                     base::TimeTicks::Now()));
    } else {
      OnSuccess(addr_list_);
    }
  }

  void OnSortComplete(base::TimeTicks start_time,
                      bool success,
                      const AddressList& addr_list) {
    if (!success) {
      UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.SortFailure",
                                   base::TimeTicks::Now() - start_time);
      OnFailure(ERR_DNS_SORT_ERROR, DnsResponse::DNS_PARSE_OK);
      return;
    }

    UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.SortSuccess",
                                 base::TimeTicks::Now() - start_time);

    // AddressSorter prunes unusable destinations.
    if (addr_list.empty()) {
      LOG(WARNING) << "Address list empty after RFC3484 sort";
      OnFailure(ERR_NAME_NOT_RESOLVED, DnsResponse::DNS_PARSE_OK);
      return;
    }

    OnSuccess(addr_list);
  }

  void OnFailure(int net_error, DnsResponse::Result result) {
    DCHECK_NE(OK, net_error);
    net_log_.EndEvent(
        NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK,
        base::Bind(&NetLogDnsTaskFailedCallback, net_error, result));
    delegate_->OnDnsTaskComplete(task_start_time_, net_error, AddressList(),
                                 base::TimeDelta());
  }

  void OnSuccess(const AddressList& addr_list) {
    net_log_.EndEvent(NetLogEventType::HOST_RESOLVER_IMPL_DNS_TASK,
                      addr_list.CreateNetLogCallback());
    delegate_->OnDnsTaskComplete(task_start_time_, OK, addr_list, ttl_);
  }

  DnsClient* client_;
  Key key_;

  // The listener to the results of this DnsTask.
  Delegate* delegate_;
  const NetLogWithSource net_log_;

  std::unique_ptr<DnsTransaction> transaction_a_;
  std::unique_ptr<DnsTransaction> transaction_aaaa_;

  unsigned num_completed_transactions_;

  // These are updated as each transaction completes.
  base::TimeDelta ttl_;
  // IPv6 addresses must appear first in the list.
  AddressList addr_list_;

  base::TimeTicks task_start_time_;

  DISALLOW_COPY_AND_ASSIGN(DnsTask);
};

//-----------------------------------------------------------------------------

// Aggregates all Requests for the same Key. Dispatched via PriorityDispatch.
class HostResolverImpl::Job : public PrioritizedDispatcher::Job,
                              public HostResolverImpl::DnsTask::Delegate {
 public:
  // Creates new job for |key| where |request_net_log| is bound to the
  // request that spawned it.
  Job(const base::WeakPtr<HostResolverImpl>& resolver,
      const Key& key,
      RequestPriority priority,
      const NetLogWithSource& source_net_log)
      : resolver_(resolver),
        key_(key),
        priority_tracker_(priority),
        had_non_speculative_request_(false),
        had_dns_config_(false),
        num_occupied_job_slots_(0),
        dns_task_error_(OK),
        creation_time_(base::TimeTicks::Now()),
        priority_change_time_(creation_time_),
        net_log_(
            NetLogWithSource::Make(source_net_log.net_log(),
                                   NetLogSourceType::HOST_RESOLVER_IMPL_JOB)) {
    source_net_log.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_CREATE_JOB);

    net_log_.BeginEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB,
                        base::Bind(&NetLogJobCreationCallback,
                                   source_net_log.source(), &key_.hostname));
  }

  ~Job() override {
    if (is_running()) {
      // |resolver_| was destroyed with this Job still in flight.
      // Clean-up, record in the log, but don't run any callbacks.
      if (is_proc_running()) {
        proc_task_->Cancel();
        proc_task_ = nullptr;
      }
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
    if (!requests_.empty()) {
      // Log any remaining Requests as cancelled.
      for (RequestImpl* req : requests_) {
        DCHECK_EQ(this, req->job());
        LogCancelRequest(req->source_net_log(), req->info());
        req->OnJobCancelled(this);
      }
      requests_.clear();
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
    DCHECK_EQ(key_.hostname, request->info().hostname());

    priority_tracker_.Add(request->priority());

    request->source_net_log().AddEvent(
        NetLogEventType::HOST_RESOLVER_IMPL_JOB_ATTACH,
        net_log_.source().ToEventParametersCallback());

    net_log_.AddEvent(
        NetLogEventType::HOST_RESOLVER_IMPL_JOB_REQUEST_ATTACH,
        base::Bind(&NetLogJobAttachCallback, request->source_net_log().source(),
                   priority()));

    if (!request->info().is_speculative())
      had_non_speculative_request_ = true;

    requests_.push_back(request);

    UpdatePriority();
  }

  void ChangeRequestPriority(RequestImpl* req, RequestPriority priority) {
    DCHECK_EQ(key_.hostname, req->info().hostname());

    priority_tracker_.Remove(req->priority());
    req->set_priority(priority);
    priority_tracker_.Add(req->priority());
    UpdatePriority();
  }

  // Detach cancelled request. If it was the last active Request, also finishes
  // this Job.
  void CancelRequest(RequestImpl* request) {
    DCHECK_EQ(key_.hostname, request->info().hostname());
    DCHECK(!requests_.empty());

    LogCancelRequest(request->source_net_log(), request->info());

    priority_tracker_.Remove(request->priority());
    net_log_.AddEvent(
        NetLogEventType::HOST_RESOLVER_IMPL_JOB_REQUEST_DETACH,
        base::Bind(&NetLogJobAttachCallback, request->source_net_log().source(),
                   priority()));

    if (num_active_requests() > 0) {
      UpdatePriority();
      RemoveRequest(request);
    } else {
      // If we were called from a Request's callback within CompleteRequests,
      // that Request could not have been cancelled, so num_active_requests()
      // could not be 0. Therefore, we are not in CompleteRequests().
      CompleteRequestsWithError(OK /* cancelled */);
    }
  }

  void RemoveRequest(RequestImpl* request) {
    auto it = std::find(requests_.begin(), requests_.end(), request);
    DCHECK(it != requests_.end());
    requests_.erase(it);
  }

  // Called from AbortAllInProgressJobs. Completes all requests and destroys
  // the job. This currently assumes the abort is due to a network change.
  // TODO This should not delete |this|.
  void Abort() {
    DCHECK(is_running());
    CompleteRequestsWithError(ERR_NETWORK_CHANGED);
  }

  // If DnsTask present, abort it and fall back to ProcTask.
  void AbortDnsTask() {
    if (dns_task_) {
      KillDnsTask();
      dns_task_error_ = OK;
      StartProcTask();
    }
  }

  // Called by HostResolverImpl when this job is evicted due to queue overflow.
  // Completes all requests and destroys the job.
  void OnEvicted() {
    DCHECK(!is_running());
    DCHECK(is_queued());
    handle_.Reset();

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_EVICTED);

    // This signals to CompleteRequests that this job never ran.
    CompleteRequestsWithError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE);
  }

  // Attempts to serve the job from HOSTS. Returns true if succeeded and
  // this Job was destroyed.
  bool ServeFromHosts() {
    DCHECK_GT(num_active_requests(), 0u);
    AddressList addr_list;
    if (resolver_->ServeFromHosts(key(),
                                  requests_.front()->info(),
                                  &addr_list)) {
      // This will destroy the Job.
      CompleteRequests(MakeCacheEntry(OK, addr_list), base::TimeDelta());
      return true;
    }
    return false;
  }

  const Key& key() const { return key_; }

  bool is_queued() const {
    return !handle_.is_null();
  }

  bool is_running() const {
    return is_dns_running() || is_proc_running();
  }

 private:
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

  // MakeCacheEntry() and MakeCacheEntryWithTTL() are helpers to build a
  // HostCache::Entry(). The address list is omited from the cache entry
  // for errors.
  HostCache::Entry MakeCacheEntry(int net_error,
                                  const AddressList& addr_list) const {
    return HostCache::Entry(
        net_error,
        net_error == OK ? MakeAddressListForRequest(addr_list) : AddressList());
  }

  HostCache::Entry MakeCacheEntryWithTTL(int net_error,
                                         const AddressList& addr_list,
                                         base::TimeDelta ttl) const {
    return HostCache::Entry(
        net_error,
        net_error == OK ? MakeAddressListForRequest(addr_list) : AddressList(),
        ttl);
  }

  AddressList MakeAddressListForRequest(const AddressList& list) const {
    if (requests_.empty())
      return list;
    return AddressList::CopyWithPort(list, requests_.front()->info().port());
  }

  void UpdatePriority() {
    if (is_queued()) {
      if (priority() != static_cast<RequestPriority>(handle_.priority()))
        priority_change_time_ = base::TimeTicks::Now();
      handle_ = resolver_->dispatcher_->ChangePriority(handle_, priority());
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

    net_log_.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_JOB_STARTED);

    had_dns_config_ = resolver_->HaveDnsConfig();

    start_time_ = base::TimeTicks::Now();
    base::TimeDelta queue_time = start_time_ - creation_time_;
    base::TimeDelta queue_time_after_change =
        start_time_ - priority_change_time_;

    DNS_HISTOGRAM_BY_PRIORITY("Net.DNS.JobQueueTime", priority(), queue_time);
    DNS_HISTOGRAM_BY_PRIORITY("Net.DNS.JobQueueTimeAfterChange", priority(),
                              queue_time_after_change);

    bool system_only =
        (key_.host_resolver_flags & HOST_RESOLVER_SYSTEM_ONLY) != 0;

    // Caution: Job::Start must not complete synchronously.
    if (!system_only && had_dns_config_ &&
        !ResemblesMulticastDNSName(key_.hostname)) {
      StartDnsTask();
    } else {
      StartProcTask();
    }
  }

  // TODO(szym): Since DnsTransaction does not consume threads, we can increase
  // the limits on |dispatcher_|. But in order to keep the number of
  // TaskScheduler threads low, we will need to use an "inner"
  // PrioritizedDispatcher with tighter limits.
  void StartProcTask() {
    DCHECK(!is_dns_running());
    proc_task_ =
        new ProcTask(key_, resolver_->proc_params_,
                     base::Bind(&Job::OnProcTaskComplete,
                                base::Unretained(this), base::TimeTicks::Now()),
                     net_log_);

    // Start() could be called from within Resolve(), hence it must NOT directly
    // call OnProcTaskComplete, for example, on synchronous failure.
    proc_task_->Start();
  }

  // Called by ProcTask when it completes.
  void OnProcTaskComplete(base::TimeTicks start_time,
                          int net_error,
                          const AddressList& addr_list) {
    DCHECK(is_proc_running());

    if (dns_task_error_ != OK) {
      base::TimeDelta duration = base::TimeTicks::Now() - start_time;
      if (net_error == OK) {
        UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.FallbackSuccess", duration);
        if ((dns_task_error_ == ERR_NAME_NOT_RESOLVED) &&
            ResemblesNetBIOSName(key_.hostname)) {
          UmaAsyncDnsResolveStatus(RESOLVE_STATUS_SUSPECT_NETBIOS);
        } else {
          UmaAsyncDnsResolveStatus(RESOLVE_STATUS_PROC_SUCCESS);
        }
        UMA_HISTOGRAM_SPARSE_SLOWLY("Net.DNS.DnsTask.Errors",
                                    std::abs(dns_task_error_));
        resolver_->OnDnsTaskResolve(dns_task_error_);
      } else {
        UMA_HISTOGRAM_LONG_TIMES_100("AsyncDNS.FallbackFail", duration);
        UmaAsyncDnsResolveStatus(RESOLVE_STATUS_FAIL);
      }
    }

    if (ContainsIcannNameCollisionIp(addr_list))
      net_error = ERR_ICANN_NAME_COLLISION;

    base::TimeDelta ttl =
        base::TimeDelta::FromSeconds(kNegativeCacheEntryTTLSeconds);
    if (net_error == OK)
      ttl = base::TimeDelta::FromSeconds(kCacheEntryTTLSeconds);

    // Don't store the |ttl| in cache since it's not obtained from the server.
    CompleteRequests(MakeCacheEntry(net_error, addr_list), ttl);
  }

  void StartDnsTask() {
    DCHECK(resolver_->HaveDnsConfig());
    dns_task_.reset(new DnsTask(resolver_->dns_client_.get(), key_, this,
                                net_log_));

    dns_task_->StartFirstTransaction();
    // Schedule a second transaction, if needed.
    if (dns_task_->needs_two_transactions())
      Schedule(true);
  }

  void StartSecondDnsTransaction() {
    DCHECK(dns_task_->needs_two_transactions());
    dns_task_->StartSecondTransaction();
  }

  // Called if DnsTask fails. It is posted from StartDnsTask, so Job may be
  // deleted before this callback. In this case dns_task is deleted as well,
  // so we use it as indicator whether Job is still valid.
  void OnDnsTaskFailure(const base::WeakPtr<DnsTask>& dns_task,
                        base::TimeDelta duration,
                        int net_error) {
    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.DnsTask.FailureTime", duration);

    if (!dns_task)
      return;

    dns_task_error_ = net_error;

    // TODO(szym): Run ServeFromHosts now if nsswitch.conf says so.
    // http://crbug.com/117655

    // TODO(szym): Some net errors indicate lack of connectivity. Starting
    // ProcTask in that case is a waste of time.
    if (resolver_->fallback_to_proctask_) {
      KillDnsTask();
      StartProcTask();
    } else {
      UmaAsyncDnsResolveStatus(RESOLVE_STATUS_FAIL);
      CompleteRequestsWithError(net_error);
    }
  }


  // HostResolverImpl::DnsTask::Delegate implementation:

  void OnDnsTaskComplete(base::TimeTicks start_time,
                         int net_error,
                         const AddressList& addr_list,
                         base::TimeDelta ttl) override {
    DCHECK(is_dns_running());

    base::TimeDelta duration = base::TimeTicks::Now() - start_time;
    if (net_error != OK) {
      OnDnsTaskFailure(dns_task_->AsWeakPtr(), duration, net_error);
      return;
    }

    UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.DnsTask.SuccessTime", duration);

    UmaAsyncDnsResolveStatus(RESOLVE_STATUS_DNS_SUCCESS);
    RecordTTL(ttl);

    resolver_->OnDnsTaskResolve(OK);

    base::TimeDelta bounded_ttl =
        std::max(ttl, base::TimeDelta::FromSeconds(kMinimumTTLSeconds));

    if (ContainsIcannNameCollisionIp(addr_list)) {
      CompleteRequestsWithError(ERR_ICANN_NAME_COLLISION);
    } else {
      CompleteRequests(MakeCacheEntryWithTTL(net_error, addr_list, ttl),
                       bounded_ttl);
    }
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

  void RecordJobHistograms(int error) {
    enum Category {  // Used in UMA_HISTOGRAM_ENUMERATION.
      RESOLVE_SUCCESS,
      RESOLVE_FAIL,
      RESOLVE_SPECULATIVE_SUCCESS,
      RESOLVE_SPECULATIVE_FAIL,
      RESOLVE_MAX,  // Bounding value.
    };
    Category category = RESOLVE_MAX;  // Illegal value for later DCHECK only.

    base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
    if (error == OK) {
      if (had_non_speculative_request_) {
        category = RESOLVE_SUCCESS;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime", duration);
        switch (key_.address_family) {
          case ADDRESS_FAMILY_IPV4:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV4",
                                         duration);
            break;
          case ADDRESS_FAMILY_IPV6:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV6",
                                         duration);
            break;
          case ADDRESS_FAMILY_UNSPECIFIED:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.UNSPEC",
                                         duration);
            break;
        }
      } else {
        category = RESOLVE_SPECULATIVE_SUCCESS;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.Speculative",
                                     duration);
      }
    } else {
      if (had_non_speculative_request_) {
        category = RESOLVE_FAIL;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime", duration);
        switch (key_.address_family) {
          case ADDRESS_FAMILY_IPV4:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV4",
                                         duration);
            break;
          case ADDRESS_FAMILY_IPV6:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.IPV6",
                                         duration);
            break;
          case ADDRESS_FAMILY_UNSPECIFIED:
            UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveSuccessTime.UNSPEC",
                                         duration);
            break;
        }
      } else {
        category = RESOLVE_SPECULATIVE_FAIL;
        UMA_HISTOGRAM_LONG_TIMES_100("Net.DNS.ResolveFailureTime.Speculative",
                                     duration);
      }
    }
    DCHECK_LT(static_cast<int>(category),
              static_cast<int>(RESOLVE_MAX));  // Be sure it was set.
    UMA_HISTOGRAM_ENUMERATION("Net.DNS.ResolveCategory", category, RESOLVE_MAX);
  }

  // Performs Job's last rites. Completes all Requests. Deletes this.
  void CompleteRequests(const HostCache::Entry& entry,
                        base::TimeDelta ttl) {
    CHECK(resolver_.get());

    // This job must be removed from resolver's |jobs_| now to make room for a
    // new job with the same key in case one of the OnComplete callbacks decides
    // to spawn one. Consequently, the job deletes itself when CompleteRequests
    // is done.
    std::unique_ptr<Job> self_deleter(this);

    resolver_->RemoveJob(this);

    if (is_running()) {
      if (is_proc_running()) {
        DCHECK(!is_queued());
        proc_task_->Cancel();
        proc_task_ = nullptr;
      }
      KillDnsTask();

      // Signal dispatcher that a slot has opened.
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
                                      entry.error());

    resolver_->SchedulePersist();

    DCHECK(!requests_.empty());

    if (entry.error() == OK || entry.error() == ERR_ICANN_NAME_COLLISION) {
      // Record this histogram here, when we know the system has a valid DNS
      // configuration.
      UMA_HISTOGRAM_BOOLEAN("AsyncDNS.HaveDnsConfig",
                            resolver_->received_dns_config_);
    }

    bool did_complete = (entry.error() != ERR_NETWORK_CHANGED) &&
                        (entry.error() != ERR_HOST_RESOLVER_QUEUE_TOO_LARGE);
    if (did_complete) {
      resolver_->CacheResult(key_, entry, ttl);
      RecordJobHistograms(entry.error());
    }

    // Complete all of the requests that were attached to the job and
    // detach them.
    while (!requests_.empty()) {
      RequestImpl* req = requests_.front();
      requests_.pop_front();
      DCHECK_EQ(this, req->job());
      // Update the net log and notify registered observers.
      LogFinishRequest(req->source_net_log(), req->info(), entry.error());
      if (did_complete) {
        // Record effective total time from creation to completion.
        RecordTotalTime(req->info().is_speculative(), false,
                        base::TimeTicks::Now() - req->request_time());
      }
      req->OnJobCompleted(this, entry.error(), entry.addresses());

      // Check if the resolver was destroyed as a result of running the
      // callback. If it was, we could continue, but we choose to bail.
      if (!resolver_.get())
        return;
    }
  }

  // Convenience wrapper for CompleteRequests in case of failure.
  void CompleteRequestsWithError(int net_error) {
    CompleteRequests(HostCache::Entry(net_error, AddressList()),
                     base::TimeDelta());
  }

  RequestPriority priority() const {
    return priority_tracker_.highest_priority();
  }

  // Number of non-canceled requests in |requests_|.
  size_t num_active_requests() const {
    return priority_tracker_.total_count();
  }

  bool is_dns_running() const { return !!dns_task_; }

  bool is_proc_running() const { return !!proc_task_; }

  base::WeakPtr<HostResolverImpl> resolver_;

  Key key_;

  // Tracks the highest priority across |requests_|.
  PriorityTracker priority_tracker_;

  bool had_non_speculative_request_;

  // Distinguishes measurements taken while DnsClient was fully configured.
  bool had_dns_config_;

  // Number of slots occupied by this Job in resolver's PrioritizedDispatcher.
  unsigned num_occupied_job_slots_;

  // Result of DnsTask.
  int dns_task_error_;

  const base::TimeTicks creation_time_;
  base::TimeTicks priority_change_time_;
  base::TimeTicks start_time_;

  NetLogWithSource net_log_;

  // Resolves the host using a HostResolverProc.
  scoped_refptr<ProcTask> proc_task_;

  // Resolves the host using a DnsTransaction.
  std::unique_ptr<DnsTask> dns_task_;

  // All Requests waiting for the result of this Job. Some can be canceled.
  base::circular_deque<RequestImpl*> requests_;

  // A handle used in |HostResolverImpl::dispatcher_|.
  PrioritizedDispatcher::Handle handle_;
};

//-----------------------------------------------------------------------------

HostResolverImpl::ProcTaskParams::ProcTaskParams(
    HostResolverProc* resolver_proc,
    size_t max_retry_attempts)
    : resolver_proc(resolver_proc),
      max_retry_attempts(max_retry_attempts),
      unresponsive_delay(
          base::TimeDelta::FromMilliseconds(kDnsDefaultUnresponsiveDelayMs)),
      retry_factor(2) {
  // Maximum of 4 retry attempts for host resolution.
  static const size_t kDefaultMaxRetryAttempts = 4u;
  if (max_retry_attempts == HostResolver::kDefaultRetryAttempts)
    max_retry_attempts = kDefaultMaxRetryAttempts;
}

HostResolverImpl::ProcTaskParams::ProcTaskParams(const ProcTaskParams& other) =
    default;

HostResolverImpl::ProcTaskParams::~ProcTaskParams() {}

HostResolverImpl::~HostResolverImpl() {
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

void HostResolverImpl::SetMaxQueuedJobs(size_t value) {
  DCHECK_EQ(0u, dispatcher_->num_queued_jobs());
  DCHECK_GT(value, 0u);
  max_queued_jobs_ = value;
}

int HostResolverImpl::Resolve(const RequestInfo& info,
                              RequestPriority priority,
                              AddressList* addresses,
                              const CompletionCallback& callback,
                              std::unique_ptr<Request>* out_req,
                              const NetLogWithSource& source_net_log) {
  DCHECK(addresses);
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK_EQ(false, callback.is_null());
  DCHECK(out_req);

  LogStartRequest(source_net_log, info);

  Key key;
  int rv = ResolveHelper(info, false, nullptr, source_net_log, addresses, &key);
  if (rv != ERR_DNS_CACHE_MISS) {
    LogFinishRequest(source_net_log, info, rv);
    RecordTotalTime(info.is_speculative(), true, base::TimeDelta());
    return rv;
  }

  // Next we need to attach our request to a "job". This job is responsible for
  // calling "getaddrinfo(hostname)" on a worker thread.

  auto jobit = jobs_.find(key);
  Job* job;
  if (jobit == jobs_.end()) {
    job =
        new Job(weak_ptr_factory_.GetWeakPtr(), key, priority, source_net_log);
    job->Schedule(false);

    // Check for queue overflow.
    if (dispatcher_->num_queued_jobs() > max_queued_jobs_) {
      Job* evicted = static_cast<Job*>(dispatcher_->EvictOldestLowest());
      DCHECK(evicted);
      evicted->OnEvicted();  // Deletes |evicted|.
      if (evicted == job) {
        rv = ERR_HOST_RESOLVER_QUEUE_TOO_LARGE;
        LogFinishRequest(source_net_log, info, rv);
        return rv;
      }
    }
    jobs_[key] = base::WrapUnique(job);
  } else {
    job = jobit->second.get();
  }

  // Can't complete synchronously. Create and attach request.
  auto req = std::make_unique<RequestImpl>(source_net_log, info, priority,
                                           callback, addresses, job);
  job->AddRequest(req.get());
  *out_req = std::move(req);

  // Completion happens during Job::CompleteRequests().
  return ERR_IO_PENDING;
}

HostResolverImpl::HostResolverImpl(const Options& options, NetLog* net_log)
    : max_queued_jobs_(0),
      proc_params_(NULL, options.max_retry_attempts),
      net_log_(net_log),
      received_dns_config_(false),
      num_dns_failures_(0),
      assume_ipv6_failure_on_wifi_(false),
      use_local_ipv6_(false),
      last_ipv6_probe_result_(true),
      additional_resolver_flags_(0),
      fallback_to_proctask_(true),
      persist_initialized_(false),
      weak_ptr_factory_(this),
      probe_weak_ptr_factory_(this) {
  if (options.enable_caching)
    cache_ = HostCache::CreateDefaultCache();

  PrioritizedDispatcher::Limits job_limits = options.GetDispatcherLimits();
  dispatcher_.reset(new PrioritizedDispatcher(job_limits));
  max_queued_jobs_ = job_limits.total_jobs * 100u;

  DCHECK_GE(dispatcher_->num_priorities(), static_cast<size_t>(NUM_PRIORITIES));

#if defined(OS_WIN)
  EnsureWinsockInit();
#endif
#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)
  RunLoopbackProbeJob();
#endif
  NetworkChangeNotifier::AddIPAddressObserver(this);
  NetworkChangeNotifier::AddConnectionTypeObserver(this);
  NetworkChangeNotifier::AddDNSObserver(this);
#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_OPENBSD) && \
    !defined(OS_ANDROID) && !defined(OS_FUCHSIA)
  EnsureDnsReloaderInit();
#endif

  OnConnectionTypeChanged(NetworkChangeNotifier::GetConnectionType());

  {
    DnsConfig dns_config;
    NetworkChangeNotifier::GetDnsConfig(&dns_config);
    received_dns_config_ = dns_config.IsValid();
    // Conservatively assume local IPv6 is needed when DnsConfig is not valid.
    use_local_ipv6_ = !dns_config.IsValid() || dns_config.use_local_ipv6;
  }

  fallback_to_proctask_ = !ConfigureAsyncDnsNoFallbackFieldTrial();
}

void HostResolverImpl::SetHaveOnlyLoopbackAddresses(bool result) {
  if (result) {
    additional_resolver_flags_ |= HOST_RESOLVER_LOOPBACK_ONLY;
  } else {
    additional_resolver_flags_ &= ~HOST_RESOLVER_LOOPBACK_ONLY;
  }
}

int HostResolverImpl::ResolveHelper(const RequestInfo& info,
                                    bool allow_stale,
                                    HostCache::EntryStaleness* stale_info,
                                    const NetLogWithSource& source_net_log,
                                    AddressList* addresses,
                                    Key* key) {
  IPAddress ip_address;
  IPAddress* ip_address_ptr = nullptr;
  if (ip_address.AssignFromIPLiteral(info.hostname())) {
    ip_address_ptr = &ip_address;
  } else {
    // Check that the caller supplied a valid hostname to resolve.
    if (!IsValidDNSDomain(info.hostname()))
      return ERR_NAME_NOT_RESOLVED;
  }

  // Build a key that identifies the request in the cache and in the
  // outstanding jobs map.
  *key = GetEffectiveKeyForRequest(info, ip_address_ptr, source_net_log);

  DCHECK(allow_stale == !!stale_info);
  // The result of |getaddrinfo| for empty hosts is inconsistent across systems.
  // On Windows it gives the default interface's address, whereas on Linux it
  // gives an error. We will make it fail on all platforms for consistency.
  if (info.hostname().empty() || info.hostname().size() > kMaxHostLength) {
    MakeNotStale(stale_info);
    return ERR_NAME_NOT_RESOLVED;
  }

  int net_error = ERR_UNEXPECTED;
  if (ResolveAsIP(*key, info, ip_address_ptr, &net_error, addresses)) {
    MakeNotStale(stale_info);
    return net_error;
  }

  // Special-case localhost names, as per the recommendations in
  // https://tools.ietf.org/html/draft-west-let-localhost-be-localhost.
  if (ServeLocalhost(*key, info, addresses)) {
    MakeNotStale(stale_info);
    return OK;
  }

  if (ServeFromCache(*key, info, &net_error, addresses, allow_stale,
                     stale_info)) {
    source_net_log.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_CACHE_HIT,
                            addresses->CreateNetLogCallback());
    // |ServeFromCache()| will set |*stale_info| as needed.
    return net_error;
  }

  // TODO(szym): Do not do this if nsswitch.conf instructs not to.
  // http://crbug.com/117655
  if (ServeFromHosts(*key, info, addresses)) {
    source_net_log.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_HOSTS_HIT,
                            addresses->CreateNetLogCallback());
    MakeNotStale(stale_info);
    return OK;
  }

  return ERR_DNS_CACHE_MISS;
}

int HostResolverImpl::ResolveFromCache(const RequestInfo& info,
                                       AddressList* addresses,
                                       const NetLogWithSource& source_net_log) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(addresses);

  // Update the net log and notify registered observers.
  LogStartRequest(source_net_log, info);

  Key key;
  int rv = ResolveHelper(info, false, nullptr, source_net_log, addresses, &key);

  LogFinishRequest(source_net_log, info, rv);
  return rv;
}

void HostResolverImpl::SetDnsClientEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
#if defined(ENABLE_BUILT_IN_DNS)
  if (enabled && !dns_client_) {
    SetDnsClient(DnsClient::CreateClient(net_log_));
  } else if (!enabled && dns_client_) {
    SetDnsClient(std::unique_ptr<DnsClient>());
  }
#endif
}

HostCache* HostResolverImpl::GetHostCache() {
  return cache_.get();
}

std::unique_ptr<base::Value> HostResolverImpl::GetDnsConfigAsValue() const {
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

int HostResolverImpl::ResolveStaleFromCache(
    const RequestInfo& info,
    AddressList* addresses,
    HostCache::EntryStaleness* stale_info,
    const NetLogWithSource& source_net_log) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(addresses);
  DCHECK(stale_info);

  // Update the net log and notify registered observers.
  LogStartRequest(source_net_log, info);

  Key key;
  int rv =
      ResolveHelper(info, true, stale_info, source_net_log, addresses, &key);
  LogFinishRequest(source_net_log, info, rv);
  return rv;
}

size_t HostResolverImpl::LastRestoredCacheSize() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  return cache_ ? cache_->last_restore_size() : 0;
}

size_t HostResolverImpl::CacheSize() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  return cache_ ? cache_->size() : 0;
}

void HostResolverImpl::SetNoIPv6OnWifi(bool no_ipv6_on_wifi) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  assume_ipv6_failure_on_wifi_ = no_ipv6_on_wifi;
}

bool HostResolverImpl::GetNoIPv6OnWifi() {
  return assume_ipv6_failure_on_wifi_;
}

bool HostResolverImpl::ResolveAsIP(const Key& key,
                                   const RequestInfo& info,
                                   const IPAddress* ip_address,
                                   int* net_error,
                                   AddressList* addresses) {
  DCHECK(addresses);
  DCHECK(net_error);
  if (ip_address == nullptr)
    return false;

  *net_error = OK;
  AddressFamily family = GetAddressFamily(*ip_address);
  if (key.address_family != ADDRESS_FAMILY_UNSPECIFIED &&
      key.address_family != family) {
    // Don't return IPv6 addresses for IPv4 queries, and vice versa.
    *net_error = ERR_NAME_NOT_RESOLVED;
  } else {
    *addresses = AddressList::CreateFromIPAddress(*ip_address, info.port());
    if (key.host_resolver_flags & HOST_RESOLVER_CANONNAME)
      addresses->SetDefaultCanonicalName();
  }
  return true;
}

bool HostResolverImpl::ServeFromCache(const Key& key,
                                      const RequestInfo& info,
                                      int* net_error,
                                      AddressList* addresses,
                                      bool allow_stale,
                                      HostCache::EntryStaleness* stale_info) {
  DCHECK(addresses);
  DCHECK(net_error);
  DCHECK(allow_stale == !!stale_info);
  if (!info.allow_cached_response() || !cache_.get())
    return false;

  const HostCache::Entry* cache_entry;
  if (allow_stale)
    cache_entry = cache_->LookupStale(key, base::TimeTicks::Now(), stale_info);
  else
    cache_entry = cache_->Lookup(key, base::TimeTicks::Now());
  if (!cache_entry)
    return false;

  *net_error = cache_entry->error();
  if (*net_error == OK) {
    if (cache_entry->has_ttl())
      RecordTTL(cache_entry->ttl());
    *addresses = EnsurePortOnAddressList(cache_entry->addresses(), info.port());
  }
  return true;
}

bool HostResolverImpl::ServeFromHosts(const Key& key,
                                      const RequestInfo& info,
                                      AddressList* addresses) {
  DCHECK(addresses);
  if (!HaveDnsConfig())
    return false;
  addresses->clear();

  // HOSTS lookups are case-insensitive.
  std::string hostname = base::ToLowerASCII(key.hostname);

  const DnsHosts& hosts = dns_client_->GetConfig()->hosts;

  // If |address_family| is ADDRESS_FAMILY_UNSPECIFIED other implementations
  // (glibc and c-ares) return the first matching line. We have more
  // flexibility, but lose implicit ordering.
  // We prefer IPv6 because "happy eyeballs" will fall back to IPv4 if
  // necessary.
  if (key.address_family == ADDRESS_FAMILY_IPV6 ||
      key.address_family == ADDRESS_FAMILY_UNSPECIFIED) {
    DnsHosts::const_iterator it = hosts.find(
        DnsHostsKey(hostname, ADDRESS_FAMILY_IPV6));
    if (it != hosts.end())
      addresses->push_back(IPEndPoint(it->second, info.port()));
  }

  if (key.address_family == ADDRESS_FAMILY_IPV4 ||
      key.address_family == ADDRESS_FAMILY_UNSPECIFIED) {
    DnsHosts::const_iterator it = hosts.find(
        DnsHostsKey(hostname, ADDRESS_FAMILY_IPV4));
    if (it != hosts.end())
      addresses->push_back(IPEndPoint(it->second, info.port()));
  }

  // If got only loopback addresses and the family was restricted, resolve
  // again, without restrictions. See SystemHostResolverCall for rationale.
  if ((key.host_resolver_flags &
          HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6) &&
      IsAllIPv4Loopback(*addresses)) {
    Key new_key(key);
    new_key.address_family = ADDRESS_FAMILY_UNSPECIFIED;
    new_key.host_resolver_flags &=
        ~HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6;
    return ServeFromHosts(new_key, info, addresses);
  }
  return !addresses->empty();
}

bool HostResolverImpl::ServeLocalhost(const Key& key,
                                      const RequestInfo& info,
                                      AddressList* addresses) {
  AddressList resolved_addresses;
  if (!ResolveLocalHostname(key.hostname, info.port(), &resolved_addresses))
    return false;

  addresses->clear();

  for (const auto& address : resolved_addresses) {
    // Include the address if:
    // - caller didn't specify an address family, or
    // - caller specifically asked for the address family of this address, or
    // - this is an IPv6 address and caller specifically asked for IPv4 due
    //   to lack of detected IPv6 support. (See SystemHostResolverCall for
    //   rationale).
    if (key.address_family == ADDRESS_FAMILY_UNSPECIFIED ||
        key.address_family == address.GetFamily() ||
        (address.GetFamily() == ADDRESS_FAMILY_IPV6 &&
         key.address_family == ADDRESS_FAMILY_IPV4 &&
         (key.host_resolver_flags &
          HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6))) {
      addresses->push_back(address);
    }
  }

  return true;
}

void HostResolverImpl::CacheResult(const Key& key,
                                   const HostCache::Entry& entry,
                                   base::TimeDelta ttl) {
  // Don't cache an error unless it has a positive TTL.
  if (cache_.get() && (entry.error() == OK || ttl > base::TimeDelta()))
    cache_->Set(key, entry, base::TimeTicks::Now(), ttl);
}

void HostResolverImpl::RemoveJob(Job* job) {
  DCHECK(job);
  auto it = jobs_.find(job->key());
  if (it != jobs_.end() && it->second.get() == job) {
    it->second.release();
    jobs_.erase(it);
  }
}

HostResolverImpl::Key HostResolverImpl::GetEffectiveKeyForRequest(
    const RequestInfo& info,
    const IPAddress* ip_address,
    const NetLogWithSource& net_log) {
  HostResolverFlags effective_flags =
      info.host_resolver_flags() | additional_resolver_flags_;
  AddressFamily effective_address_family = info.address_family();

  if (effective_address_family == ADDRESS_FAMILY_UNSPECIFIED &&
      // When resolving IPv4 literals, there's no need to probe for IPv6.
      // When resolving IPv6 literals, there's no benefit to artificially
      // limiting our resolution based on a probe.  Prior logic ensures
      // that this query is UNSPECIFIED (see effective_address_family
      // check above) so the code requesting the resolution should be amenable
      // to receiving a IPv6 resolution.
      !use_local_ipv6_ && ip_address == nullptr && !IsIPv6Reachable(net_log)) {
    effective_address_family = ADDRESS_FAMILY_IPV4;
    effective_flags |= HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6;
  }

  return Key(info.hostname(), effective_address_family, effective_flags);
}

bool HostResolverImpl::IsIPv6Reachable(const NetLogWithSource& net_log) {
  // Don't bother checking if the device is on WiFi and IPv6 is assumed to not
  // work on WiFi.
  if (assume_ipv6_failure_on_wifi_ &&
      NetworkChangeNotifier::GetConnectionType() ==
          NetworkChangeNotifier::CONNECTION_WIFI) {
    return false;
  }

  // Cache the result for kIPv6ProbePeriodMs (measured from after
  // IsGloballyReachable() completes).
  bool cached = true;
  if ((base::TimeTicks::Now() - last_ipv6_probe_time_).InMilliseconds() >
      kIPv6ProbePeriodMs) {
    last_ipv6_probe_result_ =
        IsGloballyReachable(IPAddress(kIPv6ProbeAddress), net_log);
    last_ipv6_probe_time_ = base::TimeTicks::Now();
    cached = false;
  }
  net_log.AddEvent(NetLogEventType::HOST_RESOLVER_IMPL_IPV6_REACHABILITY_CHECK,
                   base::Bind(&NetLogIPv6AvailableCallback,
                              last_ipv6_probe_result_, cached));
  return last_ipv6_probe_result_;
}

bool HostResolverImpl::IsGloballyReachable(const IPAddress& dest,
                                           const NetLogWithSource& net_log) {
  std::unique_ptr<DatagramClientSocket> socket(
      ClientSocketFactory::GetDefaultFactory()->CreateDatagramClientSocket(
          DatagramSocket::DEFAULT_BIND, RandIntCallback(), net_log.net_log(),
          net_log.source()));
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

void HostResolverImpl::RunLoopbackProbeJob() {
  // Run this asynchronously as it can take 40-100ms and should not block
  // initialization.
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&HaveOnlyLoopbackAddresses),
      base::BindOnce(&HostResolverImpl::SetHaveOnlyLoopbackAddresses,
                     weak_ptr_factory_.GetWeakPtr()));
}

void HostResolverImpl::AbortAllInProgressJobs() {
  // In Abort, a Request callback could spawn new Jobs with matching keys, so
  // first collect and remove all running jobs from |jobs_|.
  std::vector<std::unique_ptr<Job>> jobs_to_abort;
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    Job* job = it->second.get();
    if (job->is_running()) {
      jobs_to_abort.push_back(std::move(it->second));
      jobs_.erase(it++);
    } else {
      DCHECK(job->is_queued());
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
  base::WeakPtr<HostResolverImpl> self = weak_ptr_factory_.GetWeakPtr();

  // Then Abort them.
  for (size_t i = 0; self.get() && i < jobs_to_abort.size(); ++i) {
    jobs_to_abort[i]->Abort();
    ignore_result(jobs_to_abort[i].release());
  }

  if (self)
    dispatcher_->SetLimits(limits);
}

void HostResolverImpl::AbortDnsTasks() {
  // Pause the dispatcher so it won't start any new dispatcher jobs while
  // aborting the old ones.  This is needed so that it won't start the second
  // DnsTransaction for a job if the DnsConfig just changed.
  PrioritizedDispatcher::Limits limits = dispatcher_->GetLimits();
  dispatcher_->SetLimits(
      PrioritizedDispatcher::Limits(limits.reserved_slots.size(), 0));

  for (auto it = jobs_.begin(); it != jobs_.end(); ++it)
    it->second->AbortDnsTask();
  dispatcher_->SetLimits(limits);
}

void HostResolverImpl::TryServingAllJobsFromHosts() {
  if (!HaveDnsConfig())
    return;

  // TODO(szym): Do not do this if nsswitch.conf instructs not to.
  // http://crbug.com/117655

  // Life check to bail once |this| is deleted.
  base::WeakPtr<HostResolverImpl> self = weak_ptr_factory_.GetWeakPtr();

  for (auto it = jobs_.begin(); self.get() && it != jobs_.end();) {
    Job* job = it->second.get();
    ++it;
    // This could remove |job| from |jobs_|, but iterator will remain valid.
    job->ServeFromHosts();
  }
}

void HostResolverImpl::OnIPAddressChanged() {
  last_ipv6_probe_time_ = base::TimeTicks();
  // Abandon all ProbeJobs.
  probe_weak_ptr_factory_.InvalidateWeakPtrs();
  if (cache_.get())
    cache_->OnNetworkChange();
#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)
  RunLoopbackProbeJob();
#endif
  AbortAllInProgressJobs();
  // |this| may be deleted inside AbortAllInProgressJobs().
}

void HostResolverImpl::OnConnectionTypeChanged(
    NetworkChangeNotifier::ConnectionType type) {
  proc_params_.unresponsive_delay =
      GetTimeDeltaForConnectionTypeFromFieldTrialOrDefault(
          "DnsUnresponsiveDelayMsByConnectionType",
          base::TimeDelta::FromMilliseconds(kDnsDefaultUnresponsiveDelayMs),
          type);
}

void HostResolverImpl::OnInitialDNSConfigRead() {
  UpdateDNSConfig(false);
}

void HostResolverImpl::OnDNSChanged() {
  UpdateDNSConfig(true);
}

void HostResolverImpl::UpdateDNSConfig(bool config_changed) {
  DnsConfig dns_config;
  NetworkChangeNotifier::GetDnsConfig(&dns_config);

  if (net_log_) {
    net_log_->AddGlobalEntry(NetLogEventType::DNS_CONFIG_CHANGED,
                             base::Bind(&NetLogDnsConfigCallback, &dns_config));
  }

  // TODO(szym): Remove once http://crbug.com/137914 is resolved.
  received_dns_config_ = dns_config.IsValid();
  // Conservatively assume local IPv6 is needed when DnsConfig is not valid.
  use_local_ipv6_ = !dns_config.IsValid() || dns_config.use_local_ipv6;

  num_dns_failures_ = 0;

  // We want a new DnsSession in place, before we Abort running Jobs, so that
  // the newly started jobs use the new config.
  if (dns_client_.get()) {
    dns_client_->SetConfig(dns_config);
    if (dns_client_->GetConfig()) {
      UMA_HISTOGRAM_BOOLEAN("AsyncDNS.DnsClientEnabled", true);
      // If we just switched DnsClients, restart jobs using new resolver.
      // TODO(pauljensen): Is this necessary?
      config_changed = true;
    }
  }

  if (config_changed) {
    // If the DNS server has changed, existing cached info could be wrong so we
    // have to expire our internal cache :( Note that OS level DNS caches, such
    // as NSCD's cache should be dropped automatically by the OS when
    // resolv.conf changes so we don't need to do anything to clear that cache.
    if (cache_.get())
      cache_->OnNetworkChange();

    // Life check to bail once |this| is deleted.
    base::WeakPtr<HostResolverImpl> self = weak_ptr_factory_.GetWeakPtr();

    // Existing jobs will have been sent to the original server so they need to
    // be aborted.
    AbortAllInProgressJobs();

    // |this| may be deleted inside AbortAllInProgressJobs().
    if (self.get())
      TryServingAllJobsFromHosts();
  }
}

bool HostResolverImpl::HaveDnsConfig() const {
  // Use DnsClient only if it's fully configured and there is no override by
  // ScopedDefaultHostResolverProc.
  // The alternative is to use NetworkChangeNotifier to override DnsConfig,
  // but that would introduce construction order requirements for NCN and SDHRP.
  return dns_client_ && dns_client_->GetConfig() &&
         (proc_params_.resolver_proc || !HostResolverProc::GetDefault());
}

void HostResolverImpl::OnDnsTaskResolve(int net_error) {
  DCHECK(dns_client_);
  if (net_error == OK) {
    num_dns_failures_ = 0;
    return;
  }
  ++num_dns_failures_;
  if (num_dns_failures_ < kMaximumDnsFailures)
    return;

  // Disable DnsClient until the next DNS change.  Must be done before aborting
  // DnsTasks, since doing so may start new jobs.
  dns_client_->SetConfig(DnsConfig());

  // Switch jobs with active DnsTasks over to using ProcTasks.
  AbortDnsTasks();

  UMA_HISTOGRAM_BOOLEAN("AsyncDNS.DnsClientEnabled", false);
  UMA_HISTOGRAM_SPARSE_SLOWLY("AsyncDNS.DnsClientDisabledReason",
                              std::abs(net_error));
}

void HostResolverImpl::SetDnsClient(std::unique_ptr<DnsClient> dns_client) {
  // DnsClient and config must be updated before aborting DnsTasks, since doing
  // so may start new jobs.
  dns_client_ = std::move(dns_client);
  if (dns_client_ && !dns_client_->GetConfig() &&
      num_dns_failures_ < kMaximumDnsFailures) {
    DnsConfig dns_config;
    NetworkChangeNotifier::GetDnsConfig(&dns_config);
    dns_client_->SetConfig(dns_config);
    num_dns_failures_ = 0;
    if (dns_client_->GetConfig())
      UMA_HISTOGRAM_BOOLEAN("AsyncDNS.DnsClientEnabled", true);
  }

  AbortDnsTasks();
}

void HostResolverImpl::InitializePersistence(
    const PersistCallback& persist_callback,
    std::unique_ptr<const base::Value> old_data) {
  DCHECK(!persist_initialized_);
  persist_callback_ = persist_callback;
  persist_initialized_ = true;
  if (old_data)
    ApplyPersistentData(std::move(old_data));
}

void HostResolverImpl::ApplyPersistentData(
    std::unique_ptr<const base::Value> data) {}

std::unique_ptr<const base::Value> HostResolverImpl::GetPersistentData() {
  return std::unique_ptr<const base::Value>();
}

void HostResolverImpl::SchedulePersist() {
  if (!persist_initialized_ || persist_timer_.IsRunning())
    return;
  persist_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(kPersistDelaySec),
      base::Bind(&HostResolverImpl::DoPersist, weak_ptr_factory_.GetWeakPtr()));
}

void HostResolverImpl::DoPersist() {
  DCHECK(persist_initialized_);
  persist_callback_.Run(GetPersistentData());
}

HostResolverImpl::RequestImpl::~RequestImpl() {
  if (job_)
    job_->CancelRequest(this);
}

void HostResolverImpl::RequestImpl::ChangeRequestPriority(
    RequestPriority priority) {
  job_->ChangeRequestPriority(this, priority);
}

}  // namespace net
