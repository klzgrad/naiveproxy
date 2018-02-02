// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_browsing_data_remover.h"

#include <vector>

#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_report.h"

namespace net {

// static
void ReportingBrowsingDataRemover::RemoveBrowsingData(
    ReportingCache* cache,
    int data_type_mask,
    base::Callback<bool(const GURL&)> origin_filter) {
  bool remove_reports = (data_type_mask & DATA_TYPE_REPORTS) != 0;
  bool remove_clients = (data_type_mask & DATA_TYPE_CLIENTS) != 0;

  if (origin_filter.is_null()) {
    if (remove_reports) {
      cache->RemoveAllReports(
          ReportingReport::Outcome::ERASED_BROWSING_DATA_REMOVED);
    }
    if (remove_clients)
      cache->RemoveAllClients();
    return;
  }

  if (remove_reports) {
    std::vector<const ReportingReport*> all_reports;
    cache->GetReports(&all_reports);

    std::vector<const ReportingReport*> reports_to_remove;
    for (const ReportingReport* report : all_reports) {
      if (origin_filter.Run(report->url))
        reports_to_remove.push_back(report);
    }

    cache->RemoveReports(
        reports_to_remove,
        ReportingReport::Outcome::ERASED_BROWSING_DATA_REMOVED);
  }

  if (remove_clients) {
    std::vector<const ReportingClient*> all_clients;
    cache->GetClients(&all_clients);

    std::vector<const ReportingClient*> clients_to_remove;
    for (const ReportingClient* client : all_clients) {
      // TODO(juliatuttle): Examine client endpoint as well?
      if (origin_filter.Run(client->origin.GetURL()))
        clients_to_remove.push_back(client);
    }

    cache->RemoveClients(clients_to_remove);
  }
}

}  // namespace net
