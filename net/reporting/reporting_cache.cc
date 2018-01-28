// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_cache.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/stl_util.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_report.h"
#include "url/gurl.h"

namespace net {

namespace {

// Returns the superdomain of a given domain, or the empty string if the given
// domain is just a single label. Note that this does not take into account
// anything like the Public Suffix List, so the superdomain may end up being a
// bare TLD.
//
// Examples:
//
// GetSuperdomain("assets.example.com") -> "example.com"
// GetSuperdomain("example.net") -> "net"
// GetSuperdomain("littlebox") -> ""
std::string GetSuperdomain(const std::string& domain) {
  size_t dot_pos = domain.find('.');
  if (dot_pos == std::string::npos)
    return "";

  return domain.substr(dot_pos + 1);
}

class ReportingCacheImpl : public ReportingCache {
 public:
  ReportingCacheImpl(ReportingContext* context) : context_(context) {
    DCHECK(context_);
  }

  ~ReportingCacheImpl() override {
    base::TimeTicks now = tick_clock()->NowTicks();

    // Mark all undoomed reports as erased at shutdown, and record outcomes of
    // all remaining reports (doomed or not).
    for (auto it = reports_.begin(); it != reports_.end(); ++it) {
      ReportingReport* report = it->second.get();
      if (!base::ContainsKey(doomed_reports_, report))
        report->outcome = ReportingReport::Outcome::ERASED_REPORTING_SHUT_DOWN;
      report->RecordOutcome(now);
    }

    reports_.clear();
  }

  void AddReport(const GURL& url,
                 const std::string& group,
                 const std::string& type,
                 std::unique_ptr<const base::Value> body,
                 base::TimeTicks queued,
                 int attempts) override {
    auto report = std::make_unique<ReportingReport>(
        url, group, type, std::move(body), queued, attempts);

    auto inserted =
        reports_.insert(std::make_pair(report.get(), std::move(report)));
    DCHECK(inserted.second);

    if (reports_.size() > context_->policy().max_report_count) {
      // There should be at most one extra report (the one added above).
      DCHECK_EQ(context_->policy().max_report_count + 1, reports_.size());
      const ReportingReport* to_evict = FindReportToEvict();
      DCHECK_NE(nullptr, to_evict);
      // The newly-added report isn't pending, so even if all other reports are
      // pending, the cache should have a report to evict.
      DCHECK(!base::ContainsKey(pending_reports_, to_evict));
      reports_[to_evict]->outcome = ReportingReport::Outcome::ERASED_EVICTED;
      RemoveReportInternal(to_evict);
    }

    context_->NotifyCacheUpdated();
  }

  void GetReports(
      std::vector<const ReportingReport*>* reports_out) const override {
    reports_out->clear();
    for (const auto& it : reports_) {
      if (!base::ContainsKey(doomed_reports_, it.first))
        reports_out->push_back(it.second.get());
    }
  }

  void SetReportsPending(
      const std::vector<const ReportingReport*>& reports) override {
    for (const ReportingReport* report : reports) {
      auto inserted = pending_reports_.insert(report);
      DCHECK(inserted.second);
    }
  }

  void ClearReportsPending(
      const std::vector<const ReportingReport*>& reports) override {
    std::vector<const ReportingReport*> reports_to_remove;

    for (const ReportingReport* report : reports) {
      size_t erased = pending_reports_.erase(report);
      DCHECK_EQ(1u, erased);
      if (base::ContainsKey(doomed_reports_, report)) {
        reports_to_remove.push_back(report);
        doomed_reports_.erase(report);
      }
    }

    for (const ReportingReport* report : reports_to_remove)
      RemoveReportInternal(report);
  }

  void IncrementReportsAttempts(
      const std::vector<const ReportingReport*>& reports) override {
    for (const ReportingReport* report : reports) {
      DCHECK(base::ContainsKey(reports_, report));
      reports_[report]->attempts++;
    }

    context_->NotifyCacheUpdated();
  }

  void RemoveReports(const std::vector<const ReportingReport*>& reports,
                     ReportingReport::Outcome outcome) override {
    for (const ReportingReport* report : reports) {
      reports_[report]->outcome = outcome;
      if (base::ContainsKey(pending_reports_, report)) {
        doomed_reports_.insert(report);
      } else {
        DCHECK(!base::ContainsKey(doomed_reports_, report));
        RemoveReportInternal(report);
      }
    }

    context_->NotifyCacheUpdated();
  }

  void RemoveAllReports(ReportingReport::Outcome outcome) override {
    std::vector<const ReportingReport*> reports_to_remove;
    for (auto it = reports_.begin(); it != reports_.end(); ++it) {
      ReportingReport* report = it->second.get();
      report->outcome = outcome;
      if (!base::ContainsKey(pending_reports_, report))
        reports_to_remove.push_back(report);
      else
        doomed_reports_.insert(report);
    }

    for (const ReportingReport* report : reports_to_remove)
      RemoveReportInternal(report);

    context_->NotifyCacheUpdated();
  }

