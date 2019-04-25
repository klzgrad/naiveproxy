// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_H_
#define NET_DNS_HOST_RESOLVER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/optional.h"
#include "net/base/address_family.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/prioritized_dispatcher.h"
#include "net/base/request_priority.h"
#include "net/dns/dns_config.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver_source.h"
#include "net/dns/public/dns_query_type.h"

namespace base {
class Value;
}

namespace net {

class AddressList;
class ContextHostResolver;
class DnsClient;
struct DnsConfigOverrides;
class NetLog;
class NetLogWithSource;
class URLRequestContext;

// This class represents the task of resolving hostnames (or IP address
// literal) to an AddressList object (or other DNS-style results).
//
// Typically implemented by ContextHostResolver or wrappers thereof. See
// HostResolver::Create[...]() methods for construction or URLRequestContext for
// retrieval.
//
// See mock_host_resolver.h for test implementations.
class NET_EXPORT HostResolver {
 public:
  // Handler for an individual host resolution request. Created by
  // HostResolver::CreateRequest().
  class ResolveHostRequest {
   public:
    // Destruction cancels the request if running asynchronously, causing the
    // callback to never be invoked.
    virtual ~ResolveHostRequest() {}

    // Starts the request and returns a network error code.
    //
    // If the request could not be handled synchronously, returns
    // |ERR_IO_PENDING|, and completion will be signaled later via |callback|.
    // On any other returned value, the request was handled synchronously and
    // |callback| will not be invoked.
    //
    // Results in ERR_NAME_NOT_RESOLVED if the hostname is invalid, or if it is
    // an incompatible IP literal (e.g. IPv6 is disabled and it is an IPv6
    // literal).
    //
    // The parent HostResolver must still be alive when Start() is called,  but
    // if it is destroyed before an asynchronous result completes, the request
    // will be automatically cancelled.
    //
    // If cancelled before |callback| is invoked, it will never be invoked.
    virtual int Start(CompletionOnceCallback callback) = 0;

    // Address record (A or AAAA) results of the request. Should only be called
    // after Start() signals completion, either by invoking the callback or by
    // returning a result other than |ERR_IO_PENDING|.
    virtual const base::Optional<AddressList>& GetAddressResults() const = 0;

    // Text record (TXT) results of the request. Should only be called after
    // Start() signals completion, either by invoking the callback or by
    // returning a result other than |ERR_IO_PENDING|.
    virtual const base::Optional<std::vector<std::string>>& GetTextResults()
        const = 0;

    // Hostname record (SRV or PTR) results of the request. For SRV results,
    // hostnames are ordered acording to their priorities and weights. See RFC
    // 2782.
    //
    // Should only be called after Start() signals completion, either by
    // invoking the callback or by returning a result other than
    // |ERR_IO_PENDING|.
    virtual const base::Optional<std::vector<HostPortPair>>&
    GetHostnameResults() const = 0;

    // Information about the result's staleness in the host cache. Only
    // available if results were received from the host cache.
    //
    // Should only be called after Start() signals completion, either by
    // invoking the callback or by returning a result other than
    // |ERR_IO_PENDING|.
    virtual const base::Optional<HostCache::EntryStaleness>& GetStaleInfo()
        const = 0;

    // Changes the priority of the specified request. Can only be called while
    // the request is running (after Start() returns |ERR_IO_PENDING| and before
    // the callback is invoked).
    virtual void ChangeRequestPriority(RequestPriority priority) {}
  };

  // |max_concurrent_resolves| is how many resolve requests will be allowed to
  // run in parallel. Pass HostResolver::kDefaultParallelism to choose a
  // default value.
  // |max_retry_attempts| is the maximum number of times we will retry for host
  // resolution. Pass HostResolver::kDefaultRetryAttempts to choose a default
  // value.
  // |enable_caching| controls whether a HostCache is used.
  struct NET_EXPORT Options {
    Options();

    PrioritizedDispatcher::Limits GetDispatcherLimits() const;

    size_t max_concurrent_resolves;
    size_t max_retry_attempts;
    bool enable_caching;
  };

