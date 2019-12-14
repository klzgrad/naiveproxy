// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/context_host_resolver.h"

#include <string>
#include <utility>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_piece.h"
#include "base/time/tick_clock.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_config.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver_manager.h"
#include "net/dns/host_resolver_proc.h"
#include "net/url_request/url_request_context.h"

namespace net {

// Wrapper of ResolveHostRequests that on destruction will remove itself from
// |ContextHostResolver::handed_out_requests_|.
class ContextHostResolver::WrappedRequest
    : public HostResolver::ResolveHostRequest {
 public:
  WrappedRequest(
      std::unique_ptr<HostResolverManager::CancellableRequest> inner_request,
      ContextHostResolver* resolver,
      bool shutting_down)
      : inner_request_(std::move(inner_request)),
        resolver_(resolver),
        shutting_down_(shutting_down) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(resolver_->sequence_checker_);
  }

  ~WrappedRequest() override { Cancel(); }

  void Cancel() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    OnShutdown();

    if (resolver_) {
      DCHECK_EQ(1u, resolver_->handed_out_requests_.count(this));
      resolver_->handed_out_requests_.erase(this);
      resolver_ = nullptr;
    }
  }

  void OnShutdown() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // Cannot destroy |inner_request_| because it is still allowed to call
    // Get...Results() methods if the request was already complete.
    if (inner_request_)
      inner_request_->Cancel();

    shutting_down_ = true;

    // Not clearing |resolver_| so that early shutdown can be differentiated in
    // Start() from full cancellation on resolver destruction.
  }

  int Start(CompletionOnceCallback callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!resolver_) {
      // Parent resolver has been destroyed. HostResolver generally disallows
      // calling Start() in this case, but this implementation returns
      // ERR_FAILED to allow testing the case.
      inner_request_ = nullptr;
      return ERR_FAILED;
    }

    if (shutting_down_) {
      // Shutting down but the resolver is not yet destroyed.
      inner_request_ = nullptr;
      return ERR_CONTEXT_SHUT_DOWN;
    }

    DCHECK(inner_request_);
    return inner_request_->Start(std::move(callback));
  }

  const base::Optional<AddressList>& GetAddressResults() const override {
    if (!inner_request_) {
      static base::NoDestructor<base::Optional<AddressList>> nullopt_result;
      return *nullopt_result;
    }

    return inner_request_->GetAddressResults();
  }

  const base::Optional<std::vector<std::string>>& GetTextResults()
      const override {
    if (!inner_request_) {
      static const base::NoDestructor<base::Optional<std::vector<std::string>>>
          nullopt_result;
      return *nullopt_result;
    }

    return inner_request_->GetTextResults();
  }

  const base::Optional<std::vector<HostPortPair>>& GetHostnameResults()
      const override {
    if (!inner_request_) {
      static const base::NoDestructor<base::Optional<std::vector<HostPortPair>>>
          nullopt_result;
      return *nullopt_result;
    }

    return inner_request_->GetHostnameResults();
  }

  const base::Optional<HostCache::EntryStaleness>& GetStaleInfo()
      const override {
    if (!inner_request_) {
      static const base::NoDestructor<base::Optional<HostCache::EntryStaleness>>
          nullopt_result;
      return *nullopt_result;
    }

    return inner_request_->GetStaleInfo();
  }

  void ChangeRequestPriority(RequestPriority priority) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(inner_request_);

    inner_request_->ChangeRequestPriority(priority);
  }

 private:
  std::unique_ptr<HostResolverManager::CancellableRequest> inner_request_;

  // Resolver is expected to call Cancel() on destruction, clearing the pointer
  // before it becomes invalid.
  ContextHostResolver* resolver_;
  bool shutting_down_ = false;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(WrappedRequest);
};

ContextHostResolver::ContextHostResolver(HostResolverManager* manager,
                                         std::unique_ptr<HostCache> host_cache)
    : manager_(manager), host_cache_(std::move(host_cache)) {
  DCHECK(manager_);

  if (host_cache_)
    manager_->AddHostCacheInvalidator(host_cache_->invalidator());
}

ContextHostResolver::ContextHostResolver(
    std::unique_ptr<HostResolverManager> owned_manager,
    std::unique_ptr<HostCache> host_cache)
    : manager_(owned_manager.get()),
      owned_manager_(std::move(owned_manager)),
      host_cache_(std::move(host_cache)) {
  DCHECK(manager_);

  if (host_cache_)
    manager_->AddHostCacheInvalidator(host_cache_->invalidator());
}

ContextHostResolver::~ContextHostResolver() {
  if (owned_manager_)
    DCHECK_EQ(owned_manager_.get(), manager_);

  if (host_cache_)
    manager_->RemoveHostCacheInvalidator(host_cache_->invalidator());

  // Silently cancel all requests associated with this resolver.
  while (!handed_out_requests_.empty())
    (*handed_out_requests_.begin())->Cancel();
}

void ContextHostResolver::OnShutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto* active_request : handed_out_requests_)
    active_request->OnShutdown();

  DCHECK(context_);
  manager_->CancelProbesForContext(context_);

  context_ = nullptr;
  shutting_down_ = true;

  // TODO(crbug.com/1006902): Cancel DoH prober requests too if using
  // |context_|.
}

std::unique_ptr<HostResolver::ResolveHostRequest>
ContextHostResolver::CreateRequest(
    const HostPortPair& host,
    const NetLogWithSource& source_net_log,
    const base::Optional<ResolveHostParameters>& optional_parameters) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::unique_ptr<HostResolverManager::CancellableRequest> inner_request;
  if (!shutting_down_) {
    inner_request = manager_->CreateRequest(
        host, source_net_log, optional_parameters, context_, host_cache_.get());
  }

  auto request = std::make_unique<WrappedRequest>(std::move(inner_request),
                                                  this, shutting_down_);
  handed_out_requests_.insert(request.get());
  return request;
}

std::unique_ptr<HostResolver::MdnsListener>
ContextHostResolver::CreateMdnsListener(const HostPortPair& host,
                                        DnsQueryType query_type) {
  return manager_->CreateMdnsListener(host, query_type);
}

HostCache* ContextHostResolver::GetHostCache() {
  return host_cache_.get();
}

std::unique_ptr<base::Value> ContextHostResolver::GetDnsConfigAsValue() const {
  return manager_->GetDnsConfigAsValue();
}

void ContextHostResolver::SetRequestContext(
    URLRequestContext* request_context) {
  DCHECK(request_context);
  DCHECK(!context_);
  DCHECK(!shutting_down_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  context_ = request_context;
}

HostResolverManager* ContextHostResolver::GetManagerForTesting() {
  return manager_;
}

const URLRequestContext* ContextHostResolver::GetContextForTesting() const {
  return context_;
}

size_t ContextHostResolver::LastRestoredCacheSize() const {
  return host_cache_ ? host_cache_->last_restore_size() : 0;
}

size_t ContextHostResolver::CacheSize() const {
  return host_cache_ ? host_cache_->size() : 0;
}

void ContextHostResolver::SetProcParamsForTesting(
    const ProcTaskParams& proc_params) {
  manager_->set_proc_params_for_test(proc_params);
}

void ContextHostResolver::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  manager_->SetTickClockForTesting(tick_clock);
  if (host_cache_)
    host_cache_->set_tick_clock_for_testing(tick_clock);
}

}  // namespace net
