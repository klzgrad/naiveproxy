// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_mojo.h"

#include <memory>
#include <utility>

#include "base/callback_helpers.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"

namespace net {
namespace {

// Default TTL for successful host resolutions.
const int kCacheEntryTTLSeconds = 5;

// Default TTL for unsuccessful host resolutions.
const int kNegativeCacheEntryTTLSeconds = 0;

HostCache::Key CacheKeyForRequest(const HostResolver::RequestInfo& info) {
  return HostCache::Key(info.hostname(), info.address_family(),
                        info.host_resolver_flags());
}

}  // namespace

class HostResolverMojo::Job : public interfaces::HostResolverRequestClient {
 public:
  Job(const HostCache::Key& key,
      AddressList* addresses,
      const CompletionCallback& callback,
      mojo::InterfaceRequest<interfaces::HostResolverRequestClient> request,
      base::WeakPtr<HostCache> host_cache);

 private:
  // interfaces::HostResolverRequestClient override.
  void ReportResult(int32_t error, const AddressList& address_list) override;

  // Mojo error handler.
  void OnConnectionError();

  const HostCache::Key key_;
  AddressList* addresses_;
  CompletionCallback callback_;
  mojo::Binding<interfaces::HostResolverRequestClient> binding_;
  base::WeakPtr<HostCache> host_cache_;
};

class HostResolverMojo::RequestImpl : public HostResolver::Request {
 public:
  explicit RequestImpl(std::unique_ptr<Job> job) : job_(std::move(job)) {}

  ~RequestImpl() override = default;

  void ChangeRequestPriority(RequestPriority priority) override {}

 private:
  std::unique_ptr<Job> job_;
};

HostResolverMojo::HostResolverMojo(Impl* impl)
    : impl_(impl),
      host_cache_(HostCache::CreateDefaultCache()),
      host_cache_weak_factory_(host_cache_.get()) {
}

HostResolverMojo::~HostResolverMojo() = default;

int HostResolverMojo::Resolve(const RequestInfo& info,
                              RequestPriority priority,
                              AddressList* addresses,
                              const CompletionCallback& callback,
                              std::unique_ptr<Request>* request,
                              const NetLogWithSource& source_net_log) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(request);
  DVLOG(1) << "Resolve " << info.host_port_pair().ToString();

  HostCache::Key key = CacheKeyForRequest(info);
  int cached_result = ResolveFromCacheInternal(info, key, addresses);
  if (cached_result != ERR_DNS_CACHE_MISS) {
    DVLOG(1) << "Resolved " << info.host_port_pair().ToString()
             << " from cache";
    return cached_result;
  }

  interfaces::HostResolverRequestClientPtr handle;
  std::unique_ptr<Job> job(new Job(key, addresses, callback,
                                   mojo::MakeRequest(&handle),
                                   host_cache_weak_factory_.GetWeakPtr()));
  request->reset(new RequestImpl(std::move(job)));

  impl_->ResolveDns(std::make_unique<HostResolver::RequestInfo>(info),
                    std::move(handle));
  return ERR_IO_PENDING;
}

int HostResolverMojo::ResolveFromCache(const RequestInfo& info,
                                       AddressList* addresses,
                                       const NetLogWithSource& source_net_log) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "ResolveFromCache " << info.host_port_pair().ToString();
  return ResolveFromCacheInternal(info, CacheKeyForRequest(info), addresses);
}

int HostResolverMojo::ResolveStaleFromCache(
    const RequestInfo& info,
    AddressList* addresses,
    HostCache::EntryStaleness* stale_info,
    const NetLogWithSource& net_log) {
  NOTREACHED();
  return ERR_UNEXPECTED;
}

HostCache* HostResolverMojo::GetHostCache() {
  return host_cache_.get();
}

bool HostResolverMojo::HasCached(base::StringPiece hostname,
                                 HostCache::Entry::Source* source_out,
                                 HostCache::EntryStaleness* stale_out) const {
  if (!host_cache_)
    return false;

  return host_cache_->HasEntry(hostname, source_out, stale_out);
}

int HostResolverMojo::ResolveFromCacheInternal(const RequestInfo& info,
                                               const HostCache::Key& key,
                                               AddressList* addresses) {
  if (!info.allow_cached_response())
    return ERR_DNS_CACHE_MISS;

  const HostCache::Entry* entry =
      host_cache_->Lookup(key, base::TimeTicks::Now());
  if (!entry)
    return ERR_DNS_CACHE_MISS;

  *addresses = AddressList::CopyWithPort(entry->addresses(), info.port());
  return entry->error();
}

HostResolverMojo::Job::Job(
    const HostCache::Key& key,
    AddressList* addresses,
    const CompletionCallback& callback,
    mojo::InterfaceRequest<interfaces::HostResolverRequestClient> request,
    base::WeakPtr<HostCache> host_cache)
    : key_(key),
      addresses_(addresses),
      callback_(callback),
      binding_(this, std::move(request)),
      host_cache_(host_cache) {
  binding_.set_connection_error_handler(base::Bind(
      &HostResolverMojo::Job::OnConnectionError, base::Unretained(this)));
}

void HostResolverMojo::Job::ReportResult(int32_t error,
                                         const AddressList& address_list) {
  if (error == OK)
    *addresses_ = address_list;
  if (host_cache_) {
    base::TimeDelta ttl = base::TimeDelta::FromSeconds(
        error == OK ? kCacheEntryTTLSeconds : kNegativeCacheEntryTTLSeconds);
    HostCache::Entry entry(error, *addresses_, HostCache::Entry::SOURCE_UNKNOWN,
                           ttl);
    host_cache_->Set(key_, entry, base::TimeTicks::Now(), ttl);
  }
  if (binding_.is_bound())
    binding_.Close();
  base::ResetAndReturn(&callback_).Run(error);
}

void HostResolverMojo::Job::OnConnectionError() {
  ReportResult(ERR_FAILED, AddressList());
}

}  // namespace net
