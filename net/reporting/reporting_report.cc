// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_report.h"

#include <memory>
#include <string>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

namespace net {

namespace {

void RecordReportOutcome(ReportingReport::Outcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Reporting.ReportOutcome", outcome,
                            ReportingReport::Outcome::MAX);
}

}  // namespace

ReportingReport::ReportingReport(const GURL& url,
                                 const std::string& group,
                                 const std::string& type,
                                 std::unique_ptr<const base::Value> body,
                                 base::TimeTicks queued,
                                 int attempts)
    : url(url),
      group(group),
      type(type),
      body(std::move(body)),
      queued(queued),
      attempts(attempts),
      outcome(Outcome::UNKNOWN),
      recorded_outcome(false) {}

ReportingReport::~ReportingReport() {
  DCHECK(recorded_outcome);
}

// static
void ReportingReport::RecordReportDiscardedForNoURLRequestContext() {
  RecordReportOutcome(Outcome::DISCARDED_NO_URL_REQUEST_CONTEXT);
}

// static
void ReportingReport::RecordReportDiscardedForNoReportingService() {
  RecordReportOutcome(Outcome::DISCARDED_NO_REPORTING_SERVICE);
}

void ReportingReport::RecordOutcome(base::TimeTicks now) {
  DCHECK(!recorded_outcome);

  RecordReportOutcome(outcome);

  if (outcome == Outcome::DELIVERED) {
    UMA_HISTOGRAM_LONG_TIMES_100("Reporting.ReportDeliveredLatency",
                                 now - queued);
    UMA_HISTOGRAM_COUNTS_100("Reporting.ReportDeliveredAttempts", attempts);
  }

  recorded_outcome = true;
}

}  // namespace net
