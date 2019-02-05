// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_IMPL_H_
#define NET_DNS_HOST_RESOLVER_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/completion_once_callback.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_config_overrides.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_proc.h"
#include "net/dns/public/dns_query_type.h"
#include "net/url_request/url_request_context.h"
#include "url/gurl.h"

namespace base {
class TickClock;
}  // namespace base

namespace net {

class AddressList;
class DnsClient;
class IPAddress;
class MDnsClient;
class MDnsSocketFactory;
class NetLog;
class NetLogWithSource;

// For each hostname that is requested, HostResolver creates a
// HostResolverImpl::Job. When this job gets dispatched it creates a task
// (ProcTask for the system resolver or DnsTask for the async resolver) which
// resolves the hostname. If requests for that same host are made during the
// job's lifetime, they are attached to the existing job rather than creating a
// new one. This avoids doing parallel resolves for the same host.
//
// The way these classes fit together is illustrated by:
//
//
//            +----------- HostResolverImpl -------------+
//            |                    |                     |
//           Job                  Job                   Job
//    (for host1, fam1)    (for host2, fam2)     (for hostx, famx)
//       /    |   |            /   |   |             /   |   |
//   Request ... Request  Request ... Request   Request ... Request
//  (port1)     (port2)  (port3)      (port4)  (port5)      (portX)
//
// When a HostResolverImpl::Job finishes, the callbacks of each waiting request
// are run on the origin thread.
//
// Thread safety: This class is not threadsafe, and must only be called
// from one thread!
//
// The HostResolverImpl enforces limits on the maximum number of concurrent
// threads using PrioritizedDispatcher::Limits.
//
// Jobs are ordered in the queue based on their priority and order of arrival.
class NET_EXPORT HostResolverImpl
    : public HostResolver,
      public NetworkChangeNotifier::IPAddressObserver,
      public NetworkChangeNotifier::ConnectionTypeObserver,
      public NetworkChangeNotifier::DNSObserver {
 public:
  // Parameters for ProcTask which resolves hostnames using HostResolveProc.
  //
  // |resolver_proc| is used to perform the actual resolves; it must be
  // thread-safe since it may be run from multiple worker threads. If
  // |resolver_proc| is NULL then the default host resolver procedure is
  // used (which is SystemHostResolverProc except if overridden).
  //
  // For each attempt, we could start another attempt if host is not resolved
  // within |unresponsive_delay| time. We keep attempting to resolve the host
  // for |max_retry_attempts|. For every retry attempt, we grow the
  // |unresponsive_delay| by the |retry_factor| amount (that is retry interval
  // is multiplied by the retry factor each time). Once we have retried
  // |max_retry_attempts|, we give up on additional attempts.
  //
  struct NET_EXPORT_PRIVATE ProcTaskParams {
    // Sets up defaults.
    ProcTaskParams(HostResolverProc* resolver_proc, size_t max_retry_attempts);

    ProcTaskParams(const ProcTaskParams& other);

    ~ProcTaskParams();

    // The procedure to use for resolving host names. This will be NULL, except
    // in the case of unit-tests which inject custom host resolving behaviors.
    scoped_refptr<HostResolverProc> resolver_proc;

    // Maximum number retry attempts to resolve the hostname.
    // Pass HostResolver::kDefaultRetryAttempts to choose a default value.
    size_t max_retry_attempts;

    // This is the limit after which we make another attempt to resolve the host
    // if the worker thread has not responded yet.
    base::TimeDelta unresponsive_delay;

    // Factor to grow |unresponsive_delay| when we re-re-try.
    uint32_t retry_factor;
  };

  // Creates a HostResolver as specified by |options|. Blocking tasks are run in
  // TaskScheduler.
  //
  // If Options.enable_caching is true, a cache is created using
  // HostCache::CreateDefaultCache(). Otherwise no cache is used.
  //
  // Options.GetDispatcherLimits() determines the maximum number of jobs that
  // the resolver will run at once. This upper-bounds the total number of
  // outstanding DNS transactions (not counting retransmissions and retries).
  //
  // |net_log| must remain valid for the life of the HostResolverImpl.
  HostResolverImpl(const Options& options, NetLog* net_log);

  // If any completion callbacks are pending when the resolver is destroyed,
  // the host resolutions are cancelled, and the completion callbacks will not
  // be called.
  ~HostResolverImpl() override;

  // Set the DnsClient to be used for resolution. In case of failure, the
  // HostResolverProc from ProcTaskParams will be queried. If the DnsClient is
  // not pre-configured with a valid DnsConfig, a new config is fetched from
  // NetworkChangeNotifier.
  void SetDnsClient(std::unique_ptr<DnsClient> dns_client);

  // HostResolver methods:
  std::unique_ptr<ResolveHostRequest> CreateRequest(
      const HostPortPair& host,
      const NetLogWithSource& net_log,
      const base::Optional<ResolveHostParameters>& optional_parameters)
      override;
  int Resolve(const RequestInfo& info,
              RequestPriority priority,
              AddressList* addresses,
              CompletionOnceCallback callback,
              std::unique_ptr<Request>* out_req,
              const NetLogWithSource& source_net_log) override;
  int ResolveFromCache(const RequestInfo& info,
                       AddressList* addresses,
                       const NetLogWithSource& source_net_log) override;
  int ResolveStaleFromCache(const RequestInfo& info,
                            AddressList* addresses,
                            HostCache::EntryStaleness* stale_info,
                            const NetLogWithSource& source_net_log) override;
  void SetDnsClientEnabled(bool enabled) override;

  HostCache* GetHostCache() override;
  bool HasCached(base::StringPiece hostname,
                 HostCache::Entry::Source* source_out,
                 HostCache::EntryStaleness* stale_out) const override;

  std::unique_ptr<base::Value> GetDnsConfigAsValue() const override;

  // Returns the number of host cache entries that were restored, or 0 if there
  // is no cache.
  size_t LastRestoredCacheSize() const;
  // Returns the number of entries in the host cache, or 0 if there is no cache.
  size_t CacheSize() const;

  void SetNoIPv6OnWifi(bool no_ipv6_on_wifi) override;
  bool GetNoIPv6OnWifi() override;

  void SetDnsConfigOverrides(const DnsConfigOverrides& overrides) override;

  void SetRequestContext(URLRequestContext* request_context) override;
  const std::vector<DnsConfig::DnsOverHttpsServerConfig>*
  GetDnsOverHttpsServersForTesting() const override;

  void set_proc_params_for_test(const ProcTaskParams& proc_params) {
    proc_params_ = proc_params;
  }

  void SetTickClockForTesting(const base::TickClock* tick_clock);

  // Configures maximum number of Jobs in the queue. Exposed for testing.
  // Only allowed when the queue is empty.
  void SetMaxQueuedJobsForTesting(size_t value);

  void SetMdnsSocketFactoryForTesting(
      std::unique_ptr<MDnsSocketFactory> socket_factory);
  void SetMdnsClientForTesting(std::unique_ptr<MDnsClient> client);

  void SetBaseDnsConfigForTesting(const DnsConfig& base_config);

 protected:
  // Callback from HaveOnlyLoopbackAddresses probe.
  void SetHaveOnlyLoopbackAddresses(bool result);

  // Sets the task runner used for HostResolverProc tasks.
  void SetTaskRunnerForTesting(scoped_refptr<base::TaskRunner> task_runner);

 private:
  friend class HostResolverImplTest;
  FRIEND_TEST_ALL_PREFIXES(HostResolverImplDnsTest, ModeForHistogram);
  class Job;
  class ProcTask;
  class LoopbackProbeJob;
  class DnsTask;
  class RequestImpl;
  class LegacyRequestImpl;
  using Key = HostCache::Key;
  using JobMap = std::map<Key, std::unique_ptr<Job>>;

  // Current resolver mode, useful for breaking down histograms.
  enum ModeForHistogram {
    // Using the system (i.e. O/S's) resolver.
    MODE_FOR_HISTOGRAM_SYSTEM,
    // Using the system resolver, which is in turn using private DNS.
    MODE_FOR_HISTOGRAM_SYSTEM_PRIVATE_DNS,
    // Using the system resolver, which is using DNS servers which offer
    // DNS-over-HTTPS service.
    MODE_FOR_HISTOGRAM_SYSTEM_SUPPORTS_DOH,
    // Using Chromium DNS resolver.
    MODE_FOR_HISTOGRAM_ASYNC_DNS,
    // Using Chromium DNS resolver which is using DNS servers which offer
    // DNS-over-HTTPS service.
    MODE_FOR_HISTOGRAM_ASYNC_DNS_PRIVATE_SUPPORTS_DOH,
  };

  // Number of consecutive failures of DnsTask (with successful fallback to
  // ProcTask) before the DnsClient is disabled until the next DNS change.
  static const unsigned kMaximumDnsFailures;

  // Attempts host resolution for |request|. Generally only expected to be
  // called from RequestImpl::Start().
  int Resolve(RequestImpl* request);

  // Attempts host resolution using fast local sources: IP literal resolution,
  // cache lookup, HOSTS lookup (if enabled), and localhost. Returns results
  // with error() OK if successful, ERR_NAME_NOT_RESOLVED if input is invalid,
  // or ERR_DNS_CACHE_MISS if the host could not be resolved using local
  // sources.
  //
  // On ERR_DNS_CACHE_MISS and OK, the cache key for the request is written to
  // |key|. On other errors, it may not be.
  //
  // If |allow_stale| is true, then stale cache entries can be returned.
  // |stale_info| must be non-null, and will be filled in with details of the
  // entry's staleness (if an entry is returned).
  //
  // If |allow_stale| is false, then stale cache entries will not be returned,
  // and |stale_info| must be null.
  HostCache::Entry ResolveLocally(const std::string& hostname,
                                  DnsQueryType requested_address_family,
                                  HostResolverSource source,
                                  HostResolverFlags flags,
                                  bool allow_cache,
                                  bool allow_stale,
                                  HostCache::EntryStaleness* stale_info,
                                  const NetLogWithSource& request_net_log,
                                  Key* out_key);

  // Attempts to create and start a Job to asynchronously attempt to resolve
  // |key|. On success, returns ERR_IO_PENDING and attaches the Job to
  // |request|. On error, marks |request| completed and returns the error.
  int CreateAndStartJob(const Key& key, RequestImpl* request);

  // Tries to resolve |key| and its possible IP address representation,
  // |ip_address|. Returns a results entry iff the input can be resolved.
  base::Optional<HostCache::Entry> ResolveAsIP(const Key& key,
                                               const IPAddress* ip_address);

  // Returns the result iff a positive match is found for |key| in the cache.
  //
  // If |allow_stale| is true, then stale cache entries can be returned.
  // |stale_info| must be non-null, and will be filled in with details of the
  // entry's staleness (if an entry is returned).
  //
  // If |allow_stale| is false, then stale cache entries will not be returned,
  // and |stale_info| must be null.
  base::Optional<HostCache::Entry> ServeFromCache(
      const Key& key,
      bool allow_stale,
      HostCache::EntryStaleness* stale_info);

  // Iff we have a DnsClient with a valid DnsConfig, and |key| can be resolved
  // from the HOSTS file, return the results.
  base::Optional<HostCache::Entry> ServeFromHosts(const Key& key);

  // Iff |key| is for a localhost name (RFC 6761) and address DNS query type,
  // returns a results entry with the loopback IP.
  base::Optional<HostCache::Entry> ServeLocalhost(const Key& key);

  // Returns the (hostname, address_family) key to use for |info|, choosing an
  // "effective" address family by inheriting the resolver's default address
  // family when the request leaves it unspecified.
  Key GetEffectiveKeyForRequest(const std::string& hostname,
                                DnsQueryType dns_query_type,
                                HostResolverSource source,
                                HostResolverFlags flags,
                                const IPAddress* ip_address,
                                const NetLogWithSource& net_log);

  // Probes IPv6 support and returns true if IPv6 support is enabled.
  // Results are cached, i.e. when called repeatedly this method returns result
  // from the first probe for some time before probing again.
  bool IsIPv6Reachable(const NetLogWithSource& net_log);

  // Attempts to connect a UDP socket to |dest|:53. Virtual for testing.
  virtual bool IsGloballyReachable(const IPAddress& dest,
                                   const NetLogWithSource& net_log);

  // Asynchronously checks if only loopback IPs are available.
  virtual void RunLoopbackProbeJob();

  // Records the result in cache if cache is present.
  void CacheResult(const Key& key,
                   const HostCache::Entry& entry,
                   base::TimeDelta ttl);

  // Record time from Request creation until a valid DNS response.
  void RecordTotalTime(bool speculative,
                       bool from_cache,
                       base::TimeDelta duration) const;

  // Removes |job| from |jobs_| and return, only if it exists.
  std::unique_ptr<Job> RemoveJob(Job* job);

  // Aborts all in progress jobs with ERR_NETWORK_CHANGED and notifies their
  // requests. Might start new jobs.
  void AbortAllInProgressJobs();

  // Aborts all in progress DnsTasks. In-progress jobs will fall back to
  // ProcTasks if able and otherwise abort with |error|. Might start new jobs,
  // if any jobs were taking up two dispatcher slots.
  //
  // If |fallback_only|, tasks will only abort if they can fallback to ProcTask.
  void AbortDnsTasks(int error, bool fallback_only);

  // Attempts to serve each Job in |jobs_| from the HOSTS file if we have
  // a DnsClient with a valid DnsConfig.
  void TryServingAllJobsFromHosts();

  // NetworkChangeNotifier::IPAddressObserver:
  void OnIPAddressChanged() override;

  // NetworkChangeNotifier::ConnectionTypeObserver:
  void OnConnectionTypeChanged(
      NetworkChangeNotifier::ConnectionType type) override;

  // NetworkChangeNotifier::DNSObserver:
  void OnDNSChanged() override;
  void OnInitialDNSConfigRead() override;

  // Returns DNS configuration including applying overrides. |log_to_net_log|
  // indicates whether the config should be logged to the netlog.
  DnsConfig GetBaseDnsConfig(bool log_to_net_log);
  void UpdateDNSConfig(bool config_changed);

  // True if have a DnsClient with a valid DnsConfig.
  bool HaveDnsConfig() const;

  // Called on successful DnsTask resolve.
  void OnDnsTaskResolve();
  // Called on successful resolve after falling back to ProcTask after a failed
  // DnsTask resolve.
  void OnFallbackResolve(int dns_task_error);

  MDnsClient* GetOrCreateMdnsClient();

  // Allows the tests to catch slots leaking out of the dispatcher.  One
  // HostResolverImpl::Job could occupy multiple PrioritizedDispatcher job
  // slots.
  size_t num_running_dispatcher_jobs_for_tests() const {
    return dispatcher_->num_running_jobs();
  }

  // Update |mode_for_histogram_|. Called when DNS config changes. |dns_config|
  // is the current DNS config and is only used if !HaveDnsConfig().
  void UpdateModeForHistogram(const DnsConfig& dns_config);

  // Cache of host resolution results.
  std::unique_ptr<HostCache> cache_;

  // Used for multicast DNS tasks. Created on first use using
  // GetOrCreateMndsClient().
  std::unique_ptr<MDnsSocketFactory> mdns_socket_factory_;
  std::unique_ptr<MDnsClient> mdns_client_;

  // Map from HostCache::Key to a Job.
  JobMap jobs_;

  // Starts Jobs according to their priority and the configured limits.
  std::unique_ptr<PrioritizedDispatcher> dispatcher_;

  // Limit on the maximum number of jobs queued in |dispatcher_|.
  size_t max_queued_jobs_;

  // Parameters for ProcTask.
  ProcTaskParams proc_params_;

  NetLog* net_log_;

  // If present, used by DnsTask and ServeFromHosts to resolve requests.
  std::unique_ptr<DnsClient> dns_client_;

  // True if received valid config from |dns_config_service_|. Temporary, used
  // to measure performance of DnsConfigService: http://crbug.com/125599
  bool received_dns_config_;

  // If set, used instead of getting DNS configuration from
  // NetworkChangeNotifier. Changes sent from NetworkChangeNotifier will also be
  // ignored and not cancel any pending requests.
  base::Optional<DnsConfig> test_base_config_;

  // Overrides or adds to DNS configuration read from the system for DnsClient
  // resolution.
  DnsConfigOverrides dns_config_overrides_;

  // Number of consecutive failures of DnsTask, counted when fallback succeeds.
  unsigned num_dns_failures_;

  // True if IPv6 should not be attempted when on a WiFi connection. See
  // https://crbug.com/696569 for further context.
  bool assume_ipv6_failure_on_wifi_;

  // True if DnsConfigService detected that system configuration depends on
  // local IPv6 connectivity. Disables probing.
  bool use_local_ipv6_;

  base::TimeTicks last_ipv6_probe_time_;
  bool last_ipv6_probe_result_;

  // Any resolver flags that should be added to a request by default.
  HostResolverFlags additional_resolver_flags_;

  // |true| if requests that would otherwise be handled via DnsTask should
  // instead use ProcTask when able.  Used in cases where there have been
  // multiple failures in DnsTask that succeeded in ProcTask, leading to the
  // conclusion that the resolver has a bad DNS configuration.
  bool use_proctask_by_default_;

  // Allow fallback to ProcTask if DnsTask fails.
  bool allow_fallback_to_proctask_;

  // Task runner used for DNS lookups using the system resolver. Normally a
  // TaskScheduler task runner, but can be overridden for tests.
  scoped_refptr<base::TaskRunner> proc_task_runner_;

  // Current resolver mode, useful for breaking down histogram data.
  ModeForHistogram mode_for_histogram_;

  URLRequestContext* url_request_context_;

  // Shared tick clock, overridden for testing.
  const base::TickClock* tick_clock_;

  THREAD_CHECKER(thread_checker_);

  base::WeakPtrFactory<HostResolverImpl> weak_ptr_factory_;

  base::WeakPtrFactory<HostResolverImpl> probe_weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(HostResolverImpl);
};

// Resolves a local hostname (such as "localhost" or "localhost6") into
// IP endpoints (with port 0). Returns true if |host| is a local
// hostname and false otherwise. Special IPv6 names (e.g. "localhost6")
// will resolve to an IPv6 address only, whereas other names will
// resolve to both IPv4 and IPv6.
// This function is only exposed so it can be unit-tested.
// TODO(tfarina): It would be better to change the tests so this function
// gets exercised indirectly through HostResolverImpl.
NET_EXPORT_PRIVATE bool ResolveLocalHostname(base::StringPiece host,
                                             AddressList* address_list);

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_IMPL_H_