  void GetClients(
      std::vector<const ReportingClient*>* clients_out) const override {
    clients_out->clear();
    for (const auto& it : clients_)
      for (const auto& endpoint_and_client : it.second)
        clients_out->push_back(endpoint_and_client.second.get());
  }

  void GetClientsForOriginAndGroup(
      const url::Origin& origin,
      const std::string& group,
      std::vector<const ReportingClient*>* clients_out) const override {
    clients_out->clear();

    const auto it = clients_.find(origin);
    if (it != clients_.end()) {
      for (const auto& endpoint_and_client : it->second) {
        if (endpoint_and_client.second->group == group)
          clients_out->push_back(endpoint_and_client.second.get());
      }
    }

    // If no clients were found, try successive superdomain suffixes until a
    // client with includeSubdomains is found or there are no more domain
    // components left.
    std::string domain = origin.host();
    while (clients_out->empty() && !domain.empty()) {
      GetWildcardClientsForDomainAndGroup(domain, group, clients_out);
      domain = GetSuperdomain(domain);
    }
  }

  void SetClient(const url::Origin& origin,
                 const GURL& endpoint,
                 ReportingClient::Subdomains subdomains,
                 const std::string& group,
                 base::TimeTicks expires) override {
    DCHECK(endpoint.SchemeIsCryptographic());

    base::TimeTicks last_used = tick_clock()->NowTicks();

    const ReportingClient* old_client =
        GetClientByOriginAndEndpoint(origin, endpoint);
    if (old_client) {
      last_used = client_last_used_[old_client];
      RemoveClient(old_client);
    }

    AddClient(std::make_unique<ReportingClient>(origin, endpoint, subdomains,
                                                group, expires),
              last_used);

    if (client_last_used_.size() > context_->policy().max_client_count) {
      // There should only ever be one extra client, added above.
      DCHECK_EQ(context_->policy().max_client_count + 1,
                client_last_used_.size());
      // And that shouldn't happen if it was replaced, not added.
      DCHECK(!old_client);
      const ReportingClient* to_evict =
          FindClientToEvict(tick_clock()->NowTicks());
      DCHECK(to_evict);
      RemoveClient(to_evict);
    }

    context_->NotifyCacheUpdated();
  }

  void MarkClientUsed(const url::Origin& origin,
                      const GURL& endpoint) override {
    const ReportingClient* client =
        GetClientByOriginAndEndpoint(origin, endpoint);
    DCHECK(client);
    client_last_used_[client] = tick_clock()->NowTicks();
  }

  void RemoveClients(
      const std::vector<const ReportingClient*>& clients_to_remove) override {
    for (const ReportingClient* client : clients_to_remove)
      RemoveClient(client);

    context_->NotifyCacheUpdated();
  }

  void RemoveClientForOriginAndEndpoint(const url::Origin& origin,
                                        const GURL& endpoint) override {
    const ReportingClient* client =
        GetClientByOriginAndEndpoint(origin, endpoint);
    RemoveClient(client);

    context_->NotifyCacheUpdated();
  }

  void RemoveClientsForEndpoint(const GURL& endpoint) override {
    std::vector<const ReportingClient*> clients_to_remove;

    for (auto& origin_and_endpoints : clients_)
      if (base::ContainsKey(origin_and_endpoints.second, endpoint))
        clients_to_remove.push_back(
            origin_and_endpoints.second[endpoint].get());

    for (const ReportingClient* client : clients_to_remove)
      RemoveClient(client);

    if (!clients_to_remove.empty())
      context_->NotifyCacheUpdated();
  }

  void RemoveAllClients() override {
    clients_.clear();
    wildcard_clients_.clear();
    client_last_used_.clear();

    context_->NotifyCacheUpdated();
  }

  size_t GetFullReportCountForTesting() const override {
    return reports_.size();
  }

  bool IsReportPendingForTesting(const ReportingReport* report) const override {
    return base::ContainsKey(pending_reports_, report);
  }

  bool IsReportDoomedForTesting(const ReportingReport* report) const override {
    return base::ContainsKey(doomed_reports_, report);
  }

 private:
  ReportingContext* context_;

  // Owns all reports, keyed by const raw pointer for easier lookup.
  std::unordered_map<const ReportingReport*, std::unique_ptr<ReportingReport>>
      reports_;

  // Reports that have been marked pending (in use elsewhere and should not be
  // deleted until no longer pending).
  std::unordered_set<const ReportingReport*> pending_reports_;

  // Reports that have been marked doomed (would have been deleted, but were
  // pending when the deletion was requested).
  std::unordered_set<const ReportingReport*> doomed_reports_;

  // Owns all clients, keyed by origin, then endpoint URL.
  // (These would be unordered_map, but neither url::Origin nor GURL has a hash
  // function implemented.)
  std::map<url::Origin, std::map<GURL, std::unique_ptr<ReportingClient>>>
      clients_;