  // Factory class. Useful for classes that need to inject and override resolver
  // creation for tests.
  class NET_EXPORT Factory {
   public:
    virtual ~Factory() = default;

    // See HostResolver::CreateSystemResolver.
    virtual std::unique_ptr<HostResolver> CreateResolver(const Options& options,
                                                         NetLog* net_log);
  };

  // Parameter-grouping struct for additional optional parameters for
  // CreateRequest() calls. All fields are optional and have a reasonable
  // default.
  struct ResolveHostParameters {
    // Requested DNS query type. If UNSPECIFIED, resolver will pick A or AAAA
    // (or both) based on IPv4/IPv6 settings.
    DnsQueryType dns_query_type = DnsQueryType::UNSPECIFIED;

    // The initial net priority for the host resolution request.
    RequestPriority initial_priority = RequestPriority::DEFAULT_PRIORITY;

    // The source to use for resolved addresses. Default allows the resolver to
    // pick an appropriate source. Only affects use of big external sources (eg
    // calling the system for resolution or using DNS). Even if a source is
    // specified, results can still come from cache, resolving "localhost" or
    // IP literals, etc.
    HostResolverSource source = HostResolverSource::ANY;

    enum class CacheUsage {
      // Results may come from the host cache if non-stale.
      ALLOWED,

      // Results may come from the host cache even if stale (by expiration or
      // network changes).
      STALE_ALLOWED,

      // Results will not come from the host cache.
      DISALLOWED,
    };
    CacheUsage cache_usage = CacheUsage::ALLOWED;

    // If |true|, requests that the resolver include AddressList::canonical_name
    // in the results. If the resolver can do so without significant
    // performance impact, canonical_name may still be included even if
    // parameter is set to |false|.
    bool include_canonical_name = false;

    // Hint to the resolver that resolution is only being requested for loopback
    // hosts.
    bool loopback_only = false;

    // Set |true| iff the host resolve request is only being made speculatively
    // to fill the cache and the result addresses will not be used. The request
    // will receive special logging/observer treatment, and the result addresses
    // will always be |base::nullopt|.
    bool is_speculative = false;
  };

  // Handler for an ongoing MDNS listening operation. Created by
  // HostResolver::CreateMdnsListener().
  class MdnsListener {
   public:
    // Delegate type for result update notifications from MdnsListener. All
    // methods have a |result_type| field to allow a single delegate to be
    // passed to multiple MdnsListeners and be used to listen for updates for
    // multiple types for the same host.
    class Delegate {
     public:
      enum class UpdateType { ADDED, CHANGED, REMOVED };

      virtual ~Delegate() {}

      virtual void OnAddressResult(UpdateType update_type,
                                   DnsQueryType result_type,
                                   IPEndPoint address) = 0;
      virtual void OnTextResult(UpdateType update_type,
                                DnsQueryType result_type,
                                std::vector<std::string> text_records) = 0;
      virtual void OnHostnameResult(UpdateType update_type,
                                    DnsQueryType result_type,
                                    HostPortPair host) = 0;

      // For results which may be valid MDNS but are not handled/parsed by
      // HostResolver, e.g. pointers to the root domain.
      virtual void OnUnhandledResult(UpdateType update_type,
                                     DnsQueryType result_type) = 0;
    };

    // Destruction cancels the listening operation.
    virtual ~MdnsListener() {}

    // Begins the listening operation, invoking |delegate| whenever results are
    // updated. |delegate| will no longer be called once the listening operation
    // is cancelled (via destruction of |this|).
    virtual int Start(Delegate* delegate) = 0;
  };

  // Set Options.max_concurrent_resolves to this to select a default level
  // of concurrency.
  static const size_t kDefaultParallelism = 0;

  // Set Options.max_retry_attempts to this to select a default retry value.
  static const size_t kDefaultRetryAttempts = static_cast<size_t>(-1);

  // If any completion callbacks are pending when the resolver is destroyed,
  // the host resolutions are cancelled, and the completion callbacks will not
  // be called.
  virtual ~HostResolver();

