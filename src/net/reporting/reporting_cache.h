// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_CACHE_H_
#define NET_REPORTING_REPORTING_CACHE_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/stl_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_export.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_report.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

class ReportingContext;

// The cache holds undelivered reports and clients (per-origin endpoint
// configurations) in memory. (It is not responsible for persisting them.)
//
// This corresponds roughly to the "Reporting cache" in the spec, except that
// endpoints and clients are stored in a more structurally-convenient way, and
// endpoint failures/retry-after are tracked in ReportingEndpointManager.
//
// The cache implementation has the notion of "pending" reports. These are
// reports that are part of an active delivery attempt, so they won't be
// actually deallocated. Any attempt to remove a pending report wil mark it
// "doomed", which will cause it to be deallocated once it is no longer pending.
class NET_EXPORT ReportingCache {
 public:
  // Information about the number of deliveries that we've attempted for each
  // origin and endpoint.
  struct ClientStatistics {
    // The number of attempts uploads that we've made for this client.
    int attempted_uploads = 0;
    // The number of uploads that have succeeded for this client.
    int successful_uploads = 0;
    // The number of individual reports that we've attempted to upload for this
    // client.  (Failed uploads will cause a report to be counted multiple
    // times, once for each attempt.)
    int attempted_reports = 0;
    // The number of individual reports that we've successfully uploaded for
    // this client.
    int successful_reports = 0;
  };

  static std::unique_ptr<ReportingCache> Create(ReportingContext* context);

  virtual ~ReportingCache();

  // Adds a report to the cache.
  //
  // All parameters correspond to the desired values for the relevant fields in
  // ReportingReport.
  virtual void AddReport(const GURL& url,
                         const std::string& user_agent,
                         const std::string& group,
                         const std::string& type,
                         std::unique_ptr<const base::Value> body,
                         int depth,
                         base::TimeTicks queued,
                         int attempts) = 0;

  // Gets all reports in the cache. The returned pointers are valid as long as
  // either no calls to |RemoveReports| have happened or the reports' |pending|
  // flag has been set to true using |SetReportsPending|. Does not return
  // doomed reports (pending reports for which removal has been requested).
  //
  // (Clears any existing data in |*reports_out|.)
  virtual void GetReports(
      std::vector<const ReportingReport*>* reports_out) const = 0;

  // Gets all reports in the cache, including pending and doomed reports, as a
  // base::Value.
  virtual base::Value GetReportsAsValue() const = 0;

  // Gets all reports in the cache that aren't pending. The returned pointers
  // are valid as long as either no calls to |RemoveReports| have happened or
  // the reports' |pending| flag has been set to true using |SetReportsPending|.
  //
  // (Clears any existing data in |*reports_out|.)
  virtual void GetNonpendingReports(
      std::vector<const ReportingReport*>* reports_out) const = 0;

  // Marks a set of reports as pending. |reports| must not already be marked as
  // pending.
  virtual void SetReportsPending(
      const std::vector<const ReportingReport*>& reports) = 0;

  // Unmarks a set of reports as pending. |reports| must be previously marked as
  // pending.
  virtual void ClearReportsPending(
      const std::vector<const ReportingReport*>& reports) = 0;

  // Increments |attempts| on a set of reports.
  virtual void IncrementReportsAttempts(
      const std::vector<const ReportingReport*>& reports) = 0;

  // Records that we attempted (and possibly succeeded at) delivering |reports|
  // to |endpoint|.
  virtual void IncrementEndpointDeliveries(const url::Origin& origin,
                                           const GURL& endpoint,
                                           int reports_delivered,
                                           bool successful) = 0;

  // Removes a set of reports. Any reports that are pending will not be removed
  // immediately, but rather marked doomed and removed once they are no longer
  // pending.
  virtual void RemoveReports(const std::vector<const ReportingReport*>& reports,
                             ReportingReport::Outcome outcome) = 0;

