// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_endpoint_manager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/time/tick_clock.h"
#include "net/base/backoff_entry.h"
#include "net/base/rand_callback.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_delegate.h"
#include "net/reporting/reporting_endpoint.h"
#include "net/reporting/reporting_policy.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

namespace {

class ReportingEndpointManagerImpl : public ReportingEndpointManager {
 public:
  ReportingEndpointManagerImpl(ReportingContext* context,
                               const RandIntCallback& rand_callback)
      : context_(context), rand_callback_(rand_callback) {}

  ~ReportingEndpointManagerImpl() override = default;

  const ReportingEndpoint FindEndpointForDelivery(
      const url::Origin& origin,
      const std::string& group) override {
    // Get unexpired endpoints that apply to a delivery to |origin| and |group|.
    // May have been configured by a superdomain of |origin|.
    std::vector<ReportingEndpoint> endpoints =
        cache()->GetCandidateEndpointsForDelivery(origin, group);

    // Highest-priority endpoint(s) that are not expired, failing, or
    // forbidden for use by the ReportingDelegate.
    std::vector<ReportingEndpoint> available_endpoints;
    // Total weight of endpoints in |available_endpoints|.
    int total_weight = 0;

    for (const ReportingEndpoint endpoint : endpoints) {
      if (base::Contains(endpoint_backoff_, endpoint.info.url) &&
          endpoint_backoff_[endpoint.info.url]->ShouldRejectRequest()) {
        continue;
      }
      if (!delegate()->CanUseClient(endpoint.group_key.origin,
                                    endpoint.info.url)) {
        continue;
      }

      // If this client is lower priority than the ones we've found, skip it.
      if (!available_endpoints.empty() &&
          endpoint.info.priority > available_endpoints[0].info.priority) {
        continue;
      }

      // If this client is higher priority than the ones we've found (or we
      // haven't found any), forget about those ones and remember this one.
      if (available_endpoints.empty() ||
          endpoint.info.priority < available_endpoints[0].info.priority) {
        available_endpoints.clear();
        total_weight = 0;
      }

      available_endpoints.push_back(endpoint);
      total_weight += endpoint.info.weight;
    }

    if (available_endpoints.empty()) {
      return ReportingEndpoint();
    }

    if (total_weight == 0) {
      int random_index = rand_callback_.Run(0, available_endpoints.size() - 1);
      return available_endpoints[random_index];
    }

    int random_index = rand_callback_.Run(0, total_weight - 1);
    int weight_so_far = 0;
    for (size_t i = 0; i < available_endpoints.size(); ++i) {
      const ReportingEndpoint& endpoint = available_endpoints[i];
      weight_so_far += endpoint.info.weight;
      if (random_index < weight_so_far) {
        return endpoint;
      }
    }

    // TODO(juliatuttle): Can we reach this in some weird overflow case?
    NOTREACHED();
    return ReportingEndpoint();
  }

  void InformOfEndpointRequest(const GURL& endpoint, bool succeeded) override {
    if (!base::Contains(endpoint_backoff_, endpoint)) {
      endpoint_backoff_[endpoint] = std::make_unique<BackoffEntry>(
          &policy().endpoint_backoff_policy, &tick_clock());
    }
    endpoint_backoff_[endpoint]->InformOfRequest(succeeded);
  }

 private:
  const ReportingPolicy& policy() const { return context_->policy(); }
  const base::TickClock& tick_clock() const { return context_->tick_clock(); }
  ReportingDelegate* delegate() { return context_->delegate(); }
  ReportingCache* cache() { return context_->cache(); }

  ReportingContext* context_;

  RandIntCallback rand_callback_;

  // Note: Currently the ReportingBrowsingDataRemover does not clear this data
  // because it's not persisted to disk. If it's ever persisted, it will need
  // to be cleared as well.
  // TODO(chlily): clear this data when endpoints are deleted to avoid unbounded
  // growth of this map.
  std::map<GURL, std::unique_ptr<net::BackoffEntry>> endpoint_backoff_;

  DISALLOW_COPY_AND_ASSIGN(ReportingEndpointManagerImpl);
};

}  // namespace

// static
std::unique_ptr<ReportingEndpointManager> ReportingEndpointManager::Create(
    ReportingContext* context,
    const RandIntCallback& rand_callback) {
  return std::make_unique<ReportingEndpointManagerImpl>(context, rand_callback);
}

ReportingEndpointManager::~ReportingEndpointManager() = default;

}  // namespace net
