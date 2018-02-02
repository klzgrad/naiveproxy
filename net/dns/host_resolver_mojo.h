// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_MOJO_H_
#define NET_DNS_HOST_RESOLVER_MOJO_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver.h"
#include "net/interfaces/host_resolver_service.mojom.h"

namespace net {
class AddressList;
class NetLogWithSource;

// A HostResolver implementation that converts requests to mojo types and
// forwards them to a mojo Impl interface.
class HostResolverMojo : public HostResolver {
 public:
  class Impl {
   public:
    virtual ~Impl() = default;
    virtual void ResolveDns(
        std::unique_ptr<HostResolver::RequestInfo> request_info,
        interfaces::HostResolverRequestClientPtr) = 0;
  };

  // |impl| must outlive |this|.
  explicit HostResolverMojo(Impl* impl);
  ~HostResolverMojo() override;

  // HostResolver overrides.
  // Note: |Resolve()| currently ignores |priority|.
  int Resolve(const RequestInfo& info,
              RequestPriority priority,
              AddressList* addresses,
              const CompletionCallback& callback,
              std::unique_ptr<Request>* request,
              const NetLogWithSource& source_net_log) override;
  int ResolveFromCache(const RequestInfo& info,
                       AddressList* addresses,
                       const NetLogWithSource& source_net_log) override;
  HostCache* GetHostCache() override;

 private:
  class Job;
  class RequestImpl;

  int ResolveFromCacheInternal(const RequestInfo& info,
                               const HostCache::Key& key,
                               AddressList* addresses);

  Impl* const impl_;

  std::unique_ptr<HostCache> host_cache_;
  base::WeakPtrFactory<HostCache> host_cache_weak_factory_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(HostResolverMojo);
};

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_MOJO_H_
