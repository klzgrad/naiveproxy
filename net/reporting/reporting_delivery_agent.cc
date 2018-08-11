// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_delivery_agent.h"

#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/time/tick_clock.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_delegate.h"
#include "net/reporting/reporting_endpoint_manager.h"
#include "net/reporting/reporting_observer.h"
#include "net/reporting/reporting_report.h"
#include "net/reporting/reporting_uploader.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

namespace {

void SerializeReports(const std::vector<const ReportingReport*>& reports,
                      base::TimeTicks now,
                      std::string* json_out) {
  base::ListValue reports_value;

  for (const ReportingReport* report : reports) {
    std::unique_ptr<base::DictionaryValue> report_value =
        std::make_unique<base::DictionaryValue>();

    report_value->SetInteger("age", (now - report->queued).InMilliseconds());
    report_value->SetString("type", report->type);
    report_value->SetString("url", report->url.spec());
    report_value->SetKey("report", report->body->Clone());

    reports_value.Append(std::move(report_value));
  }

  bool json_written = base::JSONWriter::Write(reports_value, json_out);
  DCHECK(json_written);
}

class ReportingDeliveryAgentImpl : public ReportingDeliveryAgent,
                                   public ReportingObserver {
 public:
  ReportingDeliveryAgentImpl(ReportingContext* context)
      : context_(context),
        timer_(std::make_unique<base::OneShotTimer>()),
        weak_factory_(this) {
    context_->AddObserver(this);
  }

  // ReportingDeliveryAgent implementation:

  ~ReportingDeliveryAgentImpl() override { context_->RemoveObserver(this); }

  void SetTimerForTesting(std::unique_ptr<base::Timer> timer) override {
    DCHECK(!timer_->IsRunning());
    timer_ = std::move(timer);
  }

  // ReportingObserver implementation:
  void OnCacheUpdated() override {
    if (CacheHasReports() && !timer_->IsRunning()) {
      SendReports();
      StartTimer();
    }
  }

 private:
  class Delivery {
   public:
    Delivery(const GURL& endpoint, std::vector<const ReportingReport*> reports)
        : endpoint(endpoint), reports(std::move(reports)) {}

    ~Delivery() = default;

    const GURL endpoint;
    std::vector<const ReportingReport*> reports;
  };

  using OriginGroup = std::pair<url::Origin, std::string>;

  bool CacheHasReports() {
    std::vector<const ReportingReport*> reports;
    context_->cache()->GetReports(&reports);
    return !reports.empty();
  }

  void StartTimer() {
    timer_->Start(FROM_HERE, policy().delivery_interval,
                  base::BindRepeating(&ReportingDeliveryAgentImpl::OnTimerFired,
                                      base::Unretained(this)));
  }

  void OnTimerFired() {
    if (CacheHasReports()) {
      SendReports();
      StartTimer();
    }
  }

  void SendReports() {
    std::vector<const ReportingReport*> reports;
    cache()->GetNonpendingReports(&reports);

    // Mark all of these reports as pending, so that they're not deleted out
    // from under us while we're checking permissions (possibly on another
    // thread).
    cache()->SetReportsPending(reports);

    // First determine which origins we're allowed to upload reports about.
    std::set<url::Origin> origins;
    for (const ReportingReport* report : reports) {
      origins.insert(url::Origin::Create(report->url));
    }
    delegate()->CanSendReports(
        std::move(origins),
        base::BindOnce(&ReportingDeliveryAgentImpl::OnSendPermissionsChecked,
                       weak_factory_.GetWeakPtr(), std::move(reports)));
  }

  void OnSendPermissionsChecked(std::vector<const ReportingReport*> reports,
                                std::set<url::Origin> allowed_origins) {
    // Sort reports into (origin, group) buckets.
    std::map<OriginGroup, std::vector<const ReportingReport*>>
        origin_group_reports;
    for (const ReportingReport* report : reports) {
      url::Origin origin = url::Origin::Create(report->url);
      if (allowed_origins.find(origin) == allowed_origins.end())
        continue;
      OriginGroup origin_group(origin, report->group);
      origin_group_reports[origin_group].push_back(report);
    }

    // Find endpoint for each (origin, group) bucket and sort reports into
    // endpoint buckets. Don't allow concurrent deliveries to the same (origin,
    // group) bucket.
    std::map<GURL, std::vector<const ReportingReport*>> endpoint_reports;
    for (auto& it : origin_group_reports) {
      const OriginGroup& origin_group = it.first;

      if (base::ContainsKey(pending_origin_groups_, origin_group))
        continue;

      GURL endpoint_url;
      if (!endpoint_manager()->FindEndpointForOriginAndGroup(
              origin_group.first, origin_group.second, &endpoint_url)) {
        continue;
      }

      cache()->MarkClientUsed(origin_group.first, endpoint_url);

      endpoint_reports[endpoint_url].insert(
          endpoint_reports[endpoint_url].end(), it.second.begin(),
          it.second.end());
      pending_origin_groups_.insert(origin_group);
    }

    // Keep track of which of these reports we don't queue for delivery; we'll
    // need to mark them as not-pending.
    std::unordered_set<const ReportingReport*> undelivered_reports(
        reports.begin(), reports.end());

    // Start a delivery to each endpoint.
    for (auto& it : endpoint_reports) {
      const GURL& endpoint = it.first;
      const std::vector<const ReportingReport*>& reports = it.second;

      endpoint_manager()->SetEndpointPending(endpoint);

      std::string json;
      SerializeReports(reports, tick_clock()->NowTicks(), &json);

      int max_depth = 0;
      for (const ReportingReport* report : reports) {
        undelivered_reports.erase(report);
        if (report->depth > max_depth)
          max_depth = report->depth;
      }

      // TODO: Calculate actual max depth.
      uploader()->StartUpload(
          endpoint, json, max_depth,
          base::BindOnce(
              &ReportingDeliveryAgentImpl::OnUploadComplete,
              weak_factory_.GetWeakPtr(),
              std::make_unique<Delivery>(endpoint, std::move(reports))));
    }

    cache()->ClearReportsPending(
        {undelivered_reports.begin(), undelivered_reports.end()});
  }

  void OnUploadComplete(const std::unique_ptr<Delivery>& delivery,
                        ReportingUploader::Outcome outcome) {
    cache()->IncrementEndpointDeliveries(
        delivery->endpoint, delivery->reports,
        outcome == ReportingUploader::Outcome::SUCCESS);

    if (outcome == ReportingUploader::Outcome::SUCCESS) {
      cache()->RemoveReports(delivery->reports,
                             ReportingReport::Outcome::DELIVERED);
      endpoint_manager()->InformOfEndpointRequest(delivery->endpoint, true);
    } else {
      cache()->IncrementReportsAttempts(delivery->reports);
      endpoint_manager()->InformOfEndpointRequest(delivery->endpoint, false);
    }

    if (outcome == ReportingUploader::Outcome::REMOVE_ENDPOINT)
      cache()->RemoveClientsForEndpoint(delivery->endpoint);

    for (const ReportingReport* report : delivery->reports) {
      pending_origin_groups_.erase(
          OriginGroup(url::Origin::Create(report->url), report->group));
    }

    endpoint_manager()->ClearEndpointPending(delivery->endpoint);
    cache()->ClearReportsPending(delivery->reports);
  }

  const ReportingPolicy& policy() { return context_->policy(); }
  const base::TickClock* tick_clock() { return context_->tick_clock(); }
  ReportingDelegate* delegate() { return context_->delegate(); }
  ReportingCache* cache() { return context_->cache(); }
  ReportingUploader* uploader() { return context_->uploader(); }
  ReportingEndpointManager* endpoint_manager() {
    return context_->endpoint_manager();
  }

  ReportingContext* context_;

  std::unique_ptr<base::Timer> timer_;

  // Tracks OriginGroup tuples for which there is a pending delivery running.
  // (Would be an unordered_set, but there's no hash on pair.)
  std::set<OriginGroup> pending_origin_groups_;

  base::WeakPtrFactory<ReportingDeliveryAgentImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ReportingDeliveryAgentImpl);
};

}  // namespace

// static
std::unique_ptr<ReportingDeliveryAgent> ReportingDeliveryAgent::Create(
    ReportingContext* context) {
  return std::make_unique<ReportingDeliveryAgentImpl>(context);
}

ReportingDeliveryAgent::~ReportingDeliveryAgent() = default;

}  // namespace net