  // Creates a request to resolve the given hostname (or IP address literal).
  // Profiling information for the request is saved to |net_log| if non-NULL.
  //
  // Additional parameters may be set using |optional_parameters|. Reasonable
  // defaults will be used if passed |base::nullopt|.
  virtual std::unique_ptr<ResolveHostRequest> CreateRequest(
      const HostPortPair& host,
      const NetLogWithSource& net_log,
      const base::Optional<ResolveHostParameters>& optional_parameters) = 0;

  // Create a listener to watch for updates to an MDNS result.
  virtual std::unique_ptr<MdnsListener> CreateMdnsListener(
      const HostPortPair& host,
      DnsQueryType query_type);

  // Enable or disable the built-in asynchronous DnsClient.
  virtual void SetDnsClientEnabled(bool enabled);

  // Returns the HostResolverCache |this| uses, or NULL if there isn't one.
  // Used primarily to clear the cache and for getting debug information.
  virtual HostCache* GetHostCache();

  // Checks whether this HostResolver has cached a resolution for the given
  // hostname (or IP address literal). If so, returns true and writes the source
  // of the resolution (e.g. DNS, HOSTS file, etc.) to |source_out|, the
  // staleness of the resolution to |stale_out|, and whether the result was
  // retrieved securely or not to |secure_out| (if they are not null). It tries
  // using two common address_family and host_resolver_flag combinations when
  // checking the cache; this means false negatives are possible, but unlikely.
  virtual bool HasCached(base::StringPiece hostname,
                         HostCache::Entry::Source* source_out,
                         HostCache::EntryStaleness* stale_out,
                         bool* secure_out) const = 0;

  // Returns the current DNS configuration |this| is using, as a Value, or
  // nullptr if it's configured to always use the system host resolver.
  virtual std::unique_ptr<base::Value> GetDnsConfigAsValue() const;

  // Sets the HostResolver to assume that IPv6 is unreachable when on a wifi
  // connection. See https://crbug.com/696569 for further context.
  virtual void SetNoIPv6OnWifi(bool no_ipv6_on_wifi);
  virtual bool GetNoIPv6OnWifi();

  // Sets overriding configuration that will replace or add to configuration
  // read from the system for DnsClient resolution.
  virtual void SetDnsConfigOverrides(const DnsConfigOverrides& overrides);

  // Sets the URLRequestContext to be used for underlying requests made at the
  // HTTP level (e.g. DNS over HTTPS requests).
  virtual void SetRequestContext(URLRequestContext* request_context) {}

  // Returns the currently configured DNS over HTTPS servers. Returns nullptr if
  // DNS over HTTPS is not enabled.
  virtual const std::vector<DnsConfig::DnsOverHttpsServerConfig>*
  GetDnsOverHttpsServersForTesting() const;

  // Creates a HostResolver implementation that queries the underlying system.
  // (Except if a unit-test has changed the global HostResolverProc using
  // ScopedHostResolverProc to intercept requests to the system).
  static std::unique_ptr<HostResolver> CreateSystemResolver(
      const Options& options,
      NetLog* net_log);
  // Same, but explicitly returns the implementing ContextHostResolver. Only
  // used by tests.
  static std::unique_ptr<ContextHostResolver> CreateSystemResolverImpl(
      const Options& options,
      NetLog* net_log);

  // As above, but uses default parameters.
  static std::unique_ptr<HostResolver> CreateDefaultResolver(NetLog* net_log);
  // Same, but explicitly returns the implementing ContextHostResolver. Only
  // used by tests and by StaleHostResolver in Cronet.
  static std::unique_ptr<ContextHostResolver> CreateDefaultResolverImpl(
      NetLog* net_log);

  // Helpers for interacting with HostCache and ProcResolver.
  static AddressFamily DnsQueryTypeToAddressFamily(DnsQueryType query_type);
  static HostResolverFlags ParametersToHostResolverFlags(
      const ResolveHostParameters& parameters);

 protected:
  HostResolver();

 private:
  DISALLOW_COPY_AND_ASSIGN(HostResolver);
};

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_H_
