// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_
#define NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/feature_list.h"
#include "base/macros.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/url_request/network_error_logging_delegate.h"

namespace base {
class Value;
}  // namespace base

namespace net {
class ReportingService;
}  // namespace net

namespace url {
class Origin;
}  // namespace url

namespace features {
extern const base::Feature NET_EXPORT kNetworkErrorLogging;
}  // namespace features

namespace net {

class NET_EXPORT NetworkErrorLoggingService
    : public NetworkErrorLoggingDelegate {
 public:
  static const char kReportType[];

  // Keys for data included in report bodies. Exposed for tests.

  static const char kUriKey[];
  static const char kReferrerKey[];
  static const char kServerIpKey[];
  static const char kProtocolKey[];
  static const char kStatusCodeKey[];
  static const char kElapsedTimeKey[];
  static const char kTypeKey[];

  // Creates the NetworkErrorLoggingService.
  //
  // Will return nullptr if Network Error Logging is disabled via
  // base::FeatureList.
  static std::unique_ptr<NetworkErrorLoggingService> Create();

  // NetworkErrorLoggingDelegate implementation:

  ~NetworkErrorLoggingService() override;

  void SetReportingService(ReportingService* reporting_service) override;

  void OnHeader(const url::Origin& origin, const std::string& value) override;

  void OnNetworkError(const ErrorDetails& details) override;

  void SetTickClockForTesting(std::unique_ptr<base::TickClock> tick_clock);

 private:
  // NEL Policy set by an origin.
  struct OriginPolicy {
    // Reporting API endpoint group to which reports should be sent.
    std::string report_to;

    base::TimeTicks expires;

    bool include_subdomains;
  };

  // Map from origin to origin's (owned) policy.
  // Would be unordered_map, but url::Origin has no hash.
  using PolicyMap = std::map<url::Origin, OriginPolicy>;

  // Wildcard policies are policies for which the includeSubdomains flag is set.
  //
  // Wildcard policies are accessed by domain name, not full origin, so there
  // can be multiple wildcard policies per domain name.
  //
  // This is a map from domain name to the set of pointers to wildcard policies
  // in that domain.
  //
  // Policies in the map are unowned; they are pointers to the original in the
  // PolicyMap.
  using WildcardPolicyMap =
      std::map<std::string, std::set<const OriginPolicy*>>;

  NetworkErrorLoggingService();

  // Would be const, but base::TickClock::NowTicks isn't.
  bool ParseHeader(const std::string& json_value, OriginPolicy* policy_out);

  const OriginPolicy* FindPolicyForOrigin(const url::Origin& origin) const;
  const OriginPolicy* FindWildcardPolicyForDomain(
      const std::string& domain) const;
  void MaybeAddWildcardPolicy(const url::Origin& origin,
                              const OriginPolicy* policy);
  void MaybeRemoveWildcardPolicy(const url::Origin& origin,
                                 const OriginPolicy* policy);
  std::unique_ptr<const base::Value> CreateReportBody(
      const std::string& type,
      const ErrorDetails& details) const;

  std::unique_ptr<base::TickClock> tick_clock_;

  // Unowned.
  ReportingService* reporting_service_;

  PolicyMap policies_;
  WildcardPolicyMap wildcard_policies_;

  DISALLOW_COPY_AND_ASSIGN(NetworkErrorLoggingService);
};

}  // namespace net

#endif  // NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_SERVICE_H_
