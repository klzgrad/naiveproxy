// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_header_parser.h"

#include <string>

#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_delegate.h"

namespace net {

namespace {

enum class HeaderOutcome {
  DISCARDED_NO_REPORTING_SERVICE = 0,
  DISCARDED_INVALID_SSL_INFO = 1,
  DISCARDED_CERT_STATUS_ERROR = 2,
  DISCARDED_JSON_TOO_BIG = 3,
  DISCARDED_JSON_INVALID = 4,
  PARSED = 5,

  MAX
};

void RecordHeaderOutcome(HeaderOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Net.Reporting.HeaderOutcome", outcome,
                            HeaderOutcome::MAX);
}

enum class HeaderEndpointGroupOutcome {
  DISCARDED_NOT_DICTIONARY = 0,
  DISCARDED_GROUP_NOT_STRING = 1,
  DISCARDED_TTL_MISSING = 2,
  DISCARDED_TTL_NOT_INTEGER = 3,
  DISCARDED_TTL_NEGATIVE = 4,
  DISCARDED_ENDPOINTS_MISSING = 5,
  DISCARDED_ENDPOINTS_NOT_LIST = 6,

  PARSED = 7,

  MAX
};

void RecordHeaderEndpointGroupOutcome(HeaderEndpointGroupOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Net.Reporting.HeaderEndpointGroupOutcome", outcome,
                            HeaderEndpointGroupOutcome::MAX);
}

enum class HeaderEndpointOutcome {
  DISCARDED_NOT_DICTIONARY = 0,
  DISCARDED_URL_MISSING = 1,
  DISCARDED_URL_NOT_STRING = 2,
  DISCARDED_URL_INVALID = 3,
  DISCARDED_URL_INSECURE = 4,
  DISCARDED_PRIORITY_NOT_INTEGER = 5,
  DISCARDED_WEIGHT_NOT_INTEGER = 6,
  DISCARDED_WEIGHT_NOT_POSITIVE = 7,

  REMOVED = 8,
  SET_REJECTED_BY_DELEGATE = 9,
  SET = 10,

  MAX
};

bool EndpointParsedSuccessfully(HeaderEndpointOutcome outcome) {
  return outcome == HeaderEndpointOutcome::REMOVED ||
         outcome == HeaderEndpointOutcome::SET_REJECTED_BY_DELEGATE ||
         outcome == HeaderEndpointOutcome::SET;
}

void RecordHeaderEndpointOutcome(HeaderEndpointOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Net.Reporting.HeaderEndpointOutcome", outcome,
                            HeaderEndpointOutcome::MAX);
}

const char kUrlKey[] = "url";
const char kIncludeSubdomainsKey[] = "include_subdomains";
const char kEndpointsKey[] = "endpoints";
const char kGroupKey[] = "group";
const char kGroupDefaultValue[] = "default";
const char kMaxAgeKey[] = "max_age";
const char kPriorityKey[] = "priority";
const char kWeightKey[] = "weight";

// Processes a single endpoint tuple received in a Report-To header.
//
// |origin| is the origin that sent the Report-To header.
//
// |value| is the parsed JSON value of the endpoint tuple.
//
// |*endpoint_out| will contain the endpoint URL parsed out of the tuple.
HeaderEndpointOutcome ProcessEndpoint(ReportingDelegate* delegate,
                                      ReportingCache* cache,
                                      base::TimeTicks now,
                                      const std::string& group,
                                      int ttl_sec,
                                      ReportingClient::Subdomains subdomains,
                                      const url::Origin& origin,
                                      const base::Value& value,
                                      GURL* endpoint_url_out) {
  *endpoint_url_out = GURL();

  const base::DictionaryValue* dict = nullptr;
  if (!value.GetAsDictionary(&dict))
    return HeaderEndpointOutcome::DISCARDED_NOT_DICTIONARY;
  DCHECK(dict);

  std::string endpoint_url_string;
  if (!dict->HasKey(kUrlKey))
    return HeaderEndpointOutcome::DISCARDED_URL_MISSING;
  if (!dict->GetString(kUrlKey, &endpoint_url_string))
    return HeaderEndpointOutcome::DISCARDED_URL_NOT_STRING;

  GURL endpoint_url(endpoint_url_string);
  if (!endpoint_url.is_valid())
    return HeaderEndpointOutcome::DISCARDED_URL_INVALID;
  if (!endpoint_url.SchemeIsCryptographic())
    return HeaderEndpointOutcome::DISCARDED_URL_INSECURE;

  int priority = ReportingClient::kDefaultPriority;
  if (dict->HasKey(kPriorityKey) && !dict->GetInteger(kPriorityKey, &priority))
    return HeaderEndpointOutcome::DISCARDED_PRIORITY_NOT_INTEGER;

  int weight = ReportingClient::kDefaultWeight;
  if (dict->HasKey(kWeightKey) && !dict->GetInteger(kWeightKey, &weight))
    return HeaderEndpointOutcome::DISCARDED_WEIGHT_NOT_INTEGER;
  if (weight <= 0)
    return HeaderEndpointOutcome::DISCARDED_WEIGHT_NOT_POSITIVE;

  *endpoint_url_out = endpoint_url;

  if (ttl_sec == 0) {
    cache->RemoveClientForOriginAndEndpoint(origin, endpoint_url);
    return HeaderEndpointOutcome::REMOVED;
  }

  if (!delegate->CanSetClient(origin, endpoint_url))
    return HeaderEndpointOutcome::SET_REJECTED_BY_DELEGATE;

  cache->SetClient(origin, endpoint_url, subdomains, group,
                   now + base::TimeDelta::FromSeconds(ttl_sec), priority,
                   weight);
  return HeaderEndpointOutcome::SET;
}

