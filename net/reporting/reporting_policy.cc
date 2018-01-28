// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_policy.h"

#include "base/time/time.h"

namespace net {

ReportingPolicy::ReportingPolicy()
    : max_report_count(100u),
      max_client_count(1000u),
      delivery_interval(base::TimeDelta::FromMinutes(1)),
      persistence_interval(base::TimeDelta::FromMinutes(1)),
      persist_reports_across_restarts(false),
      persist_clients_across_restarts(true),
      garbage_collection_interval(base::TimeDelta::FromMinutes(5)),
      max_report_age(base::TimeDelta::FromMinutes(15)),
      max_report_attempts(5),
      clear_reports_on_network_changes(true),
      clear_clients_on_network_changes(false) {
  endpoint_backoff_policy.num_errors_to_ignore = 0;
  endpoint_backoff_policy.initial_delay_ms = 60 * 1000;  // 1 minute
  endpoint_backoff_policy.multiply_factor = 2.0;
  endpoint_backoff_policy.jitter_factor = 0.1;
  endpoint_backoff_policy.maximum_backoff_ms = -1;  // 1 hour
  endpoint_backoff_policy.entry_lifetime_ms = -1;   // infinite
  endpoint_backoff_policy.always_use_initial_delay = false;
}

ReportingPolicy::ReportingPolicy(const ReportingPolicy& other)
    : max_report_count(other.max_report_count),
      max_client_count(other.max_client_count),
      delivery_interval(other.delivery_interval),
      endpoint_backoff_policy(other.endpoint_backoff_policy),
      persistence_interval(other.persistence_interval),
      persist_reports_across_restarts(other.persist_reports_across_restarts),
      persist_clients_across_restarts(other.persist_clients_across_restarts),
      garbage_collection_interval(other.garbage_collection_interval),
      max_report_age(other.max_report_age),
      max_report_attempts(other.max_report_attempts),
      clear_reports_on_network_changes(other.clear_reports_on_network_changes),
      clear_clients_on_network_changes(other.clear_clients_on_network_changes) {
}

ReportingPolicy::~ReportingPolicy() {}

}  // namespace net
