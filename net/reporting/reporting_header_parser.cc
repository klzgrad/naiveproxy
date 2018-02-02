// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_header_parser.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_delegate.h"

namespace net {

namespace {

enum class HeaderOutcome {
  DISCARDED_NO_REPORTING_SERVICE = 0,
  DISCARDED_INVALID_SSL_INFO = 1,
  DISCARDED_CERT_STATUS_ERROR = 2,
  DISCARDED_INVALID_JSON = 3,
  PARSED = 4,

  MAX
};

void RecordHeaderOutcome(HeaderOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Reporting.HeaderOutcome", outcome,
                            HeaderOutcome::MAX);
}

enum class HeaderEndpointOutcome {
  DISCARDED_NOT_DICTIONARY = 0,
  DISCARDED_ENDPOINT_MISSING = 1,
  DISCARDED_ENDPOINT_NOT_STRING = 2,
  DISCARDED_ENDPOINT_INVALID = 3,
  DISCARDED_ENDPOINT_INSECURE = 4,
  DISCARDED_TTL_MISSING = 5,
  DISCARDED_TTL_NOT_INTEGER = 6,
  DISCARDED_TTL_NEGATIVE = 7,
  DISCARDED_GROUP_NOT_STRING = 8,
  REMOVED = 9,
  SET_REJECTED_BY_DELEGATE = 10,
  SET = 11,

  MAX
};

void RecordHeaderEndpointOutcome(HeaderEndpointOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION("Reporting.HeaderEndpointOutcome", outcome,
                            HeaderEndpointOutcome::MAX);
}

const char kUrlKey[] = "url";
const char kIncludeSubdomainsKey[] = "includeSubdomains";
const char kGroupKey[] = "group";
const char kGroupDefaultValue[] = "default";
const char kMaxAgeKey[] = "max-age";

HeaderEndpointOutcome ProcessEndpoint(ReportingDelegate* delegate,
                                      ReportingCache* cache,
                                      base::TimeTicks now,
                                      const GURL& url,
                                      const base::Value& value) {
  const base::DictionaryValue* dict = nullptr;
  if (!value.GetAsDictionary(&dict))
    return HeaderEndpointOutcome::DISCARDED_NOT_DICTIONARY;
  DCHECK(dict);

  std::string endpoint_url_string;
  if (!dict->HasKey(kUrlKey))
    return HeaderEndpointOutcome::DISCARDED_ENDPOINT_MISSING;
  if (!dict->GetString(kUrlKey, &endpoint_url_string))
    return HeaderEndpointOutcome::DISCARDED_ENDPOINT_NOT_STRING;

  GURL endpoint_url(endpoint_url_string);
  if (!endpoint_url.is_valid())
    return HeaderEndpointOutcome::DISCARDED_ENDPOINT_INVALID;
  if (!endpoint_url.SchemeIsCryptographic())
    return HeaderEndpointOutcome::DISCARDED_ENDPOINT_INSECURE;

  int ttl_sec = -1;
  if (!dict->HasKey(kMaxAgeKey))
    return HeaderEndpointOutcome::DISCARDED_TTL_MISSING;
  if (!dict->GetInteger(kMaxAgeKey, &ttl_sec))
    return HeaderEndpointOutcome::DISCARDED_TTL_NOT_INTEGER;
  if (ttl_sec < 0)
    return HeaderEndpointOutcome::DISCARDED_TTL_NEGATIVE;

  std::string group = kGroupDefaultValue;
  if (dict->HasKey(kGroupKey) && !dict->GetString(kGroupKey, &group))
    return HeaderEndpointOutcome::DISCARDED_GROUP_NOT_STRING;

  ReportingClient::Subdomains subdomains = ReportingClient::Subdomains::EXCLUDE;
  bool subdomains_bool = false;
  if (dict->HasKey(kIncludeSubdomainsKey) &&
      dict->GetBoolean(kIncludeSubdomainsKey, &subdomains_bool) &&
      subdomains_bool == true) {
    subdomains = ReportingClient::Subdomains::INCLUDE;
  }

  if (ttl_sec == 0) {
    cache->RemoveClientForOriginAndEndpoint(url::Origin::Create(url),
                                            endpoint_url);
    return HeaderEndpointOutcome::REMOVED;
  }

  url::Origin origin = url::Origin::Create(url);
  if (!delegate->CanSetClient(origin, endpoint_url))
    return HeaderEndpointOutcome::SET_REJECTED_BY_DELEGATE;

  cache->SetClient(origin, endpoint_url, subdomains, group,
                   now + base::TimeDelta::FromSeconds(ttl_sec));
  return HeaderEndpointOutcome::SET;
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
void ReportingHeaderParser::ParseHeader(ReportingContext* context,
                                        const GURL& url,
                                        const std::string& json_value) {
  DCHECK(url.SchemeIsCryptographic());

  std::unique_ptr<base::Value> value =
      base::JSONReader::Read("[" + json_value + "]");
  if (!value) {
    RecordHeaderOutcome(HeaderOutcome::DISCARDED_INVALID_JSON);
    return;
  }

  const base::ListValue* list = nullptr;
  bool is_list = value->GetAsList(&list);
  DCHECK(is_list);

  ReportingDelegate* delegate = context->delegate();
  ReportingCache* cache = context->cache();
  base::TimeTicks now = context->tick_clock()->NowTicks();
  for (size_t i = 0; i < list->GetSize(); i++) {
    const base::Value* endpoint = nullptr;
    bool got_endpoint = list->Get(i, &endpoint);
    DCHECK(got_endpoint);
    RecordHeaderEndpointOutcome(
        ProcessEndpoint(delegate, cache, now, url, *endpoint));
  }
}

}  // namespace net
