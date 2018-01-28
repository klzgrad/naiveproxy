// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/network_error_logging/network_error_logging_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/reporting/reporting_service.h"
#include "net/socket/next_proto.h"
#include "net/url_request/network_error_logging_delegate.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace features {

const base::Feature kNetworkErrorLogging{"NetworkErrorLogging",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features

namespace net {

namespace {

const char kReportToKey[] = "report-to";
const char kMaxAgeKey[] = "max-age";
const char kIncludeSubdomainsKey[] = "includeSubdomains";

// Returns the superdomain of a given domain, or the empty string if the given
// domain is just a single label. Note that this does not take into account
// anything like the Public Suffix List, so the superdomain may end up being a
// bare TLD.
//
// Examples:
//
// GetSuperdomain("assets.example.com") -> "example.com"
// GetSuperdomain("example.net") -> "net"
// GetSuperdomain("littlebox") -> ""
//
// TODO(juliatuttle): Deduplicate from Reporting in //net.
std::string GetSuperdomain(const std::string& domain) {
  size_t dot_pos = domain.find('.');
  if (dot_pos == std::string::npos)
    return "";

  return domain.substr(dot_pos + 1);
}

const struct {
  Error error;
  const char* type;
} kErrorTypes[] = {
    // dns.unreachable?
    {ERR_NAME_NOT_RESOLVED, "dns.name_not_resolved"},
    {ERR_NAME_RESOLUTION_FAILED, "dns.failed"},
    {ERR_DNS_TIMED_OUT, "dns.timed_out"},

    {ERR_CONNECTION_TIMED_OUT, "tcp.timed_out"},
    {ERR_CONNECTION_CLOSED, "tcp.closed"},
    {ERR_CONNECTION_RESET, "tcp.reset"},
    {ERR_CONNECTION_REFUSED, "tcp.refused"},
    {ERR_CONNECTION_ABORTED, "tcp.aborted"},
    {ERR_ADDRESS_INVALID, "tcp.address_invalid"},
    {ERR_ADDRESS_UNREACHABLE, "tcp.address_unreachable"},
    {ERR_CONNECTION_FAILED, "tcp.failed"},

    {ERR_SSL_VERSION_OR_CIPHER_MISMATCH, "tls.version_or_cipher_mismatch"},
    {ERR_BAD_SSL_CLIENT_AUTH_CERT, "tls.bad_client_auth_cert"},
    {ERR_CERT_COMMON_NAME_INVALID, "tls.cert.name_invalid"},
    {ERR_CERT_DATE_INVALID, "tls.cert.date_invalid"},
    {ERR_CERT_AUTHORITY_INVALID, "tls.cert.authority_invalid"},
    {ERR_CERT_INVALID, "tls.cert.invalid"},
    {ERR_CERT_REVOKED, "tls.cert.revoked"},
    {ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN,
     "tls.cert.pinned_key_not_in_cert_chain"},
    {ERR_SSL_PROTOCOL_ERROR, "tls.protocol.error"},
    // tls.failed?

    // http.protocol.error?
    {ERR_INVALID_HTTP_RESPONSE, "http.response.invalid"},
    {ERR_TOO_MANY_REDIRECTS, "http.response.redirect_loop"},
    // http.failed?

    {ERR_ABORTED, "abandoned"},
    // unknown?

    // TODO(juliatuttle): Surely there are more errors we want here.
};

bool GetTypeFromNetError(Error error, std::string* type_out) {
  for (size_t i = 0; i < arraysize(kErrorTypes); ++i) {
    if (kErrorTypes[i].error == error) {
      *type_out = kErrorTypes[i].type;
      return true;
    }
  }
  return false;
}

}  // namespace

// static:

const char NetworkErrorLoggingService::kReportType[] = "network-error";
const char NetworkErrorLoggingService::kUriKey[] = "uri";
const char NetworkErrorLoggingService::kReferrerKey[] = "referrer";
const char NetworkErrorLoggingService::kServerIpKey[] = "server-ip";
const char NetworkErrorLoggingService::kProtocolKey[] = "protocol";
const char NetworkErrorLoggingService::kStatusCodeKey[] = "status-code";
const char NetworkErrorLoggingService::kElapsedTimeKey[] = "elapsed-time";
const char NetworkErrorLoggingService::kTypeKey[] = "type";

// static
std::unique_ptr<NetworkErrorLoggingService>
NetworkErrorLoggingService::Create() {
  if (!base::FeatureList::IsEnabled(features::kNetworkErrorLogging))
    return std::unique_ptr<NetworkErrorLoggingService>();

  // Would be MakeUnique, but the constructor is private so MakeUnique can't see
  // it.
  return base::WrapUnique(new NetworkErrorLoggingService());
}

NetworkErrorLoggingService::~NetworkErrorLoggingService() = default;

void NetworkErrorLoggingService::SetReportingService(
    ReportingService* reporting_service) {
  reporting_service_ = reporting_service;
}

void NetworkErrorLoggingService::OnHeader(const url::Origin& origin,
                                          const std::string& value) {
  // NEL is only available to secure origins, so don't permit insecure origins
  // to set policies.
  if (!origin.GetURL().SchemeIsCryptographic())
    return;

  OriginPolicy policy;
  if (!ParseHeader(value, &policy))
    return;

  PolicyMap::iterator it = policies_.find(origin);
  if (it != policies_.end()) {
    MaybeRemoveWildcardPolicy(origin, &it->second);
    policies_.erase(it);
  }

  if (policy.expires.is_null())
    return;

  auto inserted = policies_.insert(std::make_pair(origin, policy));
  DCHECK(inserted.second);
  MaybeAddWildcardPolicy(origin, &inserted.first->second);
}

void NetworkErrorLoggingService::OnNetworkError(const ErrorDetails& details) {
  if (!reporting_service_)
    return;

  url::Origin origin = url::Origin::Create(details.uri);

  // NEL is only available to secure origins, so ignore network errors from
  // insecure origins. (The check in OnHeader prevents insecure origins from
  // setting policies, but this check is needed to ensure that insecure origins
  // can't match wildcard policies from secure origins.)
  if (!origin.GetURL().SchemeIsCryptographic())
    return;

  const OriginPolicy* policy = FindPolicyForOrigin(origin);
  if (!policy)
    return;

  std::string type_string;
  if (!GetTypeFromNetError(details.type, &type_string))
    return;

  reporting_service_->QueueReport(details.uri, policy->report_to, kReportType,
                                  CreateReportBody(type_string, details));
}

void NetworkErrorLoggingService::SetTickClockForTesting(
    std::unique_ptr<base::TickClock> tick_clock) {
  DCHECK(tick_clock);
  tick_clock_ = std::move(tick_clock);
}

NetworkErrorLoggingService::NetworkErrorLoggingService()
    : tick_clock_(base::MakeUnique<base::DefaultTickClock>()),
      reporting_service_(nullptr) {}

bool NetworkErrorLoggingService::ParseHeader(const std::string& json_value,
                                             OriginPolicy* policy_out) {
  DCHECK(policy_out);

  std::unique_ptr<base::Value> value = base::JSONReader::Read(json_value);
  if (!value)
    return false;

  const base::DictionaryValue* dict = nullptr;
  if (!value->GetAsDictionary(&dict))
    return false;

  int max_age_sec;
  if (!dict->GetInteger(kMaxAgeKey, &max_age_sec) || max_age_sec < 0)
    return false;

  std::string report_to;
  if (!dict->GetString(kReportToKey, &report_to) && max_age_sec > 0)
    return false;

  bool include_subdomains = false;
  // includeSubdomains is optional and defaults to false, so it's okay if
  // GetBoolean fails.
  dict->GetBoolean(kIncludeSubdomainsKey, &include_subdomains);

  policy_out->report_to = report_to;
  if (max_age_sec > 0) {
    policy_out->expires =
        tick_clock_->NowTicks() + base::TimeDelta::FromSeconds(max_age_sec);
  } else {
    policy_out->expires = base::TimeTicks();
  }
  policy_out->include_subdomains = include_subdomains;

  return true;
}

const NetworkErrorLoggingService::OriginPolicy*
NetworkErrorLoggingService::FindPolicyForOrigin(
    const url::Origin& origin) const {
  // TODO(juliatuttle): Clean out expired policies sometime/somewhere.
  PolicyMap::const_iterator it = policies_.find(origin);
  if (it != policies_.end() && tick_clock_->NowTicks() < it->second.expires)
    return &it->second;

  std::string domain = origin.host();
  const OriginPolicy* wildcard_policy = nullptr;
  while (!wildcard_policy && !domain.empty()) {
    wildcard_policy = FindWildcardPolicyForDomain(domain);
    domain = GetSuperdomain(domain);
  }

  return wildcard_policy;
}

const NetworkErrorLoggingService::OriginPolicy*
NetworkErrorLoggingService::FindWildcardPolicyForDomain(
    const std::string& domain) const {
  DCHECK(!domain.empty());

  WildcardPolicyMap::const_iterator it = wildcard_policies_.find(domain);
  if (it == wildcard_policies_.end())
    return nullptr;

  DCHECK(!it->second.empty());

  // TODO(juliatuttle): Come up with a deterministic way to resolve these.
  if (it->second.size() > 1) {
    LOG(WARNING) << "Domain " << domain
                 << " matches multiple origins with includeSubdomains; "
                 << "choosing one arbitrarily.";
  }

  for (std::set<const OriginPolicy*>::const_iterator jt = it->second.begin();
       jt != it->second.end(); ++jt) {
    if (tick_clock_->NowTicks() < (*jt)->expires)
      return *jt;
  }

  return nullptr;
}

void NetworkErrorLoggingService::MaybeAddWildcardPolicy(
    const url::Origin& origin,
    const OriginPolicy* policy) {
  DCHECK(policy);
  DCHECK_EQ(policy, &policies_[origin]);

  if (!policy->include_subdomains)
    return;

  auto inserted = wildcard_policies_[origin.host()].insert(policy);
  DCHECK(inserted.second);
}

void NetworkErrorLoggingService::MaybeRemoveWildcardPolicy(
    const url::Origin& origin,
    const OriginPolicy* policy) {
  DCHECK(policy);
  DCHECK_EQ(policy, &policies_[origin]);

  if (!policy->include_subdomains)
    return;

  WildcardPolicyMap::iterator wildcard_it =
      wildcard_policies_.find(origin.host());
  DCHECK(wildcard_it != wildcard_policies_.end());

  size_t erased = wildcard_it->second.erase(policy);
  DCHECK_EQ(1u, erased);
  if (wildcard_it->second.empty())
    wildcard_policies_.erase(wildcard_it);
}

std::unique_ptr<const base::Value> NetworkErrorLoggingService::CreateReportBody(
    const std::string& type,
    const NetworkErrorLoggingDelegate::ErrorDetails& details) const {
  auto body = base::MakeUnique<base::DictionaryValue>();

  body->SetString(kUriKey, details.uri.spec());
  body->SetString(kReferrerKey, details.referrer.spec());
  body->SetString(kServerIpKey, details.server_ip.ToString());
  std::string protocol = NextProtoToString(details.protocol);
  if (protocol == "unknown")
    protocol = "";
  body->SetString(kProtocolKey, protocol);
  body->SetInteger(kStatusCodeKey, details.status_code);
  body->SetInteger(kElapsedTimeKey, details.elapsed_time.InMilliseconds());
  body->SetString(kTypeKey, type);

  return std::move(body);
}

}  // namespace net
