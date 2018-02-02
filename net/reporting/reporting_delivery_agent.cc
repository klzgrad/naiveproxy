// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_delivery_agent.h"

#include <map>
#include <string>
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
    if (CacheHasReports())
      StartTimer();
  }

 private:
  class Delivery {
   public:
    Delivery(const GURL& endpoint,
             const std::vector<const ReportingReport*>& reports)
        : endpoint(endpoint), reports(reports) {}

    ~Delivery() {}

    const GURL endpoint;
    const std::vector<const ReportingReport*> reports;
  };

  using OriginGroup = std::pair<url::Origin, std::string>;

  bool CacheHasReports() {
    std::vector<const ReportingReport*> reports;
    context_->cache()->GetReports(&reports);
    return !reports.empty();
  }

  void StartTimer() {
    timer_->Start(FROM_HERE, policy().delivery_interval,
                  base::Bind(&ReportingDeliveryAgentImpl::OnTimerFired,
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
    cache()->GetReports(&reports);

    // Sort reports into (origin, group) buckets.
    std::map<OriginGroup, std::vector<const ReportingReport*>>
        origin_group_reports;
    for (const ReportingReport* report : reports) {
      url::Origin origin = url::Origin::Create(report->url);
      if (!delegate()->CanSendReport(origin))
        continue;
      OriginGroup origin_group(url::Origin::Create(report->url), report->group);
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

    // Start a delivery to each endpoint.
    for (auto& it : endpoint_reports) {
      const GURL& endpoint = it.first;
      const std::vector<const ReportingReport*>& reports = it.second;

      endpoint_manager()->SetEndpointPending(endpoint);
      cache()->SetReportsPending(reports);

      std::string json;
      SerializeReports(reports, tick_clock()->NowTicks(), &json);

      uploader()->StartUpload(
          endpoint, json,
          base::Bind(&ReportingDeliveryAgentImpl::OnUploadComplete,
                     weak_factory_.GetWeakPtr(),
                     std::make_unique<Delivery>(endpoint, reports)));
    }
  }

  void OnUploadComplete(const std::unique_ptr<Delivery>& delivery,
                        ReportingUploader::Outcome outcome) {
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
  base::TickClock* tick_clock() { return context_->tick_clock(); }
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

ReportingDeliveryAgent::~ReportingDeliveryAgent() {}

}  // namespace net