  // References but does not own all clients with includeSubdomains set, keyed
  // by domain name.
  std::unordered_map<std::string, std::unordered_set<const ReportingClient*>>
      wildcard_clients_;

  // The time that each client has last been used.
  std::unordered_map<const ReportingClient*, base::TimeTicks> client_last_used_;

  base::TickClock* tick_clock() { return context_->tick_clock(); }

  void RemoveReportInternal(const ReportingReport* report) {
    reports_[report]->RecordOutcome(tick_clock()->NowTicks());
    size_t erased = reports_.erase(report);
    DCHECK_EQ(1u, erased);
  }

  const ReportingReport* FindReportToEvict() const {
    const ReportingReport* earliest_queued = nullptr;

    for (const auto& it : reports_) {
      const ReportingReport* report = it.first;
      if (base::ContainsKey(pending_reports_, report))
        continue;
      if (!earliest_queued || report->queued < earliest_queued->queued) {
        earliest_queued = report;
      }
    }

    return earliest_queued;
  }

  void AddClient(std::unique_ptr<ReportingClient> client,
                 base::TimeTicks last_used) {
    DCHECK(client);

    url::Origin origin = client->origin;
    GURL endpoint = client->endpoint;

    auto inserted_last_used =
        client_last_used_.insert(std::make_pair(client.get(), last_used));
    DCHECK(inserted_last_used.second);

    if (client->subdomains == ReportingClient::Subdomains::INCLUDE) {
      const std::string& domain = origin.host();
      auto inserted_wildcard_client =
          wildcard_clients_[domain].insert(client.get());
      DCHECK(inserted_wildcard_client.second);
    }

    auto inserted_client =
        clients_[origin].insert(std::make_pair(endpoint, std::move(client)));
    DCHECK(inserted_client.second);
  }

  void RemoveClient(const ReportingClient* client) {
    DCHECK(client);

    url::Origin origin = client->origin;
    GURL endpoint = client->endpoint;

    if (client->subdomains == ReportingClient::Subdomains::INCLUDE) {
      const std::string& domain = origin.host();
      size_t erased_wildcard_client = wildcard_clients_[domain].erase(client);
      DCHECK_EQ(1u, erased_wildcard_client);
      if (wildcard_clients_[domain].empty()) {
        size_t erased_wildcard_domain = wildcard_clients_.erase(domain);
        DCHECK_EQ(1u, erased_wildcard_domain);
      }
    }

    size_t erased_last_used = client_last_used_.erase(client);
    DCHECK_EQ(1u, erased_last_used);

    size_t erased_endpoint = clients_[origin].erase(endpoint);
    DCHECK_EQ(1u, erased_endpoint);
    if (clients_[origin].empty()) {
      size_t erased_origin = clients_.erase(origin);
      DCHECK_EQ(1u, erased_origin);
    }
  }

  const ReportingClient* GetClientByOriginAndEndpoint(
      const url::Origin& origin,
      const GURL& endpoint) const {
    const auto& origin_it = clients_.find(origin);
    if (origin_it == clients_.end())
      return nullptr;

    const auto& endpoint_it = origin_it->second.find(endpoint);
    if (endpoint_it == origin_it->second.end())
      return nullptr;

    return endpoint_it->second.get();
  }

  void GetWildcardClientsForDomainAndGroup(
      const std::string& domain,
      const std::string& group,
      std::vector<const ReportingClient*>* clients_out) const {
    clients_out->clear();

    auto it = wildcard_clients_.find(domain);
    if (it == wildcard_clients_.end())
      return;

    for (const ReportingClient* client : it->second) {
      DCHECK_EQ(ReportingClient::Subdomains::INCLUDE, client->subdomains);
      if (client->group == group)
        clients_out->push_back(client);
    }
  }

  const ReportingClient* FindClientToEvict(base::TimeTicks now) const {
    DCHECK(!client_last_used_.empty());

    const ReportingClient* earliest_used = nullptr;
    base::TimeTicks earliest_used_last_used;
    const ReportingClient* earliest_expired = nullptr;

    for (const auto& it : client_last_used_) {
      const ReportingClient* client = it.first;
      base::TimeTicks client_last_used = it.second;
      if (earliest_used == nullptr ||
          client_last_used < earliest_used_last_used) {
        earliest_used = client;
        earliest_used_last_used = client_last_used;
      }
      if (earliest_expired == nullptr ||
          client->expires < earliest_expired->expires) {
        earliest_expired = client;
      }
    }

    // If there are expired clients, return the earliest-expired.
    if (earliest_expired->expires < now)
      return earliest_expired;
    else
      return earliest_used;
  }
};

}  // namespace

// static
std::unique_ptr<ReportingCache> ReportingCache::Create(
    ReportingContext* context) {
  return std::make_unique<ReportingCacheImpl>(context);
}

ReportingCache::~ReportingCache() {}

}  // namespace net
