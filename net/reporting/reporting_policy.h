// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_POLICY_H_
#define NET_REPORTING_REPORTING_POLICY_H_

#include "base/time/time.h"
#include "net/base/backoff_entry.h"
#include "net/base/net_export.h"

namespace net {

// Various policy knobs for the Reporting system.
struct NET_EXPORT ReportingPolicy {
  // Provides a reasonable default for use in a browser embedder.
  ReportingPolicy();
  ReportingPolicy(const ReportingPolicy& other);
  ~ReportingPolicy();

  // Maximum number of reports to queue before evicting the oldest.
  size_t max_report_count;

  // Maximum number of clients to remember before evicting least-recently-used.
  size_t max_client_count;

  // Minimum interval at which to attempt delivery of queued reports.
  base::TimeDelta delivery_interval;

  // Backoff policy for failing endpoints.
  BackoffEntry::Policy endpoint_backoff_policy;

  // Minimum interval at which Reporting will persist state to (relatively)
  // stable storage to be restored if the embedder restarts.
  base::TimeDelta persistence_interval;

  // Whether to persist undelivered reports across embedder restarts.
  bool persist_reports_across_restarts;

  // Whether to persist clients (per-origin endpoint configurations) across
  // embedder restarts.
  bool persist_clients_across_restarts;

  // Minimum interval at which to garbage-collect the cache.
  base::TimeDelta garbage_collection_interval;

  // Maximum age a report can be queued for before being discarded as expired.
  base::TimeDelta max_report_age;

  // Maximum number of delivery attempts a report can have before being
  // discarded as failed.
  int max_report_attempts;

  // Whether to persist (versus clear) reports when the network changes to avoid
  // leaking browsing data between networks.
  bool persist_reports_across_network_changes;

  // Whether to persist (versus clear) clients when the network changes to avoid
  // leaking browsing data between networks.
  bool persist_clients_across_network_changes;
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_POLICY_H_