  // Removes all reports. Like |RemoveReports()|, pending reports are doomed
  // until no longer pending.
  virtual void RemoveAllReports(ReportingReport::Outcome outcome) = 0;

  // Creates or updates a client for a particular origin and a particular
  // endpoint.
  //
  // All parameters correspond to the desired values for the fields in
  // ReportingClient.
  //
  // |endpoint| must use a cryptographic scheme.
  virtual void SetClient(const url::Origin& origin,
                         const GURL& endpoint,
                         ReportingClient::Subdomains subdomains,
                         const std::string& group,
                         base::TimeTicks expires,
                         int priority,
                         int client) = 0;

  virtual void MarkClientUsed(const ReportingClient* client) = 0;

  // Gets all of the clients in the cache, regardless of origin or group.
  //
  // (Clears any existing data in |*clients_out|.)
  virtual void GetClients(
      std::vector<const ReportingClient*>* clients_out) const = 0;

  // Gets information about all of the clients in the cache, encoded as a
  // base::Value.
  virtual base::Value GetClientsAsValue() const = 0;

  // Gets all of the clients configured for a particular origin in a particular
  // group. The returned pointers are only guaranteed to be valid if no calls
  // have been made to |SetClient| or |RemoveEndpoint| in between.
  //
  // If no origin match is found, the cache will return clients from the most
  // specific superdomain which contains any clients with include_subdomains
  // set.  For example, given the origin https://foo.bar.baz.com/, the cache
  // would prioritize returning each potential match below over the ones below
  // it:
  //
  // 1. https://foo.bar.baz.com/ (exact origin match)
  // 2. https://foo.bar.baz.com:444/ (technically, a superdomain)
  // 3. https://bar.baz.com/, https://bar.baz.com:444/, etc. (superdomain)
  // 4. https://baz.com/, https://baz.com:444/, etc. (superdomain)
  // etc.
  //
  // (Clears any existing data in |*clients_out|.)
  virtual void GetClientsForOriginAndGroup(
      const url::Origin& origin,
      const std::string& group,
      std::vector<const ReportingClient*>* clients_out) const = 0;

  // Gets all of the endpoints in the cache configured for a particular origin.
  // Does not pay attention to wildcard hosts; only returns endpoints configured
  // by |origin| itself.
  virtual void GetEndpointsForOrigin(
      const url::Origin& origin,
      std::vector<GURL>* endpoints_out) const = 0;

  // Removes a set of clients.
  //
  // May invalidate ReportingClient pointers returned by |GetClients| or
  // |GetClientsForOriginAndGroup|.
  virtual void RemoveClients(
      const std::vector<const ReportingClient*>& clients) = 0;

  // Removes a client for a particular origin and a particular endpoint.
  virtual void RemoveClientForOriginAndEndpoint(const url::Origin& origin,
                                                const GURL& endpoint) = 0;

  // Removes all clients whose endpoint is |endpoint|.
  //
  // May invalidate ReportingClient pointers returned by |GetClients| or
  // |GetClientsForOriginAndGroup|.
  virtual void RemoveClientsForEndpoint(const GURL& endpoint) = 0;

  // Removes all clients.
  virtual void RemoveAllClients() = 0;

  // Returns information about the number of attempted and successful uploads
  // for a particular origin and endpoint.
  virtual ClientStatistics GetStatisticsForOriginAndEndpoint(
      const url::Origin& origin,
      const GURL& endpoint) const = 0;

  // Gets the count of reports in the cache, *including* doomed reports.
  //
  // Needed to ensure that doomed reports are eventually deleted, since no
  // method provides a view of *every* report in the cache, just non-doomed
  // ones.
  virtual size_t GetFullReportCountForTesting() const = 0;

  virtual bool IsReportPendingForTesting(
      const ReportingReport* report) const = 0;

  virtual bool IsReportDoomedForTesting(
      const ReportingReport* report) const = 0;
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_CACHE_H_
