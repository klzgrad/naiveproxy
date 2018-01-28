// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_H_
#define NET_DNS_HOST_RESOLVER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "base/macros.h"
#include "net/base/address_family.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/base/prioritized_dispatcher.h"
#include "net/base/request_priority.h"
#include "net/dns/host_cache.h"

namespace base {
class Value;
}

namespace net {

class AddressList;
class HostResolverImpl;
class HostResolverProc;
class NetLog;
class NetLogWithSource;

// This class represents the task of resolving hostnames (or IP address
// literal) to an AddressList object.
//
// HostResolver can handle multiple requests at a time, so when cancelling a
// request the RequestHandle that was returned by Resolve() needs to be
// given.  A simpler alternative for consumers that only have 1 outstanding
// request at a time is to create a SingleRequestHostResolver wrapper around
// HostResolver (which will automatically cancel the single request when it
// goes out of scope).
class NET_EXPORT HostResolver {
 public:
  // HostResolver::Request class is used to cancel the request and change it's
  // priority. It must be owned by consumer. Deletion cancels the request.
  class Request {
   public:
    virtual ~Request() {}

    // Changes the priority of the specified request. Can be called after
    // Resolve() is called. Can't be called once the request is cancelled or
    // completed.
    virtual void ChangeRequestPriority(RequestPriority priority) = 0;
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

  // The parameters for doing a Resolve(). A hostname and port are
  // required; the rest are optional (and have reasonable defaults).
  class NET_EXPORT RequestInfo {
   public:
    explicit RequestInfo(const HostPortPair& host_port_pair);
    RequestInfo(const RequestInfo& request_info);
    ~RequestInfo();

    const HostPortPair& host_port_pair() const { return host_port_pair_; }
    void set_host_port_pair(const HostPortPair& host_port_pair) {
      host_port_pair_ = host_port_pair;
    }

    uint16_t port() const { return host_port_pair_.port(); }
    const std::string& hostname() const { return host_port_pair_.host(); }

    AddressFamily address_family() const { return address_family_; }
    void set_address_family(AddressFamily address_family) {
      address_family_ = address_family;
    }

    HostResolverFlags host_resolver_flags() const {
      return host_resolver_flags_;
    }
    void set_host_resolver_flags(HostResolverFlags host_resolver_flags) {
      host_resolver_flags_ = host_resolver_flags;
    }

    bool allow_cached_response() const { return allow_cached_response_; }
    void set_allow_cached_response(bool b) { allow_cached_response_ = b; }

    bool is_speculative() const { return is_speculative_; }
    void set_is_speculative(bool b) { is_speculative_ = b; }

    bool is_my_ip_address() const { return is_my_ip_address_; }
    void set_is_my_ip_address(bool b) { is_my_ip_address_ = b; }

   private:
    RequestInfo();

    // The hostname to resolve, and the port to use in resulting sockaddrs.
    HostPortPair host_port_pair_;

    // The address family to restrict results to.
    AddressFamily address_family_;

    // Flags to use when resolving this request.
    HostResolverFlags host_resolver_flags_;

    // Whether it is ok to return a result from the host cache.
    bool allow_cached_response_;

    // Whether this request was started by the DNS prefetcher.
    bool is_speculative_;

    // Indicates a request for myIpAddress (to differentiate from other requests
    // for localhost, currently used by Chrome OS).
    bool is_my_ip_address_;
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

  // Resolves the given hostname (or IP address literal), filling out the
  // |addresses| object upon success.  The |info.port| parameter will be set as
  // the sin(6)_port field of the sockaddr_in{6} struct.  Returns OK if
  // successful or an error code upon failure.  Returns
  // ERR_NAME_NOT_RESOLVED if hostname is invalid, or if it is an
  // incompatible IP literal (e.g. IPv6 is disabled and it is an IPv6
  // literal).
  //
  // If the operation cannot be completed synchronously, ERR_IO_PENDING will
  // be returned and the real result code will be passed to the completion
  // callback.  Otherwise the result code is returned immediately from this
  // call.
  //
  // [out_req] must be owned by a caller. If the request is not completed
  // synchronously, it will be filled with a handle to the request. It must be
  // completed before the HostResolver itself is destroyed.
  //
  // Requests can be cancelled any time by deletion of the [out_req]. Deleting
  // |out_req| will cancel the request, and cause |callback| not to be invoked.
  //
  // Profiling information for the request is saved to |net_log| if non-NULL.
  virtual int Resolve(const RequestInfo& info,
                      RequestPriority priority,
                      AddressList* addresses,
                      const CompletionCallback& callback,
                      std::unique_ptr<Request>* out_req,
                      const NetLogWithSource& net_log) = 0;

  // Resolves the given hostname (or IP address literal) out of cache or HOSTS
  // file (if enabled) only. This is guaranteed to complete synchronously.
  // This acts like |Resolve()| if the hostname is IP literal, or cached value
  // or HOSTS entry exists. Otherwise, ERR_DNS_CACHE_MISS is returned.
  virtual int ResolveFromCache(const RequestInfo& info,
                               AddressList* addresses,
                               const NetLogWithSource& net_log) = 0;

  // Enable or disable the built-in asynchronous DnsClient.
  virtual void SetDnsClientEnabled(bool enabled);

  // Returns the HostResolverCache |this| uses, or NULL if there isn't one.
  // Used primarily to clear the cache and for getting debug information.
  virtual HostCache* GetHostCache();

  // Returns the current DNS configuration |this| is using, as a Value, or
  // nullptr if it's configured to always use the system host resolver.
  virtual std::unique_ptr<base::Value> GetDnsConfigAsValue() const;

  typedef base::Callback<void(std::unique_ptr<const base::Value>)>
      PersistCallback;
  // Configures the HostResolver to be able to persist data (e.g. observed
  // performance) between sessions. |persist_callback| is a callback that will
  // be called when the HostResolver wants to persist data; |old_data| is the
  // data last persisted by the resolver on the previous session.
  virtual void InitializePersistence(
      const PersistCallback& persist_callback,
      std::unique_ptr<const base::Value> old_data);

  // Sets the HostResolver to assume that IPv6 is unreachable when on a wifi
  // connection. See https://crbug.com/696569 for further context.
  virtual void SetNoIPv6OnWifi(bool no_ipv6_on_wifi);
  virtual bool GetNoIPv6OnWifi();

  // Creates a HostResolver implementation that queries the underlying system.
  // (Except if a unit-test has changed the global HostResolverProc using
  // ScopedHostResolverProc to intercept requests to the system).
  static std::unique_ptr<HostResolver> CreateSystemResolver(
      const Options& options,
      NetLog* net_log);
  // Same, but explicitly returns the HostResolverImpl. Only used by
  // StaleHostResolver in cronet.
  static std::unique_ptr<HostResolverImpl> CreateSystemResolverImpl(
      const Options& options,
      NetLog* net_log);

  // As above, but uses default parameters.
  static std::unique_ptr<HostResolver> CreateDefaultResolver(NetLog* net_log);
  // Same, but explicitly returns the HostResolverImpl. Only used by
  // StaleHostResolver in cronet.
  static std::unique_ptr<HostResolverImpl> CreateDefaultResolverImpl(
      NetLog* net_log);

 protected:
  HostResolver();

 private:
  DISALLOW_COPY_AND_ASSIGN(HostResolver);
};

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_H_
