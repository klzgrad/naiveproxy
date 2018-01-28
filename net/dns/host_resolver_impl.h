// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_IMPL_H_
#define NET_DNS_HOST_RESOLVER_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_piece.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_proc.h"

namespace net {

class AddressList;
class DnsClient;
class IPAddress;
class NetLog;
class NetLogWithSource;

// For each hostname that is requested, HostResolver creates a
// HostResolverImpl::Job. When this job gets dispatched it creates a ProcTask
// which runs the given HostResolverProc in TaskScheduler. If requests for that
// same host are made during the job's lifetime, they are attached to the
// existing job rather than creating a new one. This avoids doing parallel
// resolves for the same host.
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

  // Configures maximum number of Jobs in the queue. Exposed for testing.
  // Only allowed when the queue is empty.
  void SetMaxQueuedJobs(size_t value);

  // Set the DnsClient to be used for resolution. In case of failure, the
  // HostResolverProc from ProcTaskParams will be queried. If the DnsClient is
  // not pre-configured with a valid DnsConfig, a new config is fetched from
  // NetworkChangeNotifier.
  void SetDnsClient(std::unique_ptr<DnsClient> dns_client);

  // HostResolver methods:
  int Resolve(const RequestInfo& info,
              RequestPriority priority,
              AddressList* addresses,
              const CompletionCallback& callback,
              std::unique_ptr<Request>* out_req,
              const NetLogWithSource& source_net_log) override;
  int ResolveFromCache(const RequestInfo& info,
                       AddressList* addresses,
                       const NetLogWithSource& source_net_log) override;
  void SetDnsClientEnabled(bool enabled) override;
  HostCache* GetHostCache() override;
  std::unique_ptr<base::Value> GetDnsConfigAsValue() const override;

  // Like |ResolveFromCache()|, but can return a stale result if the
  // implementation supports it. Fills in |*stale_info| if a response is
  // returned to indicate how stale (or not) it is.
  int ResolveStaleFromCache(const RequestInfo& info,
                            AddressList* addresses,
                            HostCache::EntryStaleness* stale_info,
                            const NetLogWithSource& source_net_log);
  // Returns the number of host cache entries that were restored, or 0 if there
  // is no cache.
  size_t LastRestoredCacheSize() const;
  // Returns the number of entries in the host cache, or 0 if there is no cache.
  size_t CacheSize() const;

  void InitializePersistence(
      const PersistCallback& persist_callback,
      std::unique_ptr<const base::Value> old_data) override;

  void SetNoIPv6OnWifi(bool no_ipv6_on_wifi) override;
  bool GetNoIPv6OnWifi() override;

  void set_proc_params_for_test(const ProcTaskParams& proc_params) {
    proc_params_ = proc_params;
  }

 protected:
  // Callback from HaveOnlyLoopbackAddresses probe.
  void SetHaveOnlyLoopbackAddresses(bool result);

  // Sets the task runner used for HostResolverProc tasks.
  void SetTaskRunnerForTesting(scoped_refptr<base::TaskRunner> task_runner);

 private:
  friend class HostResolverImplTest;
  class Job;
  class ProcTask;
  class LoopbackProbeJob;
  class DnsTask;
  class RequestImpl;
  using Key = HostCache::Key;
  using JobMap = std::map<Key, std::unique_ptr<Job>>;

  // Number of consecutive failures of DnsTask (with successful fallback to
  // ProcTask) before the DnsClient is disabled until the next DNS change.
  static const unsigned kMaximumDnsFailures;

  // Helper used by |Resolve()| and |ResolveFromCache()|.  Performs IP
  // literal, cache and HOSTS lookup (if enabled), returns OK if successful,
  // ERR_NAME_NOT_RESOLVED if either hostname is invalid or IP literal is
  // incompatible, ERR_DNS_CACHE_MISS if entry was not found in cache and
  // HOSTS and is not localhost.
  //
  // On success, the resulting addresses are written to |addresses|.
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
  int ResolveHelper(const RequestInfo& info,
                    bool allow_stale,
                    HostCache::EntryStaleness* stale_info,
                    const NetLogWithSource& request_net_log,
                    AddressList* addresses,
                    Key* key);