// Processes a single endpoint group tuple received in a Report-To header.
//
// |origin| is the origin that sent the Report-To header.
//
// |value| is the parsed JSON value of the endpoint group tuple.
HeaderEndpointGroupOutcome ProcessEndpointGroup(ReportingDelegate* delegate,
                                                ReportingCache* cache,
                                                std::set<GURL>* new_endpoints,
                                                base::TimeTicks now,
                                                const url::Origin& origin,
                                                const base::Value& value) {
  const base::DictionaryValue* dict = nullptr;
  if (!value.GetAsDictionary(&dict))
    return HeaderEndpointGroupOutcome::DISCARDED_NOT_DICTIONARY;
  DCHECK(dict);

  std::string group = kGroupDefaultValue;
  if (dict->HasKey(kGroupKey) && !dict->GetString(kGroupKey, &group))
    return HeaderEndpointGroupOutcome::DISCARDED_GROUP_NOT_STRING;

  int ttl_sec = -1;
  if (!dict->HasKey(kMaxAgeKey))
    return HeaderEndpointGroupOutcome::DISCARDED_TTL_MISSING;
  if (!dict->GetInteger(kMaxAgeKey, &ttl_sec))
    return HeaderEndpointGroupOutcome::DISCARDED_TTL_NOT_INTEGER;
  if (ttl_sec < 0)
    return HeaderEndpointGroupOutcome::DISCARDED_TTL_NEGATIVE;

  ReportingClient::Subdomains subdomains = ReportingClient::Subdomains::EXCLUDE;
  bool subdomains_bool = false;
  if (dict->HasKey(kIncludeSubdomainsKey) &&
      dict->GetBoolean(kIncludeSubdomainsKey, &subdomains_bool) &&
      subdomains_bool == true) {
    subdomains = ReportingClient::Subdomains::INCLUDE;
  }

  const base::ListValue* endpoint_list = nullptr;
  if (!dict->HasKey(kEndpointsKey))
    return HeaderEndpointGroupOutcome::DISCARDED_ENDPOINTS_MISSING;
  if (!dict->GetList(kEndpointsKey, &endpoint_list))
    return HeaderEndpointGroupOutcome::DISCARDED_ENDPOINTS_NOT_LIST;

  for (size_t i = 0; i < endpoint_list->GetSize(); i++) {
    const base::Value* endpoint = nullptr;
    bool got_endpoint = endpoint_list->Get(i, &endpoint);
    DCHECK(got_endpoint);
    GURL endpoint_url;

    HeaderEndpointOutcome outcome =
        ProcessEndpoint(delegate, cache, now, group, ttl_sec, subdomains,
                        origin, *endpoint, &endpoint_url);
    if (EndpointParsedSuccessfully(outcome))
      new_endpoints->insert(endpoint_url);
    RecordHeaderEndpointOutcome(outcome);
  }

  return HeaderEndpointGroupOutcome::PARSED;
}

}  // namespace

// static
void ReportingHeaderParser::RecordHeaderDiscardedForNoReportingService() {
  RecordHeaderOutcome(HeaderOutcome::DISCARDED_NO_REPORTING_SERVICE);
}

// static
void ReportingHeaderParser::RecordHeaderDiscardedForInvalidSSLInfo() {
  RecordHeaderOutcome(HeaderOutcome::DISCARDED_INVALID_SSL_INFO);
}

// static
void ReportingHeaderParser::RecordHeaderDiscardedForCertStatusError() {
  RecordHeaderOutcome(HeaderOutcome::DISCARDED_CERT_STATUS_ERROR);
}

// static
void ReportingHeaderParser::RecordHeaderDiscardedForJsonInvalid() {
  RecordHeaderOutcome(HeaderOutcome::DISCARDED_JSON_INVALID);
}

// static
void ReportingHeaderParser::RecordHeaderDiscardedForJsonTooBig() {
  RecordHeaderOutcome(HeaderOutcome::DISCARDED_JSON_TOO_BIG);
}

// static
void ReportingHeaderParser::ParseHeader(ReportingContext* context,
                                        const GURL& url,
                                        std::unique_ptr<base::Value> value) {
  DCHECK(url.SchemeIsCryptographic());

  const base::ListValue* group_list = nullptr;
  bool is_list = value->GetAsList(&group_list);
  DCHECK(is_list);

  ReportingDelegate* delegate = context->delegate();
  ReportingCache* cache = context->cache();

  url::Origin origin = url::Origin::Create(url);

  std::vector<GURL> old_endpoints;
  cache->GetEndpointsForOrigin(origin, &old_endpoints);

  std::set<GURL> new_endpoints;

  base::TimeTicks now = context->tick_clock()->NowTicks();
  for (size_t i = 0; i < group_list->GetSize(); i++) {
    const base::Value* group = nullptr;
    bool got_group = group_list->Get(i, &group);
    DCHECK(got_group);
    HeaderEndpointGroupOutcome outcome = ProcessEndpointGroup(
        delegate, cache, &new_endpoints, now, origin, *group);
    RecordHeaderEndpointGroupOutcome(outcome);
  }

  // Remove any endpoints that weren't specified in the current header(s).
  for (const GURL& old_endpoint : old_endpoints) {
    if (new_endpoints.count(old_endpoint) == 0u)
      cache->RemoveClientForOriginAndEndpoint(origin, old_endpoint);
  }

  RecordHeaderOutcome(HeaderOutcome::PARSED);
}

}  // namespace net
