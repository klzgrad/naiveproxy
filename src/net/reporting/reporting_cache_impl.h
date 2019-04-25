// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_CACHE_IMPL_H_
#define NET_REPORTING_REPORTING_CACHE_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/macros.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_report.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

class ReportingCacheImpl : public ReportingCache {
 public:
  explicit ReportingCacheImpl(ReportingContext* context);

  ~ReportingCacheImpl() override;

  // ReportingCache implementation
  void AddReport(const GURL& url,
                 const std::string& user_agent,
                 const std::string& group,
                 const std::string& type,
                 std::unique_ptr<const base::Value> body,
                 int depth,
                 base::TimeTicks queued,
                 int attempts) override;
  void GetReports(
      std::vector<const ReportingReport*>* reports_out) const override;
  base::Value GetReportsAsValue() const override;
  void GetNonpendingReports(
      std::vector<const ReportingReport*>* reports_out) const override;
  void SetReportsPending(
      const std::vector<const ReportingReport*>& reports) override;
  void ClearReportsPending(
      const std::vector<const ReportingReport*>& reports) override;
  void IncrementReportsAttempts(
      const std::vector<const ReportingReport*>& reports) override;
  void IncrementEndpointDeliveries(const url::Origin& origin,
                                   const GURL& endpoint,
                                   int reports_delivered,
                                   bool successful) override;
  void RemoveReports(const std::vector<const ReportingReport*>& reports,
                     ReportingReport::Outcome outcome) override;
  void RemoveAllReports(ReportingReport::Outcome outcome) override;
  void SetClient(const url::Origin& origin,
                 const GURL& endpoint,
                 ReportingClient::Subdomains subdomains,
                 const std::string& group,
                 base::TimeTicks expires,
                 int priority,
                 int weight) override;
  void MarkClientUsed(const ReportingClient* client) override;
  void GetClients(
      std::vector<const ReportingClient*>* clients_out) const override;
  base::Value GetClientsAsValue() const override;
  void GetClientsForOriginAndGroup(
      const url::Origin& origin,
      const std::string& group,
      std::vector<const ReportingClient*>* clients_out) const override;
  void GetEndpointsForOrigin(const url::Origin& origin,
                             std::vector<GURL>* endpoints_out) const override;
  void RemoveClients(
      const std::vector<const ReportingClient*>& clients_to_remove) override;
  void RemoveClientForOriginAndEndpoint(const url::Origin& origin,
                                        const GURL& endpoint) override;
  void RemoveClientsForEndpoint(const GURL& endpoint) override;
  void RemoveAllClients() override;
  ClientStatistics GetStatisticsForOriginAndEndpoint(
      const url::Origin& origin,
      const GURL& endpoint) const override;
  size_t GetFullReportCountForTesting() const override;
  bool IsReportPendingForTesting(const ReportingReport* report) const override;
  bool IsReportDoomedForTesting(const ReportingReport* report) const override;

 private:
  struct ClientMetadata {
    base::TimeTicks last_used;
    ReportingCache::ClientStatistics stats;
  };

  void RemoveReportInternal(const ReportingReport* report);

  const ReportingReport* FindReportToEvict() const;

  void AddClient(std::unique_ptr<ReportingClient> client,
                 base::TimeTicks last_used);

  void RemoveClient(const ReportingClient* client);

  const ReportingClient* GetClientByOriginAndEndpoint(
      const url::Origin& origin,
      const GURL& endpoint) const;

  void GetWildcardClientsForDomainAndGroup(
      const std::string& domain,
      const std::string& group,
      std::vector<const ReportingClient*>* clients_out) const;

  const ReportingClient* FindClientToEvict(base::TimeTicks now) const;

  const base::TickClock* tick_clock() { return context_->tick_clock(); }

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

  // References but does not own all clients with include_subdomains set, keyed
  // by domain name.
  std::unordered_map<std::string, std::unordered_set<const ReportingClient*>>
      wildcard_clients_;

  // The time that each client has last been used.
  std::unordered_map<const ReportingClient*, ClientMetadata> client_metadata_;

  DISALLOW_COPY_AND_ASSIGN(ReportingCacheImpl);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_CACHE_IMPL_H_