  // Tries to resolve |key| as an IP, returns true and sets |net_error| if
  // succeeds, returns false otherwise.
  bool ResolveAsIP(const Key& key,
                   const RequestInfo& info,
                   const IPAddress* ip_address,
                   int* net_error,
                   AddressList* addresses);

  // If |key| is not found in cache returns false, otherwise returns
  // true, sets |net_error| to the cached error code and fills |addresses|
  // if it is a positive entry.
  //
  // If |allow_stale| is true, then stale cache entries can be returned.
  // |stale_info| must be non-null, and will be filled in with details of the
  // entry's staleness (if an entry is returned).
  //
  // If |allow_stale| is false, then stale cache entries will not be returned,
  // and |stale_info| must be null.
  bool ServeFromCache(const Key& key,
                      const RequestInfo& info,
                      int* net_error,
                      AddressList* addresses,
                      bool allow_stale,
                      HostCache::EntryStaleness* stale_info);

  // If we have a DnsClient with a valid DnsConfig, and |key| is found in the
  // HOSTS file, returns true and fills |addresses|. Otherwise returns false.
  bool ServeFromHosts(const Key& key,
                      const RequestInfo& info,
                      AddressList* addresses);

  // If |key| is for a localhost name (RFC 6761), returns true and fills
  // |addresses| with the loopback IP. Otherwise returns false.
  bool ServeLocalhost(const Key& key,
                      const RequestInfo& info,
                      AddressList* addresses);

  // Returns the (hostname, address_family) key to use for |info|, choosing an
  // "effective" address family by inheriting the resolver's default address
  // family when the request leaves it unspecified.
  Key GetEffectiveKeyForRequest(const RequestInfo& info,
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

  // Removes |job| from |jobs_|, only if it exists, but does not delete it.
  void RemoveJob(Job* job);

  // Aborts all in progress jobs with ERR_NETWORK_CHANGED and notifies their
  // requests. Might start new jobs.
  void AbortAllInProgressJobs();

  // Aborts all in progress DnsTasks. In-progress jobs will fall back to
  // ProcTasks. Might start new jobs, if any jobs were taking up two dispatcher
  // slots.
  void AbortDnsTasks();

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

  void UpdateDNSConfig(bool config_changed);

  // True if have a DnsClient with a valid DnsConfig.
  bool HaveDnsConfig() const;

  // Called when a host name is successfully resolved and DnsTask was run on it
  // and resulted in |net_error|.
  void OnDnsTaskResolve(int net_error);

  void ApplyPersistentData(std::unique_ptr<const base::Value>);
  std::unique_ptr<const base::Value> GetPersistentData();

  void SchedulePersist();
  void DoPersist();

  // Allows the tests to catch slots leaking out of the dispatcher.  One
  // HostResolverImpl::Job could occupy multiple PrioritizedDispatcher job
  // slots.
  size_t num_running_dispatcher_jobs_for_tests() const {
    return dispatcher_->num_running_jobs();
  }

  // Cache of host resolution results.
  std::unique_ptr<HostCache> cache_;

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

  // Allow fallback to ProcTask if DnsTask fails.
  bool fallback_to_proctask_;

  // Task runner used for DNS lookups using the system resolver. Normally a
  // TaskScheduler task runner, but can be overridden for tests.
  scoped_refptr<base::TaskRunner> proc_task_runner_;

  bool persist_initialized_;
  PersistCallback persist_callback_;
  base::OneShotTimer persist_timer_;

  THREAD_CHECKER(thread_checker_);

  base::WeakPtrFactory<HostResolverImpl> weak_ptr_factory_;

  base::WeakPtrFactory<HostResolverImpl> probe_weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(HostResolverImpl);
};

// Resolves a local hostname (such as "localhost" or "localhost6") into
// IP endpoints with the given port. Returns true if |host| is a local
// hostname and false otherwise. Special IPv6 names (e.g. "localhost6")
// will resolve to an IPv6 address only, whereas other names will
// resolve to both IPv4 and IPv6.
// This function is only exposed so it can be unit-tested.
// TODO(tfarina): It would be better to change the tests so this function
// gets exercised indirectly through HostResolverImpl.
NET_EXPORT_PRIVATE bool ResolveLocalHostname(base::StringPiece host,
                                             uint16_t port,
                                             AddressList* address_list);

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_IMPL_H_
