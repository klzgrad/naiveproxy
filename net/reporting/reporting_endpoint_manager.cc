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
  ReportingEndpointManagerImpl(ReportingContext* context) : context_(context) {}

  ~ReportingEndpointManagerImpl() override {}

  bool FindEndpointForOriginAndGroup(const url::Origin& origin,
                                     const std::string& group,
                                     GURL* endpoint_url_out) override {
    std::vector<const ReportingClient*> clients;
    cache()->GetClientsForOriginAndGroup(origin, group, &clients);

    // Filter out expired, pending, and backed-off endpoints.
    std::vector<const ReportingClient*> available_clients;
    base::TimeTicks now = tick_clock()->NowTicks();
    for (const ReportingClient* client : clients) {
      if (client->expires < now)
        continue;
      if (base::ContainsKey(pending_endpoints_, client->endpoint))
        continue;
      if (base::ContainsKey(endpoint_backoff_, client->endpoint) &&
          endpoint_backoff_[client->endpoint]->ShouldRejectRequest()) {
        continue;
      }
      if (!delegate()->CanUseClient(client->origin, client->endpoint))
        continue;
      available_clients.push_back(client);
    }

    if (available_clients.empty()) {
      *endpoint_url_out = GURL();
      return false;
    }

    int random_index = base::RandInt(0, available_clients.size() - 1);
    *endpoint_url_out = available_clients[random_index]->endpoint;
    return true;
  }

  void SetEndpointPending(const GURL& endpoint) override {
    DCHECK(!base::ContainsKey(pending_endpoints_, endpoint));
    pending_endpoints_.insert(endpoint);
  }

  void ClearEndpointPending(const GURL& endpoint) override {
    DCHECK(base::ContainsKey(pending_endpoints_, endpoint));
    pending_endpoints_.erase(endpoint);
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
  base::TickClock* tick_clock() { return context_->tick_clock(); }
  ReportingDelegate* delegate() { return context_->delegate(); }
  ReportingCache* cache() { return context_->cache(); }

  ReportingContext* context_;

  std::set<GURL> pending_endpoints_;

  // Note: Currently the ReportingBrowsingDataRemover does not clear this data
  // because it's not persisted to disk. If it's ever persisted, it will need
  // to be cleared as well.
  std::map<GURL, std::unique_ptr<net::BackoffEntry>> endpoint_backoff_;

  DISALLOW_COPY_AND_ASSIGN(ReportingEndpointManagerImpl);
};

}  // namespace

// static
std::unique_ptr<ReportingEndpointManager> ReportingEndpointManager::Create(
    ReportingContext* context) {
  return std::make_unique<ReportingEndpointManagerImpl>(context);
}

ReportingEndpointManager::~ReportingEndpointManager() {}

}  // namespace net
