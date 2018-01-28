// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_server_properties.h"

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "net/http/http_network_session.h"
#include "net/socket/ssl_client_socket.h"
#include "net/ssl/ssl_config.h"

namespace net {

namespace {

enum AlternativeProxyUsage {
  // Alternative Proxy was used without racing a normal connection.
  ALTERNATIVE_PROXY_USAGE_NO_RACE = 0,
  // Alternative Proxy was used by winning a race with a normal connection.
  ALTERNATIVE_PROXY_USAGE_WON_RACE = 1,
  // Alternative Proxy was not used by losing a race with a normal connection.
  ALTERNATIVE_PROXY_USAGE_LOST_RACE = 2,
  // Maximum value for the enum.
  ALTERNATIVE_PROXY_USAGE_MAX,
};

AlternativeProxyUsage ConvertProtocolUsageToProxyUsage(
    AlternateProtocolUsage usage) {
  switch (usage) {
    case ALTERNATE_PROTOCOL_USAGE_NO_RACE:
      return ALTERNATIVE_PROXY_USAGE_NO_RACE;
    case ALTERNATE_PROTOCOL_USAGE_WON_RACE:
      return ALTERNATIVE_PROXY_USAGE_WON_RACE;
    case ALTERNATE_PROTOCOL_USAGE_LOST_RACE:
      return ALTERNATIVE_PROXY_USAGE_LOST_RACE;
    default:
      NOTREACHED();
      return ALTERNATIVE_PROXY_USAGE_MAX;
  }
}

}  // anonymous namespace

const char kAlternativeServiceHeader[] = "Alt-Svc";

void HistogramAlternateProtocolUsage(AlternateProtocolUsage usage,
                                     bool proxy_server_used) {
  if (proxy_server_used) {
    DCHECK_LE(usage, ALTERNATE_PROTOCOL_USAGE_LOST_RACE);
    UMA_HISTOGRAM_ENUMERATION("Net.QuicAlternativeProxy.Usage",
                              ConvertProtocolUsageToProxyUsage(usage),
                              ALTERNATIVE_PROXY_USAGE_MAX);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Net.AlternateProtocolUsage", usage,
                              ALTERNATE_PROTOCOL_USAGE_MAX);
  }
}

void HistogramBrokenAlternateProtocolLocation(
    BrokenAlternateProtocolLocation location){
  UMA_HISTOGRAM_ENUMERATION("Net.AlternateProtocolBrokenLocation", location,
                            BROKEN_ALTERNATE_PROTOCOL_LOCATION_MAX);
}

bool IsAlternateProtocolValid(NextProto protocol) {
  switch (protocol) {
    case kProtoUnknown:
      return false;
    case kProtoHTTP11:
      return false;
    case kProtoHTTP2:
      return true;
    case kProtoQUIC:
      return true;
  }
  NOTREACHED();
  return false;
}

// static
AlternativeServiceInfo
AlternativeServiceInfo::CreateHttp2AlternativeServiceInfo(
    const AlternativeService& alternative_service,
    base::Time expiration) {
  DCHECK_EQ(alternative_service.protocol, kProtoHTTP2);
  return AlternativeServiceInfo(alternative_service, expiration,
                                QuicTransportVersionVector());
}

// static
AlternativeServiceInfo AlternativeServiceInfo::CreateQuicAlternativeServiceInfo(
    const AlternativeService& alternative_service,
    base::Time expiration,
    const QuicTransportVersionVector& advertised_versions) {
  DCHECK_EQ(alternative_service.protocol, kProtoQUIC);
  return AlternativeServiceInfo(alternative_service, expiration,
                                advertised_versions);
}

AlternativeServiceInfo::AlternativeServiceInfo() : alternative_service_() {}

AlternativeServiceInfo::~AlternativeServiceInfo() = default;

AlternativeServiceInfo::AlternativeServiceInfo(
    const AlternativeService& alternative_service,
    base::Time expiration,
    const QuicTransportVersionVector& advertised_versions)
    : alternative_service_(alternative_service), expiration_(expiration) {
  if (alternative_service_.protocol == kProtoQUIC) {
    advertised_versions_ = advertised_versions;
    std::sort(advertised_versions_.begin(), advertised_versions_.end());
  }
}

AlternativeServiceInfo::AlternativeServiceInfo(
    const AlternativeServiceInfo& alternative_service_info) = default;

AlternativeServiceInfo& AlternativeServiceInfo::operator=(
    const AlternativeServiceInfo& alternative_service_info) = default;

std::string AlternativeService::ToString() const {
  return base::StringPrintf("%s %s:%d", NextProtoToString(protocol),
                            host.c_str(), port);
}

std::string AlternativeServiceInfo::ToString() const {
  base::Time::Exploded exploded;
  expiration_.LocalExplode(&exploded);
  return base::StringPrintf(
      "%s, expires %04d-%02d-%02d %02d:%02d:%02d",
      alternative_service_.ToString().c_str(), exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second);
}

std::ostream& operator<<(std::ostream& os,
                         const AlternativeService& alternative_service) {
  os << alternative_service.ToString();
  return os;
}

// static
void HttpServerProperties::ForceHTTP11(SSLConfig* ssl_config) {
  ssl_config->alpn_protos.clear();
  ssl_config->alpn_protos.push_back(kProtoHTTP11);
}

}  // namespace net
