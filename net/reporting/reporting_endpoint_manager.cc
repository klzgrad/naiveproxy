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
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_delegate.h"
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

  const ReportingClient* FindClientForOriginAndGroup(
      const url::Origin& origin,
      const std::string& group) override {
    std::vector<const ReportingClient*> clients;
    cache()->GetClientsForOriginAndGroup(origin, group, &clients);

    // Highest-priority client(s) that are not expired, pending, failing, or
    // forbidden for use by the ReportingDelegate.
    std::vector<const ReportingClient*> available_clients;
    // Total weight of clients in available_clients.
    int total_weight = 0;

    base::TimeTicks now = tick_clock()->NowTicks();
    for (const ReportingClient* client : clients) {
      if (client->expires < now)
        continue;
      if (base::ContainsKey(endpoint_backoff_, client->endpoint) &&
          endpoint_backoff_[client->endpoint]->ShouldRejectRequest()) {
        continue;
      }
      if (!delegate()->CanUseClient(client->origin, client->endpoint))
        continue;

      // If this client is lower priority than the ones we've found, skip it.
      if (!available_clients.empty() &&
          client->priority > available_clients[0]->priority) {
        continue;
      }

      // If this client is higher priority than the ones we've found (or we
      // haven't found any), forget about those ones and remember this one.
      if (available_clients.empty() ||
          client->priority < available_clients[0]->priority) {
        available_clients.clear();
        total_weight = 0;
      }

      available_clients.push_back(client);
      total_weight += client->weight;
    }

    if (available_clients.empty()) {
      return nullptr;
    }

    int random_index = rand_callback_.Run(0, total_weight - 1);
    int weight_so_far = 0;
    for (size_t i = 0; i < available_clients.size(); ++i) {
      const ReportingClient* client = available_clients[i];
      weight_so_far += client->weight;
      if (random_index < weight_so_far) {
        return client;
      }
    }

    // TODO(juliatuttle): Can we reach this in some weird overflow case?
    NOTREACHED();
    return nullptr;
  }

  void InformOfEndpointRequest(const GURL& endpoint, bool succeeded) override {
    if (!base::ContainsKey(endpoint_backoff_, endpoint)) {
      endpoint_backoff_[endpoint] = std::make_unique<BackoffEntry>(
          &policy().endpoint_backoff_policy, tick_clock());
    }
    endpoint_backoff_[endpoint]->InformOfRequest(succeeded);
  }

 private:
  const ReportingPolicy& policy() { return context_->policy(); }
  const base::TickClock* tick_clock() { return context_->tick_clock(); }
  ReportingDelegate* delegate() { return context_->delegate(); }
  ReportingCache* cache() { return context_->cache(); }

  ReportingContext* context_;

  RandIntCallback rand_callback_;

  // Note: Currently the ReportingBrowsingDataRemover does not clear this data
  // because it's not persisted to disk. If it's ever persisted, it will need
  // to be cleared as well.
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
